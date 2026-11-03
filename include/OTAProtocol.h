/**
 * @file OTAProtocol.h
 * @brief Shared protocol definitions for RS485 OTA firmware update.
 *
 * Used by both the bootloader (bare C) and the application (Arduino).
 * Keep in sync with bootloader/src/protocol.h
 */
#pragma once

#include <stdint.h>

/* ── Packet framing ────────────────────────────────────────────────── */
#define OTA_SYNC            0xAA    /* Start-of-frame marker          */
#define OTA_BROADCAST       0xFF    /* Broadcast address (all nodes)  */

/* ── Commands  (Master → Slave) ────────────────────────────────────── */
#define CMD_PING            0x01    /* Are you in bootloader mode?    */
#define CMD_START_UPDATE    0x02    /* Begin update session           */
#define CMD_WRITE_PAGE      0x03    /* Write one 64-byte flash page   */
#define CMD_FINISH          0x04    /* End update, verify & reboot    */
#define CMD_RESET           0x05    /* Force software reset           */

/* ── Responses (Slave → Master) ────────────────────────────────────── */
#define RESP_ACK            0x81    /* Success                        */
#define RESP_NACK           0x82    /* Failure (see error code)       */

/* ── NACK error codes ──────────────────────────────────────────────── */
#define ERR_CRC             0x01    /* Packet CRC mismatch            */
#define ERR_ADDRESS         0x02    /* Flash address out of range     */
#define ERR_SEQUENCE        0x03    /* Unexpected page sequence       */
#define ERR_VERIFY          0x04    /* Final firmware CRC mismatch    */

/* ── EEPROM addresses shared between app and bootloader ────────────── */
#define OTA_EEPROM_BOOT_FLAG_ADDR   255     /* Last EEPROM byte       */
#define OTA_EEPROM_MODULE_ID_ADDR   5       /* Same as app's ADDR_MODULE_ID */
#define OTA_BOOT_FLAG_VALUE         0xB0    /* Magic value = enter bootloader */

/* ── Flash layout ──────────────────────────────────────────────────── */
#define BOOTLOADER_PAGES    4       /* BOOTEND fuse value (×256 bytes)*/
#define BOOTLOADER_SIZE     (BOOTLOADER_PAGES * 256)   /* 1024 bytes  */
#define APP_START_BYTE      0x0400  /* Application starts here        */
#define APP_START_WORD      (APP_START_BYTE / 2)       /* For func ptr*/
#define FLASH_PAGE_SIZE     64      /* ATtiny1616 page size           */
#define FLASH_TOTAL_SIZE    16384   /* 16 KB                          */
#define APP_MAX_SIZE        (FLASH_TOTAL_SIZE - BOOTLOADER_SIZE) /* 15360 */
#define APP_MAX_PAGES       (APP_MAX_SIZE / FLASH_PAGE_SIZE)     /* 240  */

/* ── Protocol constants ────────────────────────────────────────────── */
#define BOOTLOADER_VERSION  0x01
#define OTA_BAUD_RATE       9600    /* Bit-bang UART speed in bootloader */
#define BOOT_WAIT_MS        500     /* How long bootloader waits for PING */
#define OTA_MAX_RETRIES     3       /* Master retries per page         */

