/**
 * @file bootloader.c
 * @brief RS485 OTA bootloader for the split-flap ATtiny1616 slave module.
 *
 * Lives in the boot section at flash 0x0000 (BOOTEND fuse = 8, i.e. a 2 KB
 * boot section in 256-byte fuse units; see protocol.h), writes only the
 * application region above APP_START_BYTE (0x0800), and never touches fuses
 * or EEPROM calibration data (module id, home offset, steps, position map).
 * UPDI stays available as the last resort.
 *
 * Hardware
 * --------
 *  - Internal 20 MHz oscillator (OSCCFG fuse, factory default).
 *  - Bit-banged 9600 8N1 UART on the bus pins:
 *      PA5 = TX (output, idle high)   PA6 = DE (output, low = receive)
 *      PA7 = RX (input)
 *    These are the pins the application's SoftwareSerial uses.  Do not
 *    "simplify" them to PA1/PA2/PA3: the app names its pins with Arduino
 *    numbers (RS485_TX 1, RS485_DE 2, RS485_RX 3) and megaTinyCore's txy6
 *    variant maps Arduino pin 1 -> PA5, 2 -> PA6, 3 -> PA7 (PA1..PA3 are
 *    Arduino pins 14..16).  A bootloader on PA1..PA3 is completely deaf to
 *    the bus: the master's PINGs time out with every fuse and EEPROM field
 *    looking healthy.
 *    USART0 cannot be used: its default pins are PB2/PB3 and its alternate
 *    mapping puts TX/RX on PA1/PA2.  SoftwareSerial in the app uses the
 *    same bit-bang approach on the same pins at the same baud rate.
 *
 * Boot policy
 * -----------
 *  - Boot flag (EEPROM 255) == 0xB0 -> stay in update mode.
 *  - Flag clear and a valid app present -> listen for OTA frames for
 *    BOOT_WAIT_MS (500 ms; any frame addressed to us extends the window),
 *    then jump to the app.
 *  - Flag clear and the app looks erased (first word 0xFFFF) -> stay in
 *    update mode, ready for a first RS485 flash.
 *  - START_UPDATE sets the flag *before* any page is written, so a power
 *    loss mid-session re-enters the bootloader for a re-flash.
 *  - The flag is cleared only by a successful FINISH (flash CRC verified)
 *    or by CMD_RESET.  A module therefore can never be left with a dead
 *    app that cannot re-enter the bootloader.  UPDI remains the last resort.
 *
 * Bus safety for bystander modules
 * --------------------------------
 * All frame bytes after the sync marker are escape-encoded so the byte 'm'
 * (0x6D) never appears raw on the wire; the app's line parser only starts a
 * command line at 'm', so firmware image bytes streaming past other modules
 * can never execute a command there (see protocol.h / codec.h).
 *
 * Size budget
 * ----------
 * Must fit the boot section reported by the build (see platformio.ini).
 * Keep it lean: no generic payload machinery on the response path
 * (responses are at most 3 payload bytes), CRC computed while receiving,
 * no libc calls.
 */

#include <avr/io.h>
#include <stdint.h>

#include "protocol.h"
#include "codec.h"
#include "frame.h"

/* ------------------------------------------------------------------ */
/* Pin and timing constants                                            */
/* ------------------------------------------------------------------ */

#ifndef F_CPU
#define F_CPU 20000000UL
#endif

/* Arduino pins 1/2/3 of the application, i.e. PA5/PA6/PA7 on ATtiny1616
   (megaTinyCore txy6 variant).  See the hardware note at the top. */
#define TX_MASK     (1u << 5)          /* PA5 (Arduino 1) */
#define DE_MASK     (1u << 6)          /* PA6 (Arduino 2) */
#define RX_MASK     (1u << 7)          /* PA7 (Arduino 3) */

#define BIT_CYCLES  (F_CPU / OTA_BAUD_RATE)   /* 2083 at 20 MHz / 9600 */

/* One idle-poll iteration in uart_rx_byte() is about 11 CPU cycles, i.e.
   ~1800 polls per millisecond at 20 MHz.  Only used for coarse timeouts
   (idle waits, the boot window); intra-byte timing is exact via
   __builtin_avr_delay_cycles(). */
