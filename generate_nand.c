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

int main(int argc, char *argv[]) {
	// create the output dir if it does not exist
	struct stat st = {0};

	if (stat("nand", &st) == -1) {
	    mkdir("nand", 0700);
	}

	printf("%d\n", LOG_SECTION_SIZE);

	for(int bank = 0; bank < BANKS; bank++) {
		printf("Writing bank %d...\n", bank);
		uint8_t *bank_buf = (uint8_t *) malloc(PAGES_PER_BANK * BYTES_PER_PAGE);

		// set the FIL ID
		((uint32_t *)bank_buf)[0] = FIL_ID;

		// create the bad block table
		uint64_t bbt_offset = PAGES_PER_BANK * BYTES_PER_PAGE - (PAGES_PER_BLOCK * BYTES_PER_PAGE);
		memcpy(bank_buf + bbt_offset, "DEVICEINFOBBT\0\0\0", 16);

		// create the spare areas
		uint8_t *bank_spare_buf = (uint8_t *) malloc(PAGES_PER_BANK * BYTES_PER_SPARE);
		
		// initialize VFL context block on block 35, page 0
		VFLCxt *vfl_ctx = (VFLCxt *)(bank_buf + 35 * PAGES_PER_BLOCK * BYTES_PER_PAGE);
		vfl_ctx->awInfoBlk[0] = 35;

		VFLSpare *vfl_ctx_spare = (VFLSpare *)(bank_spare_buf + 35 * PAGES_PER_BLOCK * BYTES_PER_SPARE);
		vfl_ctx_spare->dwCxtAge = 1;
		vfl_ctx_spare->bSpareType = VFL_CTX_SPARE_TYPE;

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