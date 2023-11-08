#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#include "vfl.h"
#include "mbr.h"
#include "gpt.h"

#define BYTES_PER_PAGE 4096
#define BYTES_PER_SPARE 64
#define PAGES_PER_BLOCK 128
#define VFL_CTX_PHYSICAL_PAGE PAGES_PER_BLOCK * 1
#define PAGES_PER_SUBLOCK 1024
#define CS_TOTAL 4

#define NAND_SIG_PAGE 524160
#define BBT_PAGE      524161

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

uint8_t *get_valid_ftl_spare() {
	uint32_t *spare = (uint32_t *)calloc(BYTES_PER_SPARE, sizeof(char));
	spare[2] = 0x00FF00FF;
	return (uint8_t *)spare;
}

void _Helper_ConvertP2C_OneBitReorder(uint32_t dwBank, uint32_t dwPpn, uint32_t* pdwCE, uint32_t* pdwCpn, uint32_t dwReorderMask)
{
    const uint32_t dwBelowReorderMask = dwReorderMask - 1;    // Assumption:  dwReorderMask is a power of 2!
    const uint32_t dwAboveReorderMask = ~dwBelowReorderMask;

    // insert reorder bit back in correct position of "chip" page number by extracting from MSB of "physical" bank number
    *pdwCpn = ((dwPpn & dwBelowReorderMask) |
               (((dwBank / CS_TOTAL) & 0x1) ? dwReorderMask : 0) |
               ((dwPpn & dwAboveReorderMask) << 1));

    // strip reorder bit from MSB of "physical" bank number to produce "chip" CE
    *pdwCE = dwBank % CS_TOTAL;
}

void _pfnConvertP2C_TwoPlaneLSB(uint32_t dwBank, uint32_t dwPpn, uint32_t* pdwCE, uint32_t* pdwCpn)
{
    _Helper_ConvertP2C_OneBitReorder(dwBank, dwPpn, pdwCE, pdwCpn, PAGES_PER_BLOCK);
}

void _ConvertT2P_Default(uint16_t wBank, uint16_t wTbn, uint16_t *pwPbn)
{
    uint32_t dwCpn, dwCS;
    _pfnConvertP2C_TwoPlaneLSB((uint32_t)wBank, (uint32_t)(wTbn * PAGES_PER_BLOCK), &dwCS, &dwCpn);
    *pwPbn = (uint16_t)(dwCpn / PAGES_PER_BLOCK);
}

void _Vpn2Ppn(uint32_t dwVpn, uint16_t *pwCS, uint32_t *pdwPpn) {
	uint16_t wPbn, wPOffset;
	uint16_t wBank = dwVpn % WMR_NUM_OF_BANKS;
	uint16_t wVbn = dwVpn / PAGES_PER_SUBLOCK;
	_ConvertT2P_Default((wBank & 0xffff), wVbn, &wPbn);
	*pwCS = wBank % CS_TOTAL;
    wPOffset = (uint16_t)((dwVpn % PAGES_PER_SUBLOCK) / WMR_NUM_OF_BANKS);
    *pdwPpn = wPbn * PAGES_PER_BLOCK + wPOffset;
}

void _Lpn2Ppn(uint32_t dwLpn, uint16_t *pwCS, uint32_t *pdwPpn) {
	uint32_t dwLbn = dwLpn / PAGES_PER_SUBLOCK;
    uint16_t wLPOffset = (uint16_t)(dwLpn - dwLbn * PAGES_PER_SUBLOCK);
	uint32_t dwVbn = dwLbn + 1;
	uint32_t dwVpn = dwVbn * PAGES_PER_SUBLOCK + wLPOffset;
	_Vpn2Ppn(dwVpn, pwCS, pdwPpn);
}

