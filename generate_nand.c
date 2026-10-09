#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include "vfl.h"
#include "mbr.h"
#include "gpt.h"

/*
 * Generates a NAND image for the iPod Touch 2G (4 CEs of Samsung 0xb614d5ad NAND), formatted the way the
 * VSVFL production format and FTL_Format of the iPhone OS 2.x kernel lay it out. Every written page is stored as
 * <out>/cs<ce>/<page>.page and holds the page data followed by the spare area. Pages without a file are erased.
 */

#define BYTES_PER_PAGE 4096
#define BYTES_PER_SPARE 64
#define META_SIZE 12                            // the part of the spare area that the FTL/VFL sees
#define PAGES_PER_BLOCK 128
#define CS_TOTAL 4
#define BLOCKS_PER_CE 4096
#define BLOCKS_PER_BANK (BLOCKS_PER_CE / 2)     // the two planes of a CE are exposed as two banks
#define PAGES_PER_SUBLOCK (PAGES_PER_BLOCK * WMR_NUM_OF_BANKS)

// VSVFL gives 976 out of every 1024 blocks to the user super blocks, the rest is reserved to replace bad blocks
#define VFL_NUM_OF_SUBLKS ((0x3d0 * BLOCKS_PER_BANK) / 1024)
#define VFL_RESERVED_BLOCKS_PER_BANK (BLOCKS_PER_BANK - VFL_NUM_OF_SUBLKS)

// the VFL context lives in the first blocks after block 0; these blocks (and block 0) are remapped to reserved blocks
#define VFL_CXT_BLOCK 1

// ages count down, the context with the lowest age is the newest one
#define VFL_INITIAL_CXT_AGE 0xFFFFFF00
#define FTL_INITIAL_CXT_AGE 0xFFFFFFFF

// both the NAND signature and the bad block table are stored in the last block of each CE
#define SPECIAL_BLOCK (BLOCKS_PER_CE - 1)
#define NAND_SIG_PAGE (SPECIAL_BLOCK * PAGES_PER_BLOCK)
#define BBT_PAGE      (SPECIAL_BLOCK * PAGES_PER_BLOCK + 1)
#define SPECIAL_BLOCK_LEN_OFFSET 0x34
#define SPECIAL_BLOCK_DATA_OFFSET 0x38

// FTL layout: super blocks 0-2 hold the FTL context, 3-22 are free blocks and the data blocks follow
#define FTL_DATA_VBN_START (FTL_CXT_SECTION_SIZE + FREE_SECTION_SIZE)
#define FTL_NUM_OF_LBNS (VFL_NUM_OF_SUBLKS - FTL_DATA_VBN_START)
#define FTL_CXT_VBN 0
#define FTL_MAP_TABLE_FIRST_VPN 5

#define FTL_SPARE_TYPE_DATA 0x41

#define NUM_PARTITIONS 1
#define BOOT_PARTITION_FIRST_PAGE (NUM_PARTITIONS + 2)  // first two LBAs are for MBR and GUID header
#define DISK_NUM_OF_LBAS ((uint64_t)FTL_NUM_OF_LBNS * PAGES_PER_SUBLOCK)

static const char *out_dir = "nand";

static uint32_t crc32_table[256];
static int crc32_table_computed = 0;

// chip blocks that are replaced by a block in the reserved area of the same plane (block 0 and the VFL info blocks)
static const uint16_t remapped_blocks[] = { 0, 1, 2, 3, 4 };
#define NUM_REMAPPED_BLOCKS ((int)(sizeof(remapped_blocks) / sizeof(remapped_blocks[0])))

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

static uint32_t update_crc32(uint32_t crc, const uint8_t *buf, int len)
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

static uint32_t crc32(const uint8_t *buf, int len)
{
    return update_crc32(0xffffffffL, buf, len) ^ 0xffffffffL;
}

static void *xcalloc(size_t size)
{
    void *p = calloc(1, size);
    if (!p) {
        perror("calloc");
        exit(1);
    }
    return p;
}

