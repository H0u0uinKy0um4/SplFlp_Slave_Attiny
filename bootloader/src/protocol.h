/**
 * @file protocol.h
 * @brief RS485 OTA protocol definitions for the ATtiny1616 bootloader.
 *
 * Bare-C mirror of the application's include/OTAProtocol.h.  The two files
 * must stay in sync; change both or neither.
 *
 * Frame layout (master -> slave and slave -> master):
 *
 *   [0xAA][addr][cmd][len][payload ... ][crc_hi][crc_lo]
 *
 * Every byte after the sync marker is ESCAPE ENCODED on the wire:
 *
 *   0x5C ("\\") -> 0x5C 0x00
 *   0x6D ("m")  -> 0x5C 0x01
 *
 * so a raw 0x6D never appears inside an OTA frame.  The application's line
 * parser on non-target modules only starts a command line at the byte 'm'
 * (see RS485Handler.cpp loadCommand); escaping therefore guarantees that
 * bystander modules can never execute a command from firmware image bytes
 * streaming past them on the shared bus.
 *
 * CRC-16 covers addr, cmd, len and payload (CRC-16/CCITT-FALSE:
 * polynomial 0x1021, init 0xFFFF, no reflection, no final XOR) and is
 * transmitted big-endian.
 *
 * All multi-byte payload fields are big-endian.
 */
#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include <stdint.h>

/* -- Packet framing --------------------------------------------------- */
#define OTA_SYNC            0xAA    /* Start-of-frame marker           */
#define OTA_BROADCAST       0xFF    /* Broadcast address (reserved; the
                                       bootloader only accepts unicast) */

/* -- Commands (master -> slave) ---------------------------------------- */
#define CMD_PING            0x01    /* Are you in bootloader mode?    */
#define CMD_START_UPDATE    0x02    /* Begin update session            */
#define CMD_WRITE_PAGE      0x03    /* Write one 64-byte flash page    */
#define CMD_FINISH          0x04    /* End update, verify & reboot     */
#define CMD_RESET           0x05    /* Force software reset            */

/* -- Responses (slave -> master) ---------------------------------------- */
#define RESP_ACK            0x81    /* Success                         */
#define RESP_NACK          0x82    /* Failure (payload[0] = error)    */

/* -- NACK error codes --------------------------------------------------- */
#define ERR_CRC             0x01    /* Packet CRC mismatch             */
#define ERR_ADDRESS         0x02    /* Flash address out of range      */
#define ERR_SEQUENCE        0x03    /* Unexpected page sequence        */
#define ERR_VERIFY          0x04    /* Final firmware CRC mismatch     */
#define ERR_LENGTH          0x05    /* Payload has the wrong length   */

/* -- Payload lengths ----------------------------------------------------- */
#define LEN_PING            0
#define LEN_START_UPDATE    6       /* size:u16, crc32:u32             */
#define LEN_WRITE_PAGE      66      /* page:u16, data[64]              */
#define LEN_FINISH          4       /* crc32:u32                       */
#define LEN_RESET           0

/* -- EEPROM addresses shared between app and bootloader ------------------ */
#define OTA_EEPROM_BOOT_FLAG_ADDR   255     /* Last EEPROM byte        */
#define OTA_EEPROM_MODULE_ID_ADDR   5       /* Same as app's ADDR_MODULE_ID */
#define OTA_BOOT_FLAG_VALUE          0xB0    /* Magic value = enter bootloader */

/* -- Flash layout -------------------------------------------------------- */
#define BOOTLOADER_PAGES    8       /* BOOTEND fuse value (x256 bytes) */
#define BOOTLOADER_SIZE     (BOOTLOADER_PAGES * 256)   /* 2048 bytes  */
#define APP_START_BYTE      0x0800  /* Application starts here        */
#define APP_START_WORD      (APP_START_BYTE / 2)       /* For asm jmp */
#define FLASH_PAGE_SIZE     64      /* ATtiny1616 page size           */
#define FLASH_TOTAL_SIZE    16384   /* 16 KB                          */
#define APP_MAX_SIZE        (FLASH_TOTAL_SIZE - BOOTLOADER_SIZE) /* 14336 */
#define APP_MAX_PAGES       (APP_MAX_SIZE / FLASH_PAGE_SIZE)     /* 224  */

/* -- Protocol constants -------------------------------------------------- */
#define BOOTLOADER_VERSION  0x01
#define OTA_BAUD_RATE       9600    /* Bit-bang UART speed in bootloader */
#define BOOT_WAIT_MS        500     /* How long bootloader waits for PING */
#define OTA_MAX_RETRIES     3       /* Master retries per page         */

/* -- Frame limits -------------------------------------------------------- */
#define OTA_MAX_PAYLOAD     66      /* Largest request payload (WRITE_PAGE) */
#define OTA_RESP_MAX_PAYLOAD 3      /* Largest response payload (PING info) */
#define OTA_FRAME_MAX       (4 + OTA_MAX_PAYLOAD + 2)

/* -- Wire escaping ------------------------------------------------------- */
#define OTA_ESC             0x5C    /* Escape marker                   */
#define OTA_ESC_ESC         0x00    /* 0x5C 0x00 encodes 0x5C          */
#define OTA_ESC_M           0x01    /* 0x5C 0x01 encodes 0x6D          */
#define OTA_FORBIDDEN      0x6D    /* 'm': never raw on the wire      */

#endif /* OTA_PROTOCOL_H */
