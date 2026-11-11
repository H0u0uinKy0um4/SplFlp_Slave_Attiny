/**
 * @file codec.h
 * @brief Pure protocol helpers for the OTA bootloader.
 *
 * No hardware access: everything here compiles unchanged on the AVR and on
 * a host PC, which is how bootloader/test/test_codec.c exercises the exact
 * code that runs on the module.
 *
 *  - CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0)
 *    over addr, cmd, len, payload; transmitted big-endian.
 *  - CRC-32 (IEEE 802.3, poly 0xEDB88320 reflected, init/xorout 0xFFFFFFFF),
 *    the same value Python's binascii.crc32 and zlib produce, so the master,
 *    the integration and the tooling all agree.
 */
#ifndef OTA_CODEC_H
#define OTA_CODEC_H

#include <stdint.h>

static inline uint16_t ota_crc16_byte(uint16_t crc, uint8_t data)
{
    crc ^= (uint16_t)data << 8;
    for (uint8_t i = 0; i < 8; i++)
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    return crc;
}

static inline uint16_t ota_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--) crc = ota_crc16_byte(crc, *data++);
    return crc;
}

static inline uint32_t ota_crc32_byte(uint32_t crc, uint8_t data)
{
    crc ^= data;
    for (uint8_t i = 0; i < 8; i++)
        crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320UL : (crc >> 1);
    return crc;
}

static inline uint32_t ota_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    while (len--) crc = ota_crc32_byte(crc, *data++);
    return crc ^ 0xFFFFFFFFUL;
}

/*
 * Escape encoding for the wire.  One logical byte becomes:
 *   0x5C -> 0x5C 0x00
 *   0x6D -> 0x5C 0x01
 *   anything else -> itself
 */
static inline uint8_t ota_escape_needed(uint8_t b)
{
    return (b == 0x5C) || (b == 0x6D);
}

/* Encode src[0..len) into dst; returns the number of wire bytes written.
   dst must have room for 2*len bytes. */
static inline uint16_t ota_escape_encode(const uint8_t *src, uint16_t len,
                                          uint8_t *dst)
{
    uint16_t out = 0;
    while (len--) {
        uint8_t b = *src++;
        if (b == 0x5C) {
            dst[out++] = 0x5C;
            dst[out++] = 0x00;
        } else if (b == 0x6D) {
            dst[out++] = 0x5C;
            dst[out++] = 0x01;
        } else {
            dst[out++] = b;
        }
    }
    return out;
}

/* Decode one escape sequence: *pp points at the next wire byte; on return it
   points past the consumed bytes.  Returns the logical byte, or -1 on an
   illegal sequence (an escape marker followed by an unknown code, or a raw
   0x6D on the wire).  fetch is a function returning the next wire byte or -1
   on timeout; it is only called when a byte is actually needed. */
static inline int ota_escape_decode(int (*fetch)(void), int *consumed)
{
    int b = fetch();
    if (b < 0) return -1;
    (*consumed)++;
    if (b == 0x5C) {
        int c = fetch();
        if (c < 0) return -1;
        (*consumed)++;
        if (c == 0x00) return 0x5C;
        if (c == 0x01) return 0x6D;
        return -1;              /* unknown escape code */
    }
    if (b == 0x6D) return -1;   /* 'm' must never appear raw */
    return b;
}

#endif /* OTA_CODEC_H */
