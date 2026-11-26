/* Generated from layout/persisted_layout.json by tools/generate_layout.py.
 * Do not edit by hand: change the record and regenerate. */
#ifndef PERSISTED_LAYOUT_H
#define PERSISTED_LAYOUT_H

#define SF_EEPROM_SIZE                       0x100 /* EEPROM size */
#define SF_EEPROM_INIT_ADDR                  0x00 /* EEPROM init address */
#define SF_EEPROM_INIT_VALUE                 0x5D /* EEPROM init value */
#define SF_EEPROM_HOME_OFFSET_ADDR           0x01 /* home offset */
#define SF_EEPROM_TOTAL_STEPS_ADDR           0x03 /* steps per revolution */
#define SF_EEPROM_MODULE_ID_ADDR             0x05 /* module id */
#define SF_EEPROM_AUTO_HOME_ADDR             0x06 /* auto home */
#define SF_EEPROM_MAP_START_ADDR             0x0C /* position map */
#define SF_EEPROM_MAP_CHARS_ADDR             0x8C /* drum characters */
#define SF_EEPROM_MAP_FORMAT_ADDR            0xCC /* map format tag */
#define SF_EEPROM_MAP_FORMAT_VALUE           0x02 /* map format value */
#define SF_EEPROM_FW_VERSION_ADDR            0xCD /* firmware version */
#define SF_FW_VERSION_SLOT                   0x08 /* firmware version slot */
#define SF_DEBUG_MARKERS_START               0xF4 /* debug markers first */
#define SF_DEBUG_MARKERS_END                 0xFE /* debug markers last */
#define SF_EEPROM_BOOT_FLAG_ADDR             0xFF /* boot flag */
#define SF_BOOT_FLAG_VALUE                   0xB0 /* boot flag armed value */
#define SF_BOOT_FLAG_CLEAR_VALUE             0x00 /* boot flag cleared value */
#define SF_BOOTLOADER_PAGES                  0x08 /* bootloader section pages */
#define SF_BOOTLOADER_SIZE                   0x800 /* bootloader section bytes */
#define SF_APP_START_BYTE                    0x800 /* application start byte */
#define SF_FLASH_PAGE_SIZE                   0x40 /* flash page size */
#define SF_FLASH_TOTAL_SIZE                  0x4000 /* flash total size */
#define SF_FLASH_END                         0x4000 /* flash end */
#define SF_APP_MAX_SIZE                      0x3800 /* maximum application size */
#define SF_APP_MAX_PAGES                     0xE0 /* maximum application pages */
#define SF_FW_BUS_COMPAT                     1 /* bus compatibility generation */
#define SF_BOOTLOADER_VERSION                1 /* bootloader version */

#endif /* PERSISTED_LAYOUT_H */
