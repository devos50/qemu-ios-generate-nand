#ifndef VFL_H
#define VFL_H

#include "ftl.h"

#define VFL_CTX_SPARE_TYPE 0x80
#define VFL_INFO_SECTION_SIZE 4
#define VFL_BAD_MARK_INFO_TABLE_SIZE (WMR_MAX_VB / 8 / BAD_MARK_COMPRESS_SIZE)
#define VFL_NUM_OF_VFL_CXT_COPIES 8

#define VFL_AREA_START 0x0
#define VFL_FIRST_BLK_TO_SEARCH_CXT     VFL_AREA_START
#define VFL_LAST_BLK_TO_SEARCH_CXT      (VFL_AREA_START + 199)

#define AND_MAX_BANKS_PER_CS    (8)
#define VFL_SCRUB_LIST_SIZE     (20)
#define FTL_FORMAT_STRUCT_SIZE  (16)        // packed

#define BAD_MARK_COMPRESS_SIZE 8
#define VFL_BAD_MAP_TABLE_AVAILABLE_MARK 0xFFF0;

#define VFL_META_VERSION 0x00000002
#define VFL_VENDOR_SPECIFIC_TYPE 0x100014

typedef struct
{
    uint32_t dwGlobalCxtAge;             /* age for FTL meta information search  */
    uint32_t dwCxtAge;                   /* context age 0xFFFFFFFF --> 0x0 */
    uint32_t dwFTLType;                  /* FTL identifier */

    uint16_t wCxtLocation;               /* current context block location  (Physical) */
    uint16_t wNextCxtPOffset;            /* current context page offset information */

    /* this data is used for summary */
    uint16_t wNumOfInitBadBlk;           /* the number of initial bad blocks - used for VFL format */
    uint16_t wNumOfWriteFail;         /* the number of failed write operations*/
    uint16_t wNumOfEraseFail;            /* the number of failed erase operations */

    /* bad blocks management table & good block pointer */
    uint16_t awReplacementIdx[AND_MAX_BANKS_PER_CS];           /* index to the last bad block updated in the awBadMapTable per bank
                                                                    (some might be bad blocks) the array is divided to banks */
    uint16_t awBadMapTable[WMR_MAX_RESERVED_SIZE]; /* remapping table of bad blocks - the UInt 16 value set here is the virtual block (vfl address space) that is being replaced */

    /* bad blocks management table within VFL info area */
    uint16_t awInfoBlk[VFL_INFO_SECTION_SIZE];       /* physical block addresses where FTL Cxt information is stored */
    
    uint16_t wNumOfFTLSuBlk;             /* the number of super blocks allocated for the FTL - default id to give all to FTL */
    uint16_t wNumOfVFLSuBlk;             /* the total number of available super blocks */
    uint16_t awFTLCxtVbn[FTL_CXT_SECTION_SIZE];  /* page address (FTL virtual addressing space) of FTL Cxt */
    uint16_t wScrubIdx;                              /* Index for the scrub list */
    uint16_t awScrubList[VFL_SCRUB_LIST_SIZE];       /* scrub list */
    uint8_t abFTLFormat[FTL_FORMAT_STRUCT_SIZE];
    uint32_t abVSFormtType;
} __attribute__((packed)) VFLCxt;

typedef struct
{   
    VFLCxt stVFLCxt;
    uint8_t abReserved[2048 - ((3 * sizeof(uint32_t)) + sizeof(VFLCxt))];
    uint32_t dwVersion;
    uint32_t dwCheckSum;                 
    uint32_t dwXorSum;
} VFLMeta;

typedef struct
{
    uint32_t dwCxtAge;               /* context age 0xFFFFFFFF --> 0x0   */
    uint32_t dwReserved;             /* reserved                                       */
    uint8_t cStatusMark;            /* status (confirm) mark - currently not used for anything */
    uint8_t bSpareType;             /* spare type */
    /* reserved for main ECC */
} VFLSpare;

#endif