// Returns the chip block that replaces a remapped chip block, or the block itself if it is not remapped.
// VSVFL takes replacement i of a plane from virtual block wNumOfVFLSuBlk + i of that plane's bank.
static uint32_t remap_chip_block(uint32_t chip_block)
{
    uint32_t plane_idx[2] = { 0, 0 };
    for (int i = 0; i < NUM_REMAPPED_BLOCKS; i++) {
        uint32_t plane = remapped_blocks[i] & 1;
        if (remapped_blocks[i] == chip_block) {
            return (VFL_NUM_OF_SUBLKS + plane_idx[plane]) * 2 + plane;
        }
        plane_idx[plane]++;
    }
    return chip_block;
}

// Converts a virtual page number to a CE and a physical page on that CE, like VSVFL does.
static void vpn_to_ppn(uint32_t vpn, uint16_t *ce, uint32_t *ppn)
{
    uint32_t bank = vpn % WMR_NUM_OF_BANKS;
    uint32_t vbn = vpn / PAGES_PER_SUBLOCK;
    uint32_t page_in_block = (vpn % PAGES_PER_SUBLOCK) / WMR_NUM_OF_BANKS;

    // two-plane LSB reordering: the banks >= CS_TOTAL map to the odd blocks of a CE
    uint32_t chip_block = vbn * 2 + (bank / CS_TOTAL);
    *ce = bank % CS_TOTAL;
    *ppn = remap_chip_block(chip_block) * PAGES_PER_BLOCK + page_in_block;
}

static void lpn_to_ppn(uint32_t lpn, uint16_t *ce, uint32_t *ppn)
{
    uint32_t lbn = lpn / PAGES_PER_SUBLOCK;
    uint32_t vbn = lbn + FTL_DATA_VBN_START;
    vpn_to_ppn(vbn * PAGES_PER_SUBLOCK + lpn % PAGES_PER_SUBLOCK, ce, ppn);
}

static void write_page(const uint8_t *page, const uint8_t *meta, int ce, uint32_t page_index) {
    static const uint8_t zero_page[BYTES_PER_PAGE];
    char filename[1024];
    uint8_t spare[BYTES_PER_SPARE];

    snprintf(filename, sizeof(filename), "%s/cs%d", out_dir, ce);
    if (mkdir(filename, 0700) == -1 && errno != EEXIST) {
        perror(filename);
        exit(1);
    }

    snprintf(filename, sizeof(filename), "%s/cs%d/%u.page", out_dir, ce, page_index);
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    // the part of the spare area that is not used by the meta data stays erased
    memset(spare, 0xFF, sizeof(spare));
    if (meta) {
        memcpy(spare, meta, META_SIZE);
    }

    fwrite(page ? page : zero_page, 1, BYTES_PER_PAGE, f);
    fwrite(spare, 1, BYTES_PER_SPARE, f);
    fclose(f);
}

// Writes a page of a data block, with the meta data that FTL_Read and _FTLRestore expect.
static void write_data_page(const uint8_t *page, uint32_t lpn)
{
    uint16_t ce; uint32_t ppn;
    uint8_t meta[META_SIZE];

    memset(meta, 0xFF, sizeof(meta));   // bytes 8 and 10 must be 0xFF, this is the ECC mark
    memcpy(meta, &lpn, sizeof(lpn));    // the logical page stored in this page
    memset(meta + 4, 0, 4);             // write age, older than anything the FTL writes later
    meta[9] = FTL_SPARE_TYPE_DATA;

    lpn_to_ppn(lpn, &ce, &ppn);
    write_page(page, meta, ce, ppn);
}

// Writes a page in the format that the VFL uses for its special blocks: a signature, the data length and the data.
static void write_special_page(const char *magic, const void *data, uint32_t len, uint32_t page_index)
{
    for (int ce = 0; ce < CS_TOTAL; ce++) {
        uint8_t *page = xcalloc(BYTES_PER_PAGE);
        memcpy(page, magic, strlen(magic));
        memcpy(page + SPECIAL_BLOCK_LEN_OFFSET, &len, sizeof(len));
        memcpy(page + SPECIAL_BLOCK_DATA_OFFSET, data, len);
        write_page(page, NULL, ce, page_index);
        free(page);
    }
}

static void write_bbts(void) {
    // all blocks are good; the VFL rebuilds its bad block map from awBadMapTable when it opens the NAND
    uint8_t bbt[BLOCKS_PER_CE / 8];
    memset(bbt, 0xFF, sizeof(bbt));
    write_special_page("DEVICEINFOBBT", bbt, sizeof(bbt), BBT_PAGE);
}

