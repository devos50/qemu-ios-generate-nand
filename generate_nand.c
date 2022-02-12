#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#include "vfl.h"
#include "mbr.h"
#include "gpt.h"

#define BANKS 8
#define BLOCKS_PER_BANK 4096
#define PAGES_PER_BANK 524288
#define BYTES_PER_PAGE 2048
#define PAGES_PER_SUBLOCK 1024
#define PAGES_PER_BLOCK 128
#define BYTES_PER_SPARE 64
#define FIL_ID 0x43303032
#define FTL_CTX_VBLK_IND 0 // virtual block index of the FTL Context

#define NUM_PARTITIONS 1
#define BOOT_PARTITION_FIRST_PAGE (NUM_PARTITIONS + 2)  // first two LBAs are for MBR and GUID header

static uint32_t crc32_table[256];
static int crc32_table_computed = 0;

/*
CRC32 logic
*/
static void make_crc32_table(void)
{
    uint32_t c;
    int n, k;

    for (n = 0; n < 256; n++) {
            c = (uint32_t) n;
            for (k = 0; k < 8; k++) {
                    if (c & 1)
                            c = 0xedb88320L ^ (c >> 1);
                    else
                            c = c >> 1;
            }
            crc32_table[n] = c;
    }
    crc32_table_computed = 1;
}

uint32_t update_crc32(uint32_t crc, const uint8_t *buf,
                         int len)
{
    uint32_t c = crc;
    int n;

    if (!crc32_table_computed)
            make_crc32_table();
    for (n = 0; n < len; n++) {
            c = crc32_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    }
    return c;
}

uint32_t crc32(const uint8_t *buf, int len)
{
    return update_crc32(0xffffffffL, buf, len) ^ 0xffffffffL;
}

void get_physical_address(uint32_t vpn, uint32_t *bank, uint32_t *physical_block_index, uint32_t *page_in_block) {
	*bank = vpn % BANKS;
	*physical_block_index = vpn / PAGES_PER_SUBLOCK;
	*page_in_block = (vpn / BANKS) % PAGES_PER_BLOCK;
}

void write_page(uint8_t *page, uint8_t *spare, int bank, int page_index) {
	char filename[100];
	sprintf(filename, "nand/bank%d", bank);
	struct stat st = {0};
	if (stat(filename, &st) == -1) {
	    mkdir(filename, 0700);
	}

	sprintf(filename, "nand/bank%d/%d.page", bank, page_index);
	FILE *f = fopen(filename, "wb");

	if(!page) {
		page = (uint8_t *)calloc(BYTES_PER_PAGE, sizeof(char));
	}
	fwrite(page, sizeof(char), BYTES_PER_PAGE, f);

	if(!spare) {
		spare = (uint8_t *)calloc(BYTES_PER_SPARE, sizeof(char));
	}
	fwrite(spare, sizeof(char), BYTES_PER_SPARE, f);

	fclose(f);
}

void write_fil_id() {
	// set the FIL ID on the very first byte of the first bank
	uint8_t *page0_b0 = (uint8_t *)calloc(BYTES_PER_PAGE, sizeof(char));
	uint8_t *spare_page0_b0 = (uint8_t *)calloc(BYTES_PER_SPARE, sizeof(char));
	((uint32_t *)page0_b0)[0] = FIL_ID;
	write_page(page0_b0, spare_page0_b0, 0, 0);
}

void write_bbts() {
	// create the bad block table on the first physical page of the last physical block
	for(int bank = 0; bank < BANKS; bank++) {
		uint8_t *page = calloc(BYTES_PER_PAGE, sizeof(char));
		memcpy(page, "DEVICEINFOBBT\0\0\0", 16);
		write_page(page, NULL, bank, PAGES_PER_BANK - PAGES_PER_BLOCK);
	}
}