#define POLLS_PER_MS  ((F_CPU / 1000UL) / 11)

/* Data-space mappings (see iotn1616.h) */
#define MAPPED_EEPROM   0x1400u
#define MAPPED_FLASH    0x8000u

/* Frame receiver waits */
#define IDLE_WAIT_MS   300    /* per-call idle wait in update mode        */
#define NOISE_WAIT_MS  10     /* wait between noise bytes while resyncing */
#define BYTE_WAIT_MS   10     /* inter-byte timeout inside a frame        */

#define wdr_kick()  __asm__ __volatile__("wdr" ::: "memory")

static void nvm_wait(void)
{
    while (NVMCTRL_STATUS & (NVMCTRL_FBUSY_bm | NVMCTRL_EEBUSY_bm))
        ;
}

static void eeprom_write_byte(uint8_t index, uint8_t value)
{
    /* Only ever used for the boot flag (EEPROM 255, alone in its 32-byte
       EEPROM page).  Same write model as megaTinyCore's EEPROM library:
       mapped-region store loads the page buffer and NVMCTRL.ADDR, the
       CCP-unlocked PAGEERASEWRITE commits. */
    if (*(volatile uint8_t *)(MAPPED_EEPROM + index) == value)
        return;                               /* no wear on identical value */
    nvm_wait();
    *(volatile uint8_t *)(MAPPED_EEPROM + index) = value;
    _PROTECTED_WRITE_SPM(NVMCTRL_CTRLA, NVMCTRL_CMD_PAGEERASEWRITE_gc);
    nvm_wait();
}

#ifdef OTA_DEBUG_MARKERS
/* Bench breadcrumbs; see src/DebugMark.h in the app for the address map.
   Kept out of normal builds: they cost a few EEPROM writes per boot. */
#define DBG_EE_RESET_FLAGS 248
#define DBG_EE_APP_WORD_LO 245
#define DBG_EE_APP_WORD_HI 246
#define DBG_EE_STAYING     247
#define DBG_EE_BL_BOOTS    250
#define DBG_EE_BL_JUMPED   251
#define DBG_BL_JUMPED_VALUE 0x5A
#define DBG_BL_STAYING_VALUE 0xAA

static void debug_mark_boot(void)
{
    uint8_t boots = *(volatile uint8_t *)(MAPPED_EEPROM + DBG_EE_BL_BOOTS);
    boots = (boots == 0xFF) ? 1 : (uint8_t)(boots + 1);
    eeprom_write_byte(DBG_EE_RESET_FLAGS, RSTCTRL_RSTFR);
    eeprom_write_byte(DBG_EE_BL_BOOTS, boots);
}
#endif

/* ------------------------------------------------------------------ */
/* Bit-banged UART                                                     */
/* ------------------------------------------------------------------ */

/*
 * uart_tx_byte(): 8 data bits, no parity, one stop bit, LSB first.
 *
 * Each bit slot is BIT_CYCLES long; the delay constants subtract the
 * surrounding pin write and loop overhead.  The residual error is well
 * under 1% of a bit time, and every byte restarts at its own start bit,
 * so drift cannot accumulate across bytes.
 */
static void uart_tx_byte(uint8_t b)
{
    VPORTA_OUT &= (uint8_t)~TX_MASK;            /* start bit */
    __builtin_avr_delay_cycles(BIT_CYCLES - 6);

    for (uint8_t i = 8; i; i--) {
        uint8_t out = (uint8_t)(VPORTA_OUT & (uint8_t)~TX_MASK);
        if (b & 1)
            out |= TX_MASK;
        VPORTA_OUT = out;
        b >>= 1;
        __builtin_avr_delay_cycles(BIT_CYCLES - 14);
    }

    VPORTA_OUT |= TX_MASK;                      /* stop bit */
    __builtin_avr_delay_cycles(BIT_CYCLES - 6); /* stop bit fully elapses */
}

/*
 * uart_rx_byte(): waits up to wait_ms for a start bit, then samples the
 * byte at bit centres.  Returns the byte, -1 on timeout and -2 on a
 * framing error (stop bit not high).  The idle wait kicks the watchdog.
 * Callers never pass 0 (see recv_frame); a zero wait would wrap to 65 s.
 */