void write_page(uint8_t *page, uint8_t *spare, int cs, int page_index) {
	char filename[100];
	sprintf(filename, "nand/cs%d", cs);
	struct stat st = {0};
	if (stat(filename, &st) == -1) {
	    mkdir(filename, 0700);
	}

	sprintf(filename, "nand/cs%d/%d.page", cs, page_index);
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

void write_bbts() {
	// create the bad block table on each CS
	for(int cs = 0; cs < CS_TOTAL; cs++) {
		uint8_t *page = calloc(BYTES_PER_PAGE, sizeof(char));
		memcpy(page, "DEVICEINFOBBT\0\0\0", 16);

		// set all bits in this page to 1, to indicate that all blocks are of good health.
		for(int i = 16; i < BYTES_PER_PAGE; i++) {
			page[i] = 0xFF;
		}

		write_page(page, NULL, cs, BBT_PAGE);
	}
}

void write_nand_sig_page() {
	// write the NAND signature page
	uint8_t *page = calloc(0x100, sizeof(uint8_t));
    char *magic = "NANDDRIVERSIGN";
    memcpy(page, magic, strlen(magic));
    page[0x34] = 0x4; // length of the info

    // signature (0x43313131)
    page[0x38] = 0x31;
    page[0x39] = 0x31;
    page[0x3A] = 0x31;
    page[0x3B] = 0x43;
    write_page(page, NULL, 0, NAND_SIG_PAGE);
}

void write_vfl_context() {
	for (int wCSIdx = 0; wCSIdx < CS_TOTAL; wCSIdx++) {
		// initialize VFL context block on physical block 35, page 0
		VFLMeta *vfl_meta = (VFLMeta *)calloc(sizeof(VFLMeta), sizeof(char));
		vfl_meta->dwVersion = VFL_META_VERSION;

		VFLCxt *pVFLCxt = &vfl_meta->stVFLCxt;

		// we set the number of VFL/FTL user blocks to 2048 which doesn't give any room for the BBT table. This is fine as we do not have bad blocks.
		pVFLCxt->wNumOfVFLSuBlk = 2048;
		pVFLCxt->wNumOfFTLSuBlk = 2048;

		pVFLCxt->abVSFormtType = VFL_VENDOR_SPECIFIC_TYPE;
		pVFLCxt->dwGlobalCxtAge = wCSIdx;
		pVFLCxt->wCxtLocation = 1; // the VFL context is located in the first physical block
		for (int wIdx = 0; wIdx < FTL_CXT_SECTION_SIZE; wIdx++)
        {
            pVFLCxt->awFTLCxtVbn[wIdx] = (uint16_t)(wIdx);
        }
        pVFLCxt->wNumOfInitBadBlk = 0;

		for (int wIdx = 0, wPbn = VFL_FIRST_BLK_TO_SEARCH_CXT; wIdx < VFL_INFO_SECTION_SIZE && wPbn < VFL_LAST_BLK_TO_SEARCH_CXT; wPbn++)
        {
        	pVFLCxt->awInfoBlk[wIdx++] = wPbn;
        }

        // update the bad block map
		for(int i = 0; i < WMR_MAX_RESERVED_SIZE; i++) {
			pVFLCxt->awBadMapTable[i] = VFL_BAD_MAP_TABLE_AVAILABLE_MARK;
		}

		VFLSpare *vfl_ctx_spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
		vfl_ctx_spare->dwCxtAge = 1;
		vfl_ctx_spare->bSpareType = VFL_CTX_SPARE_TYPE;

		// store some copies of the VFL
		for (uint8_t wPageIdx = 0; wPageIdx < VFL_NUM_OF_VFL_CXT_COPIES; wPageIdx++) {
			write_page((uint8_t *)vfl_meta, (uint8_t *)vfl_ctx_spare, wCSIdx, VFL_CTX_PHYSICAL_PAGE + wPageIdx);
		}
	}
}

void write_ftl_context() {
	uint16_t cs;
	uint32_t ppn;

	// set the FTL spare type of the first page of the FTL CXT block to indicate that there is a CTX index
	VFLSpare *vfl_ctx_spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
	vfl_ctx_spare->bSpareType = FTL_SPARE_TYPE_CXT_INDEX;
	write_page(NULL, (uint8_t *)vfl_ctx_spare, 0, 0);

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
		ftl_meta->stFTLCxt.adwMapTablePtrs[i] = i + 5; // the mapping will start from the 2nd page in the FTL context block

		uint16_t *mapping_page = calloc(BYTES_PER_PAGE / sizeof(uint16_t), sizeof(uint16_t));
		uint32_t items_per_map = BYTES_PER_PAGE / sizeof(uint16_t);
		for(int ind_in_map = 0; ind_in_map < items_per_map; ind_in_map++) {
			mapping_page[ind_in_map] = (i * items_per_map) + ind_in_map + 1;
		}
		_Vpn2Ppn(i + 5, &cs, &ppn);
		printf("Writing logical -> virtual block map page %d to physical page %d @ cs %d\n", i, ppn, cs);
		write_page((uint8_t *)mapping_page, NULL, cs, ppn);
	}

	vfl_ctx_spare = (VFLSpare *)calloc(BYTES_PER_SPARE, sizeof(char));
	vfl_ctx_spare->bSpareType = FTL_SPARE_TYPE_CXT_INDEX;

	// we place the FTL Meta on the last page of the first virtual block.
	_Vpn2Ppn(PAGES_PER_SUBLOCK - 1, &cs, &ppn);

	printf("Writing FTL Meta to physical page %d @ cs %d\n", ppn, cs);
	write_page((uint8_t *)ftl_meta, (uint8_t *)vfl_ctx_spare, cs, ppn);
}

