/*
 * Host-side test of the bootloader's pure protocol helpers.
 *
 * Compiles the exact codec.h used by the ATtiny bootloader on the PC and
 * checks it against known CRC vectors and a frame encode/decode round trip.
 *
 *   cc -O2 -Wall -Werror -o test_codec test_codec.c && ./test_codec
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/codec.h"

/* CRC-16/CCITT-FALSE check value for "123456789" is 0x29B1. */
static void test_crc16(void)
{
    assert(ota_crc16((const uint8_t *)"123456789", 9) == 0x29B1);
    /* Empty input returns the init value. */
    assert(ota_crc16((const uint8_t *)"", 0) == 0xFFFF);
    /* Incremental and one-shot forms agree. */
    const uint8_t data[5] = {0xAA, 0x03, 0x42, 0x11, 0x22};
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < 5; i++)
        crc = ota_crc16_byte(crc, data[i]);
    assert(crc == ota_crc16(data, 5));
    printf("crc16 ok (0x%04X)\n", crc);
}

/* CRC-32 (zlib-compatible) check value for "123456789" is 0xCBF43926. */
static void test_crc32(void)
{
    assert(ota_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926UL);
    assert(ota_crc32((const uint8_t *)"", 0) == 0x00000000UL);
    /* Incremental agreement, seed form used by the bootloader. */
    const uint8_t data[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x5C, 0x6D, 0x41};
    uint32_t crc = 0xFFFFFFFFUL;
    for (int i = 0; i < 8; i++)
        crc = ota_crc32_byte(crc, data[i]);
    assert((crc ^ 0xFFFFFFFFUL) == ota_crc32(data, 8));
    printf("crc32 ok (0x%08X)\n", (unsigned)ota_crc32(data, 8));
}

/*
 * Escape encoding: 0x5C and 0x6D must never appear raw in the wire stream,
 * and decoding must restore the original bytes exactly.
 */
static void test_escape(void)
{
    uint8_t src[300], dst[600], back[300];
    for (unsigned i = 0; i < sizeof(src); i++)
        src[i] = (uint8_t)(i * 7 + 0x5C);  /* plenty of 0x5C/0x6D values */

    uint16_t n = ota_escape_encode(src, sizeof(src), dst);
    assert(n >= sizeof(src));
    for (uint16_t i = 0; i < n; i++)
        assert(dst[i] != 0x6D);

    /* Feed dst through ota_escape_decode via a cursor "fetch". */
    uint16_t pos = 0;
    int fetch(void) { return pos < n ? dst[pos++] : -1; }
    for (unsigned i = 0; i < sizeof(src); i++) {
        int dummy;
        int b = ota_escape_decode(fetch, &dummy);
        assert(b == (int)src[i]);
        back[i] = (uint8_t)b;
    }
    assert(memcmp(src, back, sizeof(src)) == 0);
    printf("escape ok (%u -> %u bytes)\n", (unsigned)sizeof(src), (unsigned)n);
}

/*
 * Whole-frame round trip the way the master and the bootloader talk:
 * logical frame -> escape on the wire -> decode -> CRC must match.
 */
static void test_frame_roundtrip(void)
{
    uint8_t payload[66];
    for (unsigned i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(0x6D - i);  /* includes 0x6D and 0x5C */

    uint8_t logical[3 + 66 + 2];
    logical[0] = 3;   /* addr */
    logical[1] = 0x03; /* CMD_WRITE_PAGE */
    logical[2] = 66;
    memcpy(logical + 3, payload, 66);
    uint16_t crc = ota_crc16(logical, 69);
    logical[69] = crc >> 8;
    logical[70] = (uint8_t)crc;

    uint8_t wire[256];
    uint16_t n = ota_escape_encode(logical, 71, wire);
    for (uint16_t i = 0; i < n; i++)
        assert(wire[i] != 0x6D);

    uint16_t pos = 0;
    int fetch(void) { return pos < n ? wire[pos++] : -1; }
    uint8_t parsed[71];
    for (unsigned i = 0; i < 71; i++) {
        int dummy;
        parsed[i] = (uint8_t)ota_escape_decode(fetch, &dummy);
    }
    assert(memcmp(parsed, logical, 71) == 0);
    assert(ota_crc16(parsed, 69) == crc);
    printf("frame round trip ok (%u wire bytes, crc 0x%04X)\n",
           (unsigned)n, crc);
}

int main(void)
{
    test_crc16();
    test_crc32();
    test_escape();
    test_frame_roundtrip();
    printf("all codec tests passed\n");
    return 0;
}