static void write_nand_sig_page(void) {
    uint32_t signature = 0x43313131;
    write_special_page("NANDDRIVERSIGN", &signature, sizeof(signature), NAND_SIG_PAGE);
}

static void write_vfl_context(void) {
    for (int ce = 0; ce < CS_TOTAL; ce++) {
        VFLMeta *vfl_meta = xcalloc(sizeof(VFLMeta));
        vfl_meta->dwVersion = VFL_META_VERSION;

        VFLCxt *pVFLCxt = &vfl_meta->stVFLCxt;
        pVFLCxt->dwGlobalCxtAge = ce;
        pVFLCxt->dwCxtAge = VFL_INITIAL_CXT_AGE;
        pVFLCxt->wNumOfVFLSuBlk = VFL_NUM_OF_SUBLKS;
        pVFLCxt->wNumOfFTLSuBlk = VFL_NUM_OF_SUBLKS;
        pVFLCxt->abVSFormtType = VFL_VENDOR_SPECIFIC_TYPE;

        // the context is written in groups of VFL_NUM_OF_VFL_CXT_COPIES pages to info block wCxtLocation (an index in
        // awInfoBlk); the next group goes after the first one
        pVFLCxt->wCxtLocation = 0;
        pVFLCxt->wNextCxtPOffset = VFL_NUM_OF_VFL_CXT_COPIES;
        for (int i = 0; i < VFL_INFO_SECTION_SIZE; i++) {
            pVFLCxt->awInfoBlk[i] = VFL_CXT_BLOCK + i;
        }
        for (int i = 0; i < FTL_CXT_SECTION_SIZE; i++) {
            pVFLCxt->awFTLCxtVbn[i] = FTL_CXT_VBN + i;
        }

        // replace the blocks that overlap with the VFL info area by blocks from the reserved area of the same plane
        for (int i = 0; i < WMR_MAX_RESERVED_SIZE; i++) {
            pVFLCxt->awBadMapTable[i] = VFL_BAD_MAP_TABLE_AVAILABLE_MARK;
        }
        uint16_t plane_idx[2] = { 0, 0 };
        for (int i = 0; i < NUM_REMAPPED_BLOCKS; i++) {
            uint16_t plane = remapped_blocks[i] & 1;
            pVFLCxt->awBadMapTable[plane * VFL_RESERVED_BLOCKS_PER_BANK + plane_idx[plane]] = remapped_blocks[i];
            plane_idx[plane]++;
        }
        pVFLCxt->awReplacementIdx[0] = plane_idx[0];
        pVFLCxt->awReplacementIdx[1] = plane_idx[1];
        pVFLCxt->wNumOfInitBadBlk = NUM_REMAPPED_BLOCKS;

        // the last reserved block of the second plane holds the NAND signature and the BBT, never use it as replacement
        pVFLCxt->awBadMapTable[2 * VFL_RESERVED_BLOCKS_PER_BANK - 1] = VFL_BAD_MAP_TABLE_UNUSABLE_MARK;

        VFLSpare vfl_ctx_spare;
        memset(&vfl_ctx_spare, 0xFF, sizeof(vfl_ctx_spare));
        vfl_ctx_spare.dwCxtAge = pVFLCxt->dwCxtAge;
        vfl_ctx_spare.dwReserved = 0;
        vfl_ctx_spare.cStatusMark = 0;
        vfl_ctx_spare.bSpareType = VFL_CTX_SPARE_TYPE;

        for (int i = 0; i < VFL_NUM_OF_VFL_CXT_COPIES; i++) {
            write_page((uint8_t *)vfl_meta, (uint8_t *)&vfl_ctx_spare, ce, VFL_CXT_BLOCK * PAGES_PER_BLOCK + i);
        }
        free(vfl_meta);
    }
}

static void write_ftl_page(const uint8_t *page, uint32_t vpn)
{
    uint16_t ce; uint32_t ppn;
    VFLSpare spare;

    memset(&spare, 0xFF, sizeof(spare));
    spare.dwCxtAge = FTL_INITIAL_CXT_AGE;
    spare.dwReserved = 0;
    spare.bSpareType = FTL_SPARE_TYPE_CXT_INDEX;

    vpn_to_ppn(vpn, &ce, &ppn);
    write_page(page, (uint8_t *)&spare, ce, ppn);
}

