#ifndef MBR_H
#define MBR_H

#define MBR_ADDRESS     0x1be

struct mbr_partition {
    uint8_t bootid;
    uint8_t starthead;
    uint8_t startsect;
    uint8_t startcyl;
    uint8_t sysid;
    uint8_t endhead;
    uint8_t endsect;
    uint8_t endcyl;
    uint32_t startlba;
    uint32_t size;
} __packed;

#endif