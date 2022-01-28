#ifndef GPT_H
#define GPT_H

typedef struct gpt_hdr
{
    uint8_t  hdr_sig[8];
    uint32_t hdr_revision;
    uint32_t hdr_size;
    uint32_t hdr_crc_self;
    uint32_t __reserved;
    uint64_t hdr_lba_self;
    uint64_t hdr_lba_alt;
    uint64_t hdr_lba_start;
    uint64_t hdr_lba_end;
    uint8_t  hdr_uuid[16];
    uint64_t hdr_lba_table;
    uint32_t hdr_entries;    // the number of partition entries
    uint32_t hdr_entsz;      // size of each partition entry
    uint32_t hdr_crc_table;
    uint32_t padding;
} gpt_hdr;

typedef struct gpt_ent
{
    uint32_t   ent_type[4];
    uint8_t   ent_uuid[16];
    uint64_t ent_lba_start;
    uint64_t ent_lba_end;
    uint64_t ent_attr;
    uint16_t ent_name[36];
} gpt_ent;

#define GPT_HDR_SIG "EFI PART"
#define GPT_HDR_REVISION 0x00010000

#endif