void write_vfl_context() {
	for(int bank = 0; bank < BANKS; bank++) {
		// initialize VFL context block on physical block 35, page 0
		VFLMeta *vfl_meta = (VFLMeta *)calloc(sizeof(VFLMeta), sizeof(char));
		vfl_meta->stVFLCxt.awInfoBlk[0] = 35;

		// write the bad mark table
		for(int i = 0; i < VFL_BAD_MARK_INFO_TABLE_SIZE; i++) {
			vfl_meta->stVFLCxt.aBadMark[i] = 0xff;
		}

		VFLSpare *vfl_ctx_spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
		vfl_ctx_spare->dwCxtAge = 1;
		vfl_ctx_spare->bSpareType = VFL_CTX_SPARE_TYPE;

		// indicate the location of the FTL CTX block
		vfl_meta->stVFLCxt.aFTLCxtVbn[0] = FTL_CTX_VBLK_IND;
		vfl_meta->stVFLCxt.aFTLCxtVbn[1] = FTL_CTX_VBLK_IND;
		vfl_meta->stVFLCxt.aFTLCxtVbn[2] = FTL_CTX_VBLK_IND;

		write_page((uint8_t *)vfl_meta, (uint8_t *)vfl_ctx_spare, bank, 35 * PAGES_PER_BLOCK);
	}
}

void write_ftl_context() {
	uint32_t bank, pbi, pib;

	// set the FTL spare type of the first page of the FTL CXT block to indicate that there is a CTX index
	VFLSpare *vfl_ctx_spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
	vfl_ctx_spare->bSpareType = FTL_SPARE_TYPE_CXT_INDEX;
	vfl_ctx_spare->eccMarker = 0xff;
	get_physical_address( (FTL_CXT_SECTION_START + FTL_CTX_VBLK_IND) * PAGES_PER_SUBLOCK, &bank, &pbi, &pib);
	write_page(NULL, (uint8_t *)vfl_ctx_spare, 0, pbi * PAGES_PER_BLOCK + pib);

	// create the FTL Meta page on the last page of the FTL Cxt block and embed the right versions
	FTLMeta *ftl_meta = (FTLMeta *)calloc(sizeof(FTLMeta), sizeof(char));
	ftl_meta->dwVersion = 0x46560000;
	ftl_meta->dwVersionNot = -0x46560001;

	// initialize the number of empty VBs for logs and the free areas
	ftl_meta->stFTLCxt.wNumOfFreeVb = FREE_SECTION_SIZE;
	for(int i = 0; i < FREE_SECTION_SIZE; i++) {
		ftl_meta->stFTLCxt.awFreeVbList[i] = FREE_SECTION_START + i;
	}

	// create empty logs
	for(int i = 0; i < LOG_SECTION_SIZE + 1; i++) {
		ftl_meta->stFTLCxt.aLOGCxtTable[i].wVbn = 0xFFFF;
	}

	// prepare the logical block -> virtual block mapping tables
	for(int i = 0; i < MAX_NUM_OF_MAP_TABLES; i++) {
		ftl_meta->stFTLCxt.adwMapTablePtrs[i] = i + 1; // the mapping will start from the 2nd page in the FTL context block

		uint16_t *mapping_page = calloc(BYTES_PER_PAGE / sizeof(uint16_t), sizeof(uint16_t));
		for(int ind_in_map = 0; ind_in_map < 1024; ind_in_map++) {
			mapping_page[ind_in_map] = (i * 1024) + ind_in_map + 1;
		}
		get_physical_address( FTL_CXT_SECTION_START * PAGES_PER_SUBLOCK + i + 1, &bank, &pbi, &pib);
		//printf("Writing to bank %d and page %d (vp: %d)\n", bank, pbi * PAGES_PER_BLOCK + pib, FTL_CXT_SECTION_START * PAGES_PER_SUBLOCK + i + 1);
		write_page((uint8_t *)mapping_page, NULL, bank, pbi * PAGES_PER_BLOCK + pib);
	}

	vfl_ctx_spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
	vfl_ctx_spare->bSpareType = FTL_SPARE_TYPE_CXT_INDEX;
	get_physical_address( (FTL_CXT_SECTION_START + FTL_CTX_VBLK_IND + 1) * PAGES_PER_SUBLOCK - 1, &bank, &pbi, &pib);
	printf("Writing FTL Meta to virtual page %d\n", (FTL_CXT_SECTION_START + FTL_CTX_VBLK_IND + 1) * PAGES_PER_SUBLOCK - 1);
	write_page((uint8_t *)ftl_meta, (uint8_t *)vfl_ctx_spare, bank, pbi * PAGES_PER_BLOCK + pib);
}

