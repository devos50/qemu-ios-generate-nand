#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "vfl.h"

#define BANKS 1
#define BLOCKS_PER_BANK 4096
#define PAGES_PER_BANK 524288
#define BYTES_PER_PAGE 2048
#define PAGES_PER_BLOCK 128
#define BYTES_PER_SPARE 64
#define FIL_ID 0x43303032
#define FTL_CTX_VBLK_IND 0 // virtual block index of the FTL Context

int main(int argc, char *argv[]) {
	// create the output dir if it does not exist
	struct stat st = {0};

	if (stat("nand", &st) == -1) {
	    mkdir("nand", 0700);
	}

	for(int bank = 0; bank < BANKS; bank++) {
		printf("Writing bank %d...\n", bank);
		uint8_t *bank_buf = (uint8_t *) malloc(PAGES_PER_BANK * BYTES_PER_PAGE);
		uint8_t *bank_spare_buf = (uint8_t *) malloc(PAGES_PER_BANK * BYTES_PER_SPARE);

		// set the FIL ID on the very first byte of the first bank
		((uint32_t *)bank_buf)[0] = FIL_ID;

		// create the bad block table
		uint64_t bbt_offset = PAGES_PER_BANK * BYTES_PER_PAGE - (PAGES_PER_BLOCK * BYTES_PER_PAGE);
		memcpy(bank_buf + bbt_offset, "DEVICEINFOBBT\0\0\0", 16);
		
		// initialize VFL context block on physical block 35, page 0
		VFLCxt *vfl_ctx = (VFLCxt *)(bank_buf + 35 * PAGES_PER_BLOCK * BYTES_PER_PAGE);
		vfl_ctx->awInfoBlk[0] = 35;

		// write the bad mark table
		for(int i = 0; i < VFL_BAD_MARK_INFO_TABLE_SIZE; i++) {
			vfl_ctx->aBadMark[i] = 0xff;
		}

		VFLSpare *vfl_ctx_spare = (VFLSpare *)(bank_spare_buf + 35 * PAGES_PER_BLOCK * BYTES_PER_SPARE);
		vfl_ctx_spare->dwCxtAge = 1;
		vfl_ctx_spare->bSpareType = VFL_CTX_SPARE_TYPE;

		// indicate the location of the FTL CTX block
		vfl_ctx->aFTLCxtVbn[0] = FTL_CTX_VBLK_IND;
		vfl_ctx->aFTLCxtVbn[1] = FTL_CTX_VBLK_IND;
		vfl_ctx->aFTLCxtVbn[2] = FTL_CTX_VBLK_IND;

		// set the FTL spare type of the first page of the FTL CXT block to indicate that there is a CTX index
		vfl_ctx_spare = (VFLSpare *)(bank_spare_buf + (25728 + FTL_CTX_VBLK_IND * PAGES_PER_BLOCK) * BYTES_PER_SPARE);
		vfl_ctx_spare->bSpareType = FTL_SPARE_TYPE_CXT_INDEX;
		vfl_ctx_spare->eccMarker = 0xff;

		// create the FTL Meta page on the last page of the FTL Cxt block and embed the right versions
		vfl_ctx_spare = (VFLSpare *)(bank_spare_buf + (25728 + FTL_CTX_VBLK_IND * PAGES_PER_BLOCK + PAGES_PER_BLOCK - 1) * BYTES_PER_SPARE);
		vfl_ctx_spare->bSpareType = FTL_SPARE_TYPE_CXT_INDEX;

		FTLMeta *ftl_meta = (FTLMeta *)(bank_buf + (25728 + FTL_CTX_VBLK_IND * PAGES_PER_BLOCK + PAGES_PER_BLOCK - 1) * BYTES_PER_PAGE);
		ftl_meta->dwVersion = 0x46560000;
		ftl_meta->dwVersionNot = -0x46560001;

		// prepare the logical block -> virtual block mapping tables
		for(int i = 0; i < MAX_NUM_OF_MAP_TABLES; i++) {
			ftl_meta->stFTLCxt.adwMapTablePtrs[i] = i;
		}

		for(uint16_t block_nr = 0; block_nr < 4096; block_nr++) { // TODO this should be increased!!!!!
			int bytes_offset = 25728 * BYTES_PER_PAGE;
			uint16_t *arr = (uint16_t *)(bank_buf + bytes_offset);
			arr[block_nr] = block_nr + 1;
		}
		
		// write the HFS+ partition to the first page and update the associated spare
		// FILE *hfs_file = fopen("hfs.part", "rb");
		// uint8_t *hfs_buf = (uint8_t *)malloc(BYTES_PER_PAGE);
		// fgets((char *)hfs_buf, BYTES_PER_PAGE, hfs_file);
		// uint8_t *first_page = bank_buf + 25728 * BYTES_PER_PAGE;
		// memcpy(first_page, hfs_buf, BYTES_PER_PAGE);

		VFLSpare *spare = (VFLSpare *)(bank_spare_buf + 25728 * BYTES_PER_SPARE);
		spare->eccMarker = 0xff;

		// write the main storage
		char *filename = malloc(80);
		sprintf(filename, "nand/bank%d", bank);
		FILE *f = fopen(filename, "wb");
		fwrite(bank_buf, sizeof(char), PAGES_PER_BANK * BYTES_PER_PAGE, f);
		fclose(f);

		// write the spare area
		printf("Writing spare of bank %d...\n", bank);
		sprintf(filename, "nand/bank%d_spare", bank);
		f = fopen(filename, "wb");
		fwrite(bank_spare_buf, sizeof(char), PAGES_PER_BANK * BYTES_PER_SPARE, f);
		fclose(f);
	}
}