static void write_ftl_context(void) {
    uint32_t ctx_vpn = FTL_CXT_VBN * PAGES_PER_SUBLOCK;

    // the first page of a FTL context block identifies the block (and holds the empty EC/RC/log tables, see below)
    write_ftl_page(NULL, ctx_vpn);

    FTLMeta *ftl_meta = xcalloc(sizeof(FTLMeta));
    ftl_meta->dwVersion = 0x46560000;
    ftl_meta->dwVersionNot = ~ftl_meta->dwVersion;
    ftl_meta->stFTLCxt.dwAge = FTL_INITIAL_CXT_AGE - 1;
    ftl_meta->stFTLCxt.dwWriteAge = 1;

    ftl_meta->stFTLCxt.wNumOfFreeVb = FREE_SECTION_SIZE;
    for (int i = 0; i < FREE_SECTION_SIZE; i++) {
        ftl_meta->stFTLCxt.awFreeVbList[i] = FREE_SECTION_START + i;
    }

    for (int i = 0; i < LOG_SECTION_SIZE + 1; i++) {
        ftl_meta->stFTLCxt.aLOGCxtTable[i].wVbn = 0xFFFF;
    }

    for (int i = 0; i < FTL_CXT_SECTION_SIZE; i++) {
        ftl_meta->stFTLCxt.awMapCxtVbn[i] = FTL_CXT_VBN + i;
    }

    // the logical block -> virtual block mapping tables
    uint32_t items_per_map = BYTES_PER_PAGE / sizeof(uint16_t);
    for (uint32_t i = 0; i < MAX_NUM_OF_MAP_TABLES; i++) {
        uint32_t vpn = ctx_vpn + FTL_MAP_TABLE_FIRST_VPN + i;
        ftl_meta->stFTLCxt.adwMapTablePtrs[i] = vpn;

        uint16_t *mapping_page = xcalloc(BYTES_PER_PAGE);
        for (uint32_t ind_in_map = 0; ind_in_map < items_per_map; ind_in_map++) {
            uint32_t lbn = i * items_per_map + ind_in_map;
            mapping_page[ind_in_map] = lbn < FTL_NUM_OF_LBNS ? lbn + FTL_DATA_VBN_START : 0xFFFF;
        }
        write_ftl_page((uint8_t *)mapping_page, vpn);
        free(mapping_page);
    }

    // the erase counter, log context and read counter tables are all zero, so they can all use the first (empty) page
    for (uint32_t i = 0; i < MAX_NUM_OF_EC_TABLES; i++) {
        ftl_meta->stFTLCxt.adwECTablePtrs[i] = ctx_vpn;
        ftl_meta->stFTLCxt.adwRCTablePtrs[i] = ctx_vpn;
    }
    for (uint32_t i = 0; i < MAX_NUM_OF_LOGCXT_MAPS; i++) {
        ftl_meta->stFTLCxt.adwLOGCxtMapPtrs[i] = ctx_vpn;
    }
    ftl_meta->stFTLCxt.adwStatPtrs[0] = 0xFFFFFFFF;
    ftl_meta->stFTLCxt.adwStatPtrs[1] = 0xFFFFFFFF;

    // the FTL meta data goes into the last page of the context block, the FTL searches it backwards from there
    uint32_t meta_vpn = ctx_vpn + PAGES_PER_SUBLOCK - 1;
    ftl_meta->stFTLCxt.dwCurrMapCxtPage = meta_vpn;
    ftl_meta->stFTLCxt.boolFlashCxtIsValid = 1;
    write_ftl_page((uint8_t *)ftl_meta, meta_vpn);
    free(ftl_meta);
}

