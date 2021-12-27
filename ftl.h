#ifndef FTL_H
#define FTL_H

#include "wmr.h"

#define FTL_CXT_SECTION_SIZE 3
#define FREE_SECTION_SIZE 20
#define FREE_LIST_SIZE 3
#define FTL_READ_REFRESH_LIST_SIZE 5
#define BYTES_PER_SECTOR 512
#define LOG_SECTION_SIZE (FREE_SECTION_SIZE - FREE_LIST_SIZE)
#define MAX_NUM_OF_MAP_TABLES  (WMR_MAX_VB / (WMR_SECTORS_PER_PAGE_MIN * WMR_SECTOR_SIZE) + (WMR_MAX_VB % (WMR_SECTORS_PER_PAGE_MIN * WMR_SECTOR_SIZE) ? 1 : 0)) * (sizeof(uint16_t))
#define MAX_NUM_OF_EC_TABLES   (WMR_MAX_VB / (WMR_SECTORS_PER_PAGE_MIN * WMR_SECTOR_SIZE) + (WMR_MAX_VB % (WMR_SECTORS_PER_PAGE_MIN * WMR_SECTOR_SIZE) ? 1 : 0)) * (sizeof(uint32_t))
#define MAX_NUM_OF_LOGCXT_MAPS (((LOG_SECTION_SIZE * WMR_MAX_PAGES_PER_BLOCK * WMR_NUM_OF_BANKS * sizeof(uint16_t)) / (WMR_SECTOR_SIZE * WMR_SECTORS_PER_PAGE_MIN)) + (((LOG_SECTION_SIZE * WMR_MAX_PAGES_PER_BLOCK * WMR_NUM_OF_BANKS * sizeof(uint16_t)) % (WMR_SECTOR_SIZE * WMR_SECTORS_PER_PAGE_MIN)) ? 1 : 0))

typedef struct
{
    uint32_t dwLogAge;          /* log age - uses a global counter starting from 0 */
    uint16_t wVbn;              /* the virtual block number of log block */
    uint16_t wLbn;              /* the logical block number of log block */
    uint16_t      *paPOffsetL2P; /* L2P page offset mapping table          *//* size - PAGES_PER_SUBLK * sizeof(UInt16) functionality - maps pages of a logical block inside the block */
    uint16_t wFreePOffset; /* free page offset in log block                *//* the next physical page to write to */
    uint16_t wNumOfValidLP; /* the number of valid page                            *//* the number of valid pages in the log - stay the same when rewriting a page */
    uint32_t boolCopyMerge; /* can be copymerged or not                            *//* ??? */
    /* name sure this structure is aligned 16-bit */
} LOGCxt;

typedef struct
{
    uint16_t wLbn;
    uint16_t wVbn;
} FTLReadRefreshStruct;

typedef struct
{
    uint32_t dwAge;         /* Cxt Write Age - Counter starting From 0xFFFFFFFF going down */

    uint32_t dwWriteAge;    /* counter starting from 0 - unique age of the first ftl write operation to the block */

    uint16_t wNumOfFreeVb;  /* number of free VB */
    uint16_t wFreeVbListTail; /* marks the first available VB in a cyclic list */
    uint16_t wWearLevelCounter; /* this counter is by the FTL to decide whether an Autowearlevel operation is needed after FTL_Write is done */

    uint16_t awFreeVbList[FREE_SECTION_SIZE]; /* cyclic list of the free VBs */

    uint32_t adwMapTablePtrs[MAX_NUM_OF_MAP_TABLES]; /* page address (Vpn) of the Map table pages in the Cxt block - only valid when boolFlashCxtIsValid is TRUE32 */
    uint32_t adwECTablePtrs[MAX_NUM_OF_EC_TABLES]; /* page address of the Erase Counter table */
    uint32_t adwLOGCxtMapPtrs[MAX_NUM_OF_LOGCXT_MAPS]; /* page address of the LOGCxt map table */

    uint16_t *     pawMapTable; /* cached map table logical blocks to virtual blocks - value will be recalculated in FTL_Init */
    uint16_t *     pawECCacheTable;  /* cached Erase counter table - value will be recalculated in FTL_Init - data is recovered from the last valid Erase Map in case of power failure */
    uint16_t *     pawLOGCxtMapTable;  /* pointer to the map of the logs - this is a pointer and will be calculated in the Init - data can be restored if the info is flushed */
    LOGCxt aLOGCxtTable[LOG_SECTION_SIZE + 1]; /* LOGCxt array */

    uint32_t dwMetaWearLevelCounter; /* used to decide whether the index info super blocks need to be replaced or not (slows down VFL Cxt blocks wear) */
    uint16_t wOverErasedBlock; /* should not be used - mark the last erased vb before setting boolFlushECTable */

    uint16_t awMapCxtVbn[FTL_CXT_SECTION_SIZE]; /* vb address of the FTL Cxt information */
    uint32_t dwCurrMapCxtPage; /* pointer to the last written Cxt info page */

    uint32_t boolFlashCxtIsValid; /* mark whether the FTLCxt on the flash is up to date or not */

    // this code was added Apr 4th 2007
    uint32_t adwRCTablePtrs[MAX_NUM_OF_EC_TABLES]; /* page address of the Read Counter table */
    uint16_t *     pawRCCacheTable;  /* cached Erase counter table - value will be recalculated in FTL_Init - data is recovered from the last valid Erase Map in case of power failure */
    FTLReadRefreshStruct aFTLReadRefreshList[FTL_READ_REFRESH_LIST_SIZE];
    uint32_t dwRefreshListIdx;
    uint32_t dwReadsSinceLastRefreshMark;
    // this code was added Apr 20th 2007
    uint32_t adwStatPtrs[2]; // allocate 2 pages for stats
} FTLCxt2;

typedef struct
{
    FTLCxt2 stFTLCxt;
    uint8_t abReserved[((BYTES_PER_SECTOR - 2) * sizeof(uint32_t)) - sizeof(FTLCxt2)];
    uint32_t dwVersion;
    uint32_t dwVersionNot;
} FTLMeta;

#endif