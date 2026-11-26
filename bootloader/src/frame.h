/**
 * @file frame.h
 * @brief OTA frame decoder and update-session module for the bootloader.
 *
 * One deep module owns framing, escape decoding, incremental CRC and session
 * policy. The UART, flash, EEPROM, response and reset are adapters supplied by
 * bootloader.c, so host tests exercise the shipped logic through this same
 * interface.
 */
#ifndef OTA_FRAME_H
#define OTA_FRAME_H

#include <stdint.h>
#include "protocol.h"

typedef struct {
    uint8_t addr;
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[OTA_MAX_PAYLOAD];
} ota_frame_t;

typedef struct {
    uint8_t staying;       /* boot flag set / update session active */
    uint16_t size;         /* image size from START_UPDATE */
    uint8_t crc[4];        /* image CRC32 from START_UPDATE, big-endian */
} ota_session_t;

/* Returns the next wire byte, or -1 on timeout. */
typedef int (*ota_fetch_fn)(void);

/* Hardware operations behind the seam. */
typedef struct {
    void (*send_response)(uint8_t cmd, const uint8_t *payload, uint8_t len);
    void (*write_page)(uint16_t page, const uint8_t *data);
    uint32_t (*app_crc32)(uint16_t size);
    void (*set_boot_flag)(uint8_t value);
    void (*reset)(void);
} ota_session_ports_t;

/* The sync byte is consumed by the caller; this reads everything after it. */
uint8_t ota_frame_receive(ota_fetch_fn fetch, ota_frame_t *out);

void ota_session_init(ota_session_t *session, uint8_t staying);
uint8_t ota_session_staying(const ota_session_t *session);

/* Returns 1 when the frame was addressed to module_id (boot window refresh). */
uint8_t ota_session_handle(ota_session_t *session, const ota_frame_t *frame,
                           uint8_t module_id,
                           const ota_session_ports_t *ports);

#endif /* OTA_FRAME_H */