static uint32_t write_hfs_partition(const char *filename, uint32_t first_lpn) {
    FILE *hfs_file = fopen(filename, "rb");
    if (!hfs_file) {
        perror(filename);
        exit(1);
    }
    fseek(hfs_file, 0L, SEEK_END);
    long partition_size = ftell(hfs_file);
    fseek(hfs_file, 0L, SEEK_SET);

    uint32_t pages = (partition_size + BYTES_PER_PAGE - 1) / BYTES_PER_PAGE;
    printf("Writing HFS partition using %u pages...\n", pages);

    uint8_t *page = xcalloc(BYTES_PER_PAGE);
    for (uint32_t i = 0; i < pages; i++) {
        memset(page, 0, BYTES_PER_PAGE);
        if (fread(page, 1, BYTES_PER_PAGE, hfs_file) == 0 && ferror(hfs_file)) {
            perror(filename);
            exit(1);
        }
        write_data_page(page, first_lpn + i);
    }
    free(page);
    fclose(hfs_file);

    return pages;
}

static void write_gpt(const gpt_ent *entry, uint64_t hdr_lba, uint64_t alt_lba, uint64_t table_lba)
{
    uint8_t *table_page = xcalloc(BYTES_PER_PAGE);
    memcpy(table_page, entry, sizeof(gpt_ent));
    write_data_page(table_page, table_lba);

    uint8_t *hdr_page = xcalloc(BYTES_PER_PAGE);
    gpt_hdr *hdr = (gpt_hdr *)hdr_page;
    memcpy(hdr->hdr_sig, GPT_HDR_SIG, 8);
    hdr->hdr_revision = GPT_HDR_REVISION;
    hdr->hdr_size = 0x5C;
    hdr->hdr_lba_self = hdr_lba;
    hdr->hdr_lba_alt = alt_lba;
    hdr->hdr_lba_start = BOOT_PARTITION_FIRST_PAGE;
    hdr->hdr_lba_end = DISK_NUM_OF_LBAS - 3;
    memcpy(hdr->hdr_uuid, "\x6a\x2e\x5c\x10\x9f\x3b\x4e\x41\x8d\x27\x1c\x44\xa5\x10\xe7\x01", 16);
    hdr->hdr_lba_table = table_lba;
    hdr->hdr_entries = NUM_PARTITIONS;
    hdr->hdr_entsz = sizeof(gpt_ent);
    hdr->hdr_crc_table = crc32((const uint8_t *)entry, sizeof(gpt_ent) * NUM_PARTITIONS);
    hdr->hdr_crc_self = crc32((uint8_t *)hdr, hdr->hdr_size);
    write_data_page(hdr_page, hdr_lba);

    free(table_page);
    free(hdr_page);
}

static void write_filesystem(const char *fs_image) {
    uint32_t fs_pages = write_hfs_partition(fs_image, BOOT_PARTITION_FIRST_PAGE);

    gpt_ent entry;
    memset(&entry, 0, sizeof(entry));
    entry.ent_type[0] = 0x48465300;  // Apple HFS+ (48465300-0000-11AA-AA11-00306543ECAC)
    entry.ent_type[1] = 0x11AA0000;
    entry.ent_type[2] = 0x300011AA;
    entry.ent_type[3] = 0xACEC4365;
    memcpy(entry.ent_uuid, "\x3c\x1f\x8e\x52\x06\x7d\x4b\x0a\x9b\x61\x2f\x0e\x88\x14\xc3\x5d", 16);
    entry.ent_lba_start = BOOT_PARTITION_FIRST_PAGE;
    entry.ent_lba_end = BOOT_PARTITION_FIRST_PAGE + fs_pages - 1;
    const char *name = "System";
    for (int i = 0; name[i]; i++) {
        entry.ent_name[i] = name[i];
    }
    printf("Boot system partition located on page %llu - %llu\n", entry.ent_lba_start, entry.ent_lba_end);

    // the primary GPT at the start of the disk and the backup GPT at the end
    write_gpt(&entry, 1, DISK_NUM_OF_LBAS - 1, 2);
    write_gpt(&entry, DISK_NUM_OF_LBAS - 1, 1, DISK_NUM_OF_LBAS - 2);

    // The MBR. Its type makes the kernel use the GPT, but iBoot ignores the GPT and mounts the first MBR partition, so
    // unlike a regular protective MBR the partition covers the system partition only.
    uint8_t *mbr_page = xcalloc(BYTES_PER_PAGE);
    struct mbr_partition *protective = (struct mbr_partition *)(mbr_page + MBR_ADDRESS);
    protective->sysid = 0xEE;
    protective->startlba = BOOT_PARTITION_FIRST_PAGE;
    protective->size = fs_pages;
    mbr_page[510] = 0x55;
    mbr_page[511] = 0xAA;
    write_data_page(mbr_page, 0);
    free(mbr_page);

    // When the FTL rebuilds its tables it identifies a data block by the meta data of its last page, so fill up the
    // last logical block of the filesystem. The last logical block of the disk already ends with the backup GPT header.
    uint32_t end_lpn = BOOT_PARTITION_FIRST_PAGE + fs_pages;
    uint32_t pad_end = (end_lpn + PAGES_PER_SUBLOCK - 1) / PAGES_PER_SUBLOCK * PAGES_PER_SUBLOCK;
    for (uint32_t lpn = end_lpn; lpn < pad_end; lpn++) {
        write_data_page(NULL, lpn);
    }
}