uint32_t write_hfs_partition(char *filename, uint32_t page_offset) {
	// write the HFS+ partition to the first page and update the associated spare
	uint32_t bank, pbi, pib;
	FILE *hfs_file = fopen(filename, "rb");
	fseek(hfs_file, 0L, SEEK_END);
	int partition_size = ftell(hfs_file);
	fclose(hfs_file);

	hfs_file = fopen(filename, "rb");
	for(int i = 0; i < partition_size / BYTES_PER_PAGE; i++) {
		uint8_t *page = malloc(BYTES_PER_PAGE);
		fread(page, BYTES_PER_PAGE, sizeof(uint8_t), hfs_file);
		VFLSpare *spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
		spare->eccMarker = 0xff;
		get_physical_address((FTL_CXT_SECTION_START + 1) * PAGES_PER_SUBLOCK + i + page_offset, &bank, &pbi, &pib);
		if(i == 0) {
			printf("Writing HFS partition offset %d to virtual page %d (writing to bank %d, page %d)\n", i * BYTES_PER_PAGE, (FTL_CXT_SECTION_START + 1) * PAGES_PER_SUBLOCK + i + page_offset, bank, pbi * PAGES_PER_BLOCK + pib);
		}
		write_page(page, (uint8_t *)spare, bank, pbi * PAGES_PER_BLOCK + pib);
	}
	fclose(hfs_file);

	return partition_size / BYTES_PER_PAGE;
}

void write_mbr(int boot_partition_size) {
	uint32_t bank, pbi, pib;

	// write the MBR bytes (LBA 0)
	uint8_t *mbr_page = malloc(BYTES_PER_PAGE);
	get_physical_address((FTL_CXT_SECTION_START + 1) * PAGES_PER_SUBLOCK, &bank, &pbi, &pib);
	struct mbr_partition *boot_partition = (struct mbr_partition *)(mbr_page + MBR_ADDRESS);
	boot_partition->sysid = 0xEE;
	boot_partition->startlba = BOOT_PARTITION_FIRST_PAGE;
	boot_partition->size = boot_partition_size;

	// struct mbr_partition *filesystem_partition = (struct mbr_partition *)(mbr_page + MBR_ADDRESS + sizeof(struct mbr_partition));
	// filesystem_partition->sysid = 0xEE;
	// filesystem_partition->startlba = filesystem_partition_offset;
	// filesystem_partition->size = filesystem_partition_size;

	mbr_page[510] = 0x55;
	mbr_page[511] = 0xAA;

	VFLSpare *spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
	spare->eccMarker = 0xff;

	printf("Writing MBR to page %d, bank %d\n", pbi * PAGES_PER_BLOCK + pib, bank);
	write_page(mbr_page, (uint8_t *)spare, bank, pbi * PAGES_PER_BLOCK + pib);
}