uint32_t write_hfs_partition(char *filename, uint32_t page_offset) {
	// write the HFS+ partition to the first page and update the associated spare
	uint16_t cs; uint32_t ppn;

	FILE *hfs_file = fopen(filename, "rb");
	fseek(hfs_file, 0L, SEEK_END);
	int partition_size = ftell(hfs_file);
	fclose(hfs_file);

	hfs_file = fopen(filename, "rb");
	int lpn = page_offset;
	uint8_t *spare = get_valid_ftl_spare();
	uint32_t required_pages_for_partition = partition_size / BYTES_PER_PAGE;
	printf("Writing HFS partition using %d pages...\n", required_pages_for_partition);
	for(int i = 0; i < required_pages_for_partition; i++) {
		uint8_t *page = malloc(BYTES_PER_PAGE);
		fread(page, BYTES_PER_PAGE, sizeof(uint8_t), hfs_file);
		_Lpn2Ppn(lpn, &cs, &ppn);
		printf("Writing HFS partition to physical page %d @ cs %d\n", ppn, cs);
		write_page(page, spare, cs, ppn);
		lpn++;

		//if(i == 2000) break;
	}
	fclose(hfs_file);

	return required_pages_for_partition;
}

void write_mbr(int boot_partition_size) {
	uint16_t cs; uint32_t ppn;

	// write the MBR bytes (LBA 0)
	uint8_t *mbr_page = malloc(BYTES_PER_PAGE);
	struct mbr_partition *boot_partition = (struct mbr_partition *)(mbr_page + MBR_ADDRESS);
	boot_partition->sysid = 0xEE;
	boot_partition->startlba = BOOT_PARTITION_FIRST_PAGE;
	boot_partition->size = boot_partition_size;

	mbr_page[510] = 0x55;
	mbr_page[511] = 0xAA;

	_Lpn2Ppn(0, &cs, &ppn);
	printf("Writing MBR to physical page %d @ cs %d\n", ppn, cs);
	uint8_t *spare = get_valid_ftl_spare();
	write_page(mbr_page, spare, cs, ppn);
}

void write_filesystem() {
	uint16_t cs; uint32_t ppn;

	int pages_for_boot_partition = write_hfs_partition("filesystem-it2g-readonly.img", BOOT_PARTITION_FIRST_PAGE);
	printf("Required pages for boot partition: %d\n", pages_for_boot_partition);

	// initialize the EFI header (LBA 1)
	uint8_t *gpt_header_page = malloc(BYTES_PER_PAGE);
	gpt_hdr *gpt_header = (gpt_hdr *)gpt_header_page;

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

	_Lpn2Ppn(2, &cs, &ppn);
	printf("Writing GUID boot partition entry to page %d @ cs %d\n", ppn, cs);
	uint8_t *spare = get_valid_ftl_spare();
	write_page(gpt_entry_boot_partition_page, spare, cs, ppn);

	// finalize the GPT header
	// TODO add the secondary GPT entry!
	memcpy(gpt_header, GPT_HDR_SIG, 8);
	gpt_header->hdr_revision = GPT_HDR_REVISION;
	gpt_header->hdr_size = 0x5C; // 92 bytes
	gpt_header->hdr_lba_table = 2;
	gpt_header->hdr_entries = NUM_PARTITIONS;
	gpt_header->hdr_entsz = 0x80;
	gpt_header->hdr_crc_table = crc32(gpt_entry_boot_partition_page, sizeof(gpt_ent));
	gpt_header->hdr_crc_self = crc32((uint8_t *)gpt_header, 0x5C);

	_Lpn2Ppn(1, &cs, &ppn);
	printf("Writing GUID header to page %d @ cs %d\n", ppn, cs);
	write_page(gpt_header_page, spare, cs, ppn);

	// finally, write the MBR
	write_mbr(pages_for_boot_partition);
}

int main(int argc, char *argv[]) {
	// create the output dir if it does not exist
	struct stat st = {0};

	if (stat("nand", &st) == -1) {
	    mkdir("nand", 0700);
	}

	write_vfl_context();
	write_ftl_context();
	write_nand_sig_page();
	write_bbts();
	write_filesystem();

	// testing
	uint32_t num = 49188;
	uint16_t cs; uint32_t ppn;
	_Lpn2Ppn(num, &cs, &ppn);
	printf("LPN %d => %d, cs %d\n", num, ppn, cs);
}