// Writes a whole disk image (logical page n at offset n * BYTES_PER_PAGE), e.g. one that nand_disk.py extracted from a
// NAND. Only the logical blocks that hold data are written, each of them completely, since the FTL identifies a data
// block by the meta data of its last page when it rebuilds its tables. The holes of a sparse image are skipped.
static void write_disk(const char *disk_image) {
    int fd = open(disk_image, O_RDONLY);
    if (fd == -1) {
        perror(disk_image);
        exit(1);
    }
    off_t size = lseek(fd, 0, SEEK_END);
    if (size > (off_t)DISK_NUM_OF_LBAS * BYTES_PER_PAGE) {
        fprintf(stderr, "%s is larger than the disk (%llu pages)\n", disk_image, DISK_NUM_OF_LBAS);
        exit(1);
    }

    off_t block_size = (off_t)PAGES_PER_SUBLOCK * BYTES_PER_PAGE;
    uint8_t *page = xcalloc(BYTES_PER_PAGE);
    uint32_t blocks = 0;
    off_t pos = 0;
    while (pos < size) {
        off_t data = lseek(fd, pos, SEEK_DATA);
        if (data == -1) {
            break;  // only a hole remains
        }
        uint32_t lbn = data / block_size;
        for (uint32_t i = 0; i < PAGES_PER_SUBLOCK; i++) {
            uint32_t lpn = lbn * PAGES_PER_SUBLOCK + i;
            memset(page, 0, BYTES_PER_PAGE);
            if (pread(fd, page, BYTES_PER_PAGE, (off_t)lpn * BYTES_PER_PAGE) == -1) {
                perror(disk_image);
                exit(1);
            }
            write_data_page(page, lpn);
        }
        blocks++;
        pos = (off_t)(lbn + 1) * block_size;
    }
    printf("Wrote %u logical blocks of %s\n", blocks, disk_image);
    free(page);
    close(fd);
}

int main(int argc, char *argv[]) {
    const char *fs_image = "filesystem-it2g-readonly.img";
    const char *disk_image = NULL;
    int arg = 1;
    if (argc > 1 && strcmp(argv[1], "--disk") == 0) {
        disk_image = argc > 2 ? argv[2] : NULL;
        arg = 3;
    }
    if (argc > arg + 2 || (arg == 3 && !disk_image)) {
        fprintf(stderr, "usage: %s [filesystem image | --disk <disk image>] [output directory]\n", argv[0]);
        return 1;
    }
    if (argc > arg && !disk_image) {
        fs_image = argv[arg++];
    }
    if (argc > arg) {
        out_dir = argv[arg];
    }

    struct stat st;
    if (stat(out_dir, &st) == 0) {
        fprintf(stderr, "Output directory %s already exists, remove it first\n", out_dir);
        return 1;
    }
    if (mkdir(out_dir, 0700) == -1) {
        perror(out_dir);
        return 1;
    }

    printf("VFL: %d super blocks, %d reserved blocks per bank. FTL: %d logical blocks (%llu pages)\n",
           VFL_NUM_OF_SUBLKS, VFL_RESERVED_BLOCKS_PER_BANK, FTL_NUM_OF_LBNS, DISK_NUM_OF_LBAS);

    write_vfl_context();
    write_ftl_context();
    write_nand_sig_page();
    write_bbts();
    if (disk_image) {
        write_disk(disk_image);
    } else {
        write_filesystem(fs_image);
    }
    return 0;
}