void write_filesystem() {
	int pages_for_boot_partition = write_hfs_partition("filesystem-readonly.img", BOOT_PARTITION_FIRST_PAGE);
	printf("Required pages for boot partition: %d\n", pages_for_boot_partition);
	//int pages_for_filesystem_partition = write_hfs_partition("filesystem_full.part", BOOT_PARTITION_FIRST_PAGE + pages_for_boot_partition + 1);
	//printf("Required pages for filesystem partition: %d\n", pages_for_filesystem_partition);

	// initialize the EFI header (LBA 1)
	uint32_t bank, pbi, pib;
	uint8_t *gpt_header_page = malloc(BYTES_PER_PAGE);
	gpt_hdr *gpt_header = (gpt_hdr *)gpt_header_page;

	VFLSpare *spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
	spare->eccMarker = 0xff;

	// create the boot partition entry (LBA 2)
	uint8_t *gpt_entry_boot_partition_page = malloc(BYTES_PER_PAGE);
	gpt_ent *gpt_entry_boot_partition = (gpt_ent *)gpt_entry_boot_partition_page;
	gpt_entry_boot_partition->ent_type[0] = 0x48465300;
	gpt_entry_boot_partition->ent_type[1] = 0x11AA0000;
	gpt_entry_boot_partition->ent_type[2] = 0x300011AA;
	gpt_entry_boot_partition->ent_type[3] = 0xACEC4365;
	gpt_entry_boot_partition->ent_lba_start = BOOT_PARTITION_FIRST_PAGE;
	gpt_entry_boot_partition->ent_lba_end = BOOT_PARTITION_FIRST_PAGE + pages_for_boot_partition;
	printf("Boot system partition located on page %lld - %lld\n", gpt_entry_boot_partition->ent_lba_start, gpt_entry_boot_partition->ent_lba_end);

	get_physical_address((FTL_CXT_SECTION_START + 1) * PAGES_PER_SUBLOCK + 2, &bank, &pbi, &pib);
	printf("Writing GUID boot partition entry to page %d, bank %d\n", pbi * PAGES_PER_BLOCK + pib, bank);
	write_page(gpt_entry_boot_partition_page, (uint8_t *)spare, bank, pbi * PAGES_PER_BLOCK + pib);

	// // create the file system partition entry (LBA 3)
	// uint8_t *gpt_entry_filesystem_partition_page = malloc(BYTES_PER_PAGE);
	// gpt_ent *gpt_entry_filesystem_partition = (gpt_ent *)gpt_entry_filesystem_partition_page;
	// gpt_entry_filesystem_partition->ent_type[0] = 0x48465300;
	// gpt_entry_filesystem_partition->ent_type[1] = 0x11AA0000;
	// gpt_entry_filesystem_partition->ent_type[2] = 0x300011AA;
	// gpt_entry_filesystem_partition->ent_type[3] = 0xACEC4365;
	// gpt_entry_filesystem_partition->ent_lba_start = BOOT_PARTITION_FIRST_PAGE + pages_for_boot_partition + 1;
	// gpt_entry_filesystem_partition->ent_lba_end = gpt_entry_filesystem_partition->ent_lba_start + pages_for_filesystem_partition;
	// printf("File system partition located on page %lld - %lld\n", gpt_entry_filesystem_partition->ent_lba_start, gpt_entry_filesystem_partition->ent_lba_end);

	// get_physical_address((FTL_CXT_SECTION_START + 1) * PAGES_PER_SUBLOCK + 3, &bank, &pbi, &pib);
	// printf("Writing GUID filesystem partition entry to page %d, bank %d\n", pbi * PAGES_PER_BLOCK + pib, bank);
	// write_page(gpt_entry_filesystem_partition_page, (uint8_t *)spare, bank, pbi * PAGES_PER_BLOCK + pib);

	// finalize the GPT header
	memcpy(gpt_header, GPT_HDR_SIG, 8);
	gpt_header->hdr_revision = GPT_HDR_REVISION;
	gpt_header->hdr_size = 0x5C; // 92 bytes
	gpt_header->hdr_lba_table = 2;
	gpt_header->hdr_entries = NUM_PARTITIONS;
	gpt_header->hdr_entsz = 0x80;
	gpt_header->hdr_crc_table = crc32(gpt_entry_boot_partition_page, sizeof(gpt_ent));
	gpt_header->hdr_crc_self = crc32((uint8_t *)gpt_header, 0x5C);

	get_physical_address((FTL_CXT_SECTION_START + 1) * PAGES_PER_SUBLOCK + 1, &bank, &pbi, &pib);
	printf("Writing GUID header to page %d, bank %d\n", pbi * PAGES_PER_BLOCK + pib, bank);
	write_page(gpt_header_page, (uint8_t *)spare, bank, pbi * PAGES_PER_BLOCK + pib);

	// finally, write the MBR
	write_mbr(pages_for_boot_partition);
}

int main(int argc, char *argv[]) {

	// create the output dir if it does not exist
	struct stat st = {0};

	if (stat("nand", &st) == -1) {
	    mkdir("nand", 0700);
	}

	write_fil_id();
	write_bbts();
	write_vfl_context();
	write_ftl_context();
	write_filesystem();
}