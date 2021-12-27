#ifndef VFL_H
#define VFL_H

#include "ftl.h"

#define VFL_CTX_SPARE_TYPE 0x80
#define VFL_INFO_SECTION_SIZE 4
#define VFL_BAD_MARK_INFO_TABLE_SIZE (WMR_MAX_VB / 8 / BAD_MARK_COMPRESS_SIZE)

#define BAD_MARK_COMPRESS_SIZE 8

typedef struct
{
    uint32_t dwGlobalCxtAge;                     /* age for FTL meta information search  */
    uint16_t aFTLCxtVbn[FTL_CXT_SECTION_SIZE];   /* page address (FTL virtual addressing space) of FTL Cxt */
    uint16_t wPadding;                           /* padding (align to UInt32) */

    uint32_t dwCxtAge;                      /* context age 0xFFFFFFFF --> 0x0 - NirW - check if the variable is used anywhere - what is the difference between dwGlobalCxtAge and dwCxtAge */
    uint16_t wCxtLocation;                  /* current context block location  (Physical) */
    uint16_t wNextCxtPOffset;               /* current context page offset information */

    /* this data is used for summary                                    */
    uint16_t wNumOfInitBadBlk;       /* the number of initial bad blocks - used for VFL format */
    uint16_t wNumOfWriteFail;        /* the number of write fail - currently not used */
    uint16_t wNumOfEraseFail;        /* the number of erase fail - updated (++) every time there is an erase failure that causes remapping of a block */

    /* bad blocks management table & good block pointer */
    uint16_t wBadMapTableMaxIdx;     /* index to the last bad block updated in the aBadMapTable */
    uint16_t wReservedSecStart;      /* index of the first physical block that will be used as reserved in the bank (the first block after VFL Cxt Info) */
    uint16_t wReservedSecSize;       /* number of physical blocks that are available as reserved (some might be bad blocks) */
    uint16_t aBadMapTable[WMR_MAX_RESERVED_SIZE]; /* remapping table of bad blocks - the UInt 16 value set here is the virtual block (vfl address space) that is being replaced */
    uint8_t aBadMark[VFL_BAD_MARK_INFO_TABLE_SIZE]; /* compact bitmap presentation of bad block (initial and accumulated) */

    /* bad blocks management table within VFL info area */
    uint16_t awInfoBlk[VFL_INFO_SECTION_SIZE];      /* physical block addresses where Cxt information is stored */
    uint16_t wBadMapTableScrubIdx;                  /* Index for the scrub list (start from the top of aBadMapTable */
} VFLCxt;

typedef struct
{
    uint32_t dwCxtAge;               /* context age 0xFFFFFFFF --> 0x0   */
    uint32_t dwReserved;             /* reserved                                       */
    uint8_t cStatusMark;            /* status (confirm) mark - currently not used for anything */
    uint8_t bSpareType;             /* spare type */
    uint8_t eccMarker;              /* custom */
    /* reserved for main ECC */
} VFLSpare;

#endif