static int uart_rx_byte(uint16_t wait_ms)
{
    while (wait_ms--) {
        for (uint16_t p = 0; p < POLLS_PER_MS; p++) {
            if (!(VPORTA_IN & RX_MASK))
                goto start;
            wdr_kick();
        }
    }
    return -1;

start:
    /* Start edge seen: sample data bit 0 at 1.5 bit times. */
    __builtin_avr_delay_cycles(BIT_CYCLES + BIT_CYCLES / 2 - 10);

    uint8_t b = 0;
    for (uint8_t i = 8; i; i--) {
        b >>= 1;
        if (VPORTA_IN & RX_MASK)
            b |= 0x80;
        __builtin_avr_delay_cycles(BIT_CYCLES - 12);
    }

    if (!(VPORTA_IN & RX_MASK))
        return -2;                              /* framing error */
    return b;
}

/* ------------------------------------------------------------------ */
/* Frame receive adapter                                               */
/* ------------------------------------------------------------------ */

/* The one frame buffer. Decoding and session policy live in frame.c. */
static ota_frame_t fr;

/* The fetch adapter gives the sync byte a longer wait than frame bytes. */
static uint16_t fetch_wait_ms;

static int uart_fetch(void)
{
    int byte = uart_rx_byte(fetch_wait_ms);
    fetch_wait_ms = BYTE_WAIT_MS;
    return byte;
}

/*
 * recv_frame(): consume bytes up to the sync marker, then hand the frame to
 * the decoder module. Returns 1 on a CRC-valid frame, 0 on timeout/noise.
 */
static uint8_t recv_frame(uint16_t first_wait_ms)
{
    fetch_wait_ms = first_wait_ms;
    for (;;) {
        int b = uart_fetch();
        if (b < 0)
            return 0;                           /* idle timeout */
        if (b == OTA_SYNC)
            break;
        fetch_wait_ms = NOISE_WAIT_MS;          /* noise byte, resync */
    }
    fetch_wait_ms = BYTE_WAIT_MS;
    return ota_frame_receive(uart_fetch, &fr);
}

/* ------------------------------------------------------------------ */
/* Session state                                                       */
/* ------------------------------------------------------------------ */

static uint8_t module_id;
static ota_session_t session;

/* ------------------------------------------------------------------ */
/* Frame transmit                                                      */
/* ------------------------------------------------------------------ */

/* Sends one logical byte, escape-encoded. */
static void uart_tx_escaped(uint8_t b)
{
    if (b == OTA_ESC) {
        uart_tx_byte(OTA_ESC);
        uart_tx_byte(OTA_ESC_ESC);
    } else if (b == OTA_FORBIDDEN) {
        uart_tx_byte(OTA_ESC);
        uart_tx_byte(OTA_ESC_M);
    } else {
        uart_tx_byte(b);
    }
}

/*
 * send_response(): transmits [SYNC][module_id][cmd][len][payload][crc16]
 * with everything after the sync byte escape-encoded.  Responses carry at
 * most 3 payload bytes (PING info; NACK error code), so a small fixed
 * buffer suffices - no generic payload path.  The stop bit of the last
 * byte fully elapses inside uart_tx_byte before DE is released.
 */
static void send_response(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    if (len > OTA_RESP_MAX_PAYLOAD)
        len = OTA_RESP_MAX_PAYLOAD;

    uint8_t buf[3 + OTA_RESP_MAX_PAYLOAD];
    buf[0] = module_id;
    buf[1] = cmd;
    buf[2] = len;
    for (uint8_t i = 0; i < len; i++)
        buf[3 + i] = payload[i];

    uint16_t crc = ota_crc16(buf, (uint8_t)(3 + len));

    /* Bus turnaround: while the master transmits it also drains its own
       half-duplex echo and can keep its transceiver's driver enabled for up
       to ~5 ms after the last stop bit (longer frames overflow its RX ring
       and hit that timeout).  The application waits 10 ms before every
       ASCII reply for the same reason; without this the first byte of our
       response can collide with the master's still-enabled driver. */
    __builtin_avr_delay_cycles(F_CPU / 100);    /* 10 ms */

    VPORTA_OUT |= DE_MASK;                      /* driver on */
    __builtin_avr_delay_cycles(2000);            /* settle, ~100 us */
    uart_tx_byte(OTA_SYNC);
    for (uint8_t i = 0; i < (uint8_t)(3 + len); i++)
        uart_tx_escaped(buf[i]);
    uart_tx_escaped((uint8_t)(crc >> 8));
    uart_tx_escaped((uint8_t)crc);
    VPORTA_OUT &= (uint8_t)~DE_MASK;            /* driver off */
}

/* ------------------------------------------------------------------ */
/* Flash programming                                                   */
/* ------------------------------------------------------------------ */

/*
 * flash_write_page(): same model optiboot_x uses on these parts - stores
 * through the mapped flash region load the page buffer and NVMCTRL.ADDR,
 * the CCP-unlocked PAGEERASEWRITE commits.  The page buffer is always
 * filled with a complete 64-byte page, so no PAGEBUFCLR is needed.
 */
static void flash_write_page(uint16_t page, const uint8_t *data)
{
    /* Clamp to the app region: never write the boot section (defense in
       depth; handle_write_page already validated the page number). */
    if (page >= APP_MAX_PAGES)
        return;

    uint16_t mapped = (uint16_t)(MAPPED_FLASH + APP_START_BYTE
                                 + page * FLASH_PAGE_SIZE);
    nvm_wait();
    for (uint8_t i = 0; i < FLASH_PAGE_SIZE; i++)
        *(volatile uint8_t *)(mapped + i) = data[i];
    _PROTECTED_WRITE_SPM(NVMCTRL_CTRLA, NVMCTRL_CMD_PAGEERASEWRITE_gc);
    nvm_wait();
}

static uint32_t app_region_crc32(uint16_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    const volatile uint8_t *p =
        (const volatile uint8_t *)(MAPPED_FLASH + APP_START_BYTE);
    while (size--)
        crc = ota_crc32_byte(crc, *p++);
    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t app_looks_present(void)
{
    return *(volatile uint16_t *)(MAPPED_FLASH + APP_START_BYTE) != 0xFFFFu;
}

/* ------------------------------------------------------------------ */
/* App entry and reset                                                 */
/* ------------------------------------------------------------------ */

#define STR2(x) #x
#define STR(x) STR2(x)

__attribute__((noreturn, noinline))
static void start_app(void)
{
    while (WDT_STATUS & WDT_SYNCBUSY_bm)
        ;
    _PROTECTED_WRITE(WDT_CTRLA, WDT_PERIOD_OFF_gc);

#ifdef OTA_DEBUG_MARKERS
    eeprom_write_byte(DBG_EE_BL_JUMPED, DBG_BL_JUMPED_VALUE);
#endif

    /* Reset flags are deliberately left as the hardware set them.  The
       Arduino core's .init3 hook (init_reset_flags() in megaTinyCore's
       main.cpp) reads RSTCTRL.RSTFR, clears it, and issues a SOFTWARE RESET
       when it finds no flags at all, on the assumption that a chip with zero
       flags did not come through a real reset.  Clearing them here therefore
       bounces the app straight back into the bootloader: reset loop, no
       setup(), no homing, no RS485.  Never clear RSTFR on the hand-over. */
    VPORTA_DIR = 0x00;                          /* bus pins to reset     */
    VPORTA_OUT = 0x00;                          /* state (all inputs)    */

    /* BOOTEND makes the hardware relocate the app vector table to
       APP_START_BYTE, so a plain jump is all that is needed.  GNU as takes
       jmp operands as BYTE addresses and encodes the word address itself;
       using APP_START_BYTE/2 here would jump to 0x0400 (inside this
       bootloader) instead of 0x0800. */
    __asm__ __volatile__("jmp " STR(APP_START_BYTE));
    __builtin_unreachable();
}

__attribute__((noreturn, noinline))
static void reset_now(void)
{
    /* Small margin so the master latches the ACK's final stop bit before
       the pin reset releases the bus driver. */
    __builtin_avr_delay_cycles(F_CPU / 500);   /* ~2 ms */
    while (WDT_STATUS & WDT_SYNCBUSY_bm)
        ;
    _PROTECTED_WRITE(WDT_CTRLA, WDT_PERIOD_OFF_gc);
    /* RSTCTRL.SWRR is CCP-protected (IO register protection, signature
       0xD8).  A plain store to it is silently ignored, which leaves the
       bootloader spinning in the loop below after FINISH/RESET: the
       master sees the ACK but the app never runs (module silent, no
       homing).  The app's softwareReset() and megaTinyCore's own
       init_reset_flags() use _PROTECTED_WRITE for the same reason. */
    _PROTECTED_WRITE(RSTCTRL_SWRR, RSTCTRL_SWRE_bm);
    for (;;)
        ;                                      /* reset takes effect now */
}

/* ------------------------------------------------------------------ */
/* Session port adapters                                               */
/* ------------------------------------------------------------------ */

static void bl_send_response(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    send_response(cmd, payload, len);
}

static void bl_write_page(uint16_t page, const uint8_t *data)
{
    flash_write_page(page, data);
}

static uint32_t bl_app_crc32(uint16_t size)
{
    return app_region_crc32(size);
}

static void bl_set_boot_flag(uint8_t value)
{
    eeprom_write_byte(OTA_EEPROM_BOOT_FLAG_ADDR, value);
}

static void bl_reset(void)
{
    reset_now();
}

static const ota_session_ports_t session_ports = {
    bl_send_response, bl_write_page, bl_app_crc32, bl_set_boot_flag, bl_reset
};

static uint8_t handle_frame(void)
{
    return ota_session_handle(&session, &fr, module_id, &session_ports);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
#ifdef OTA_DEBUG_MARKERS
    debug_mark_boot();
#endif

    /* OSCCFG fuse already selects the 20 MHz internal oscillator (factory
       default).  Make sure the clock prescaler is disabled so bit timings
       match F_CPU. */
    _PROTECTED_WRITE(CLKCTRL_MCLKCTRLB, 0x00);

    /* TX idle high (mark) before enabling the output, then DE low. */
    VPORTA_OUT = TX_MASK;
    VPORTA_DIR = TX_MASK | DE_MASK;

    module_id = *(volatile uint8_t *)(MAPPED_EEPROM + OTA_EEPROM_MODULE_ID_ADDR);
    ota_session_init(&session,
                     (*(volatile uint8_t *)(MAPPED_EEPROM
                                            + OTA_EEPROM_BOOT_FLAG_ADDR)
                      == OTA_BOOT_FLAG_VALUE)
                     || !app_looks_present());
#ifdef OTA_DEBUG_MARKERS
    {
        uint16_t app_word = *(volatile uint16_t *)(MAPPED_FLASH + APP_START_BYTE);
        eeprom_write_byte(DBG_EE_APP_WORD_LO, (uint8_t)(app_word & 0xFF));
        eeprom_write_byte(DBG_EE_APP_WORD_HI, (uint8_t)(app_word >> 8));
        eeprom_write_byte(DBG_EE_STAYING, ota_session_staying(&session)
                                             ? DBG_BL_STAYING_VALUE : 0x55);
    }
#endif
    uint16_t window_left_ms = BOOT_WAIT_MS;    /* boot window when not staying */

    while (WDT_STATUS & WDT_SYNCBUSY_bm)
        ;
    _PROTECTED_WRITE(WDT_CTRLA, WDT_PERIOD_2KCLK_gc);

    for (;;) {
        wdr_kick();

        uint8_t got;
        if (ota_session_staying(&session)) {
            got = recv_frame(IDLE_WAIT_MS);
        } else {
            if (window_left_ms == 0)
                start_app();
            /* Listen in small slices so the window expires on time even
               while traffic from another module's OTA session passes by
               on the shared bus. */
            uint16_t slice = window_left_ms > 2 ? 2 : window_left_ms;
            window_left_ms = (uint16_t)(window_left_ms - slice);
            got = recv_frame(slice);
        }
        if (!got)
            continue;

        /* The session module filters addresses and owns every state change. */
        uint8_t addressed = handle_frame();
        if (!ota_session_staying(&session) && addressed)
            window_left_ms = BOOT_WAIT_MS;
    }
}
