/*
 * Host tests for the OTA frame decoder and session module. These are the
 * exact sources the bootloader builds, not copies:
 *
 *   cc -O2 -Wall -Werror -I../src -o test_frame \
 *     test_frame.c ../src/frame.c && ./test_frame
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "frame.h"
#include "codec.h"

static const uint8_t *g_wire;
static uint16_t g_wire_len;
static uint16_t g_wire_pos;

static int fetch(void)
{
    return g_wire_pos < g_wire_len ? g_wire[g_wire_pos++] : -1;
}

static uint16_t build_logical(uint8_t addr, uint8_t cmd,
                              const uint8_t *payload, uint8_t len,
                              uint8_t *logical)
{
    logical[0] = addr;
    logical[1] = cmd;
    logical[2] = len;
    for (uint8_t i = 0; i < len; i++) logical[3 + i] = payload[i];
    uint16_t crc = ota_crc16(logical, (uint16_t)(3 + len));
    logical[3 + len] = (uint8_t)(crc >> 8);
    logical[4 + len] = (uint8_t)crc;
    return (uint16_t)(5 + len);
}

static void set_wire(const uint8_t *logical, uint16_t len)
{
    static uint8_t wire[256];
    g_wire_len = ota_escape_encode(logical, len, wire);
    g_wire = wire;
    g_wire_pos = 0;
}

static void test_decode(void)
{
    uint8_t payload[66];
    for (uint8_t i = 0; i < sizeof(payload); i++)
        payload[i] = (uint8_t)(0x6D - i); /* includes 0x5C and 0x6D */
    uint8_t logical[3 + 66 + 2];
    uint16_t len = build_logical(3, CMD_WRITE_PAGE, payload, 66, logical);
    set_wire(logical, len);

    ota_frame_t frame;
    assert(ota_frame_receive(fetch, &frame) == 1);
    assert(frame.addr == 3 && frame.cmd == CMD_WRITE_PAGE && frame.len == 66);
    assert(memcmp(frame.payload, payload, 66) == 0);

    /* CRC corruption is rejected. */
    logical[len - 1] ^= 0x01;
    set_wire(logical, len);
    assert(ota_frame_receive(fetch, &frame) == 0);

    /* A raw 'm' never appears on the wire; if forced in, it is rejected. */
    static uint8_t bad[256];
    set_wire(logical, len);
    memcpy(bad, g_wire, g_wire_len);
    bad[1] = 0x6D;
    g_wire = bad;
    g_wire_pos = 0;
    assert(ota_frame_receive(fetch, &frame) == 0);

    /* Oversized length and truncated frames are rejected. */
    logical[2] = OTA_MAX_PAYLOAD + 1;
    len = build_logical(3, CMD_WRITE_PAGE, payload, 1, logical);
    logical[2] = OTA_MAX_PAYLOAD + 1;
    set_wire(logical, len);
    assert(ota_frame_receive(fetch, &frame) == 0);

    len = build_logical(3, CMD_PING, 0, 0, logical);
    set_wire(logical, len);
    g_wire_len--;
    assert(ota_frame_receive(fetch, &frame) == 0);

    printf("frame decode ok\n");
}

static uint8_t last_cmd, last_payload[3], last_len;
static int responses, flag_writes, resets, page_writes;
static uint16_t last_page;
static uint32_t app_crc = 0x11223344UL;

static void test_send(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    ++responses;
    last_cmd = cmd;
    last_len = len;
    for (uint8_t i = 0; i < len && i < sizeof(last_payload); i++)
        last_payload[i] = payload[i];
}

static void test_page(uint16_t page, const uint8_t *data)
{
    ++page_writes;
    last_page = page;
    (void)data;
}

static uint32_t test_crc(uint16_t size)
{
    (void)size;
    return app_crc;
}

static void test_flag(uint8_t value)
{
    ++flag_writes;
    if (value == 0) app_crc = 0; /* flag cleared only on verified finish */
}

static void test_reset(void)
{
    ++resets;
}

static const ota_session_ports_t ports = {
    test_send, test_page, test_crc, test_flag, test_reset
};

static ota_frame_t make_frame(uint8_t addr, uint8_t cmd,
                              const uint8_t *payload, uint8_t len)
{
    ota_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.addr = addr;
    frame.cmd = cmd;
    frame.len = len;
    for (uint8_t i = 0; i < len; i++) frame.payload[i] = payload[i];
    return frame;
}

static void reset_fakes(void)
{
    responses = flag_writes = resets = page_writes = 0;
    last_cmd = last_len = 0;
    memset(last_payload, 0, sizeof(last_payload));
}

static void test_session(void)
{
    ota_session_t session;
    uint8_t info[6];

    /* PING reports staying and only frames addressed to us are handled. */
    ota_session_init(&session, 0);
    reset_fakes();
    info[0] = BOOTLOADER_VERSION; info[1] = 3; info[2] = 0;
    ota_frame_t ping = make_frame(3, CMD_PING, 0, 0);
    assert(ota_session_handle(&session, &ping, 3, &ports) == 1);
    assert(last_cmd == RESP_ACK && last_len == 3);
    assert(memcmp(last_payload, info, 3) == 0);
    reset_fakes();
    ping.addr = 4;
    assert(ota_session_handle(&session, &ping, 3, &ports) == 0);
    assert(responses == 0);

    /* A page before START_UPDATE is a sequence error. */
    uint8_t pagePayload[LEN_WRITE_PAGE] = {0, 5};
    ota_frame_t page = make_frame(3, CMD_WRITE_PAGE, pagePayload,
                                  LEN_WRITE_PAGE);
    reset_fakes();
    ota_session_handle(&session, &page, 3, &ports);
    assert(last_cmd == RESP_NACK && last_payload[0] == ERR_SEQUENCE);
    assert(page_writes == 0);

    /* START_UPDATE rejects bad lengths and sizes, then arms the flag. */
    uint8_t start[LEN_START_UPDATE] = {0x10, 0x00, 0x11, 0x22, 0x33, 0x44};
    reset_fakes();
    ota_frame_t badStart = make_frame(3, CMD_START_UPDATE, start, 5);
    ota_session_handle(&session, &badStart, 3, &ports);
    assert(last_cmd == RESP_NACK && last_payload[0] == ERR_LENGTH);

    reset_fakes();
    start[0] = start[1] = 0;
    ota_frame_t zeroStart = make_frame(3, CMD_START_UPDATE, start,
                                       LEN_START_UPDATE);
    ota_session_handle(&session, &zeroStart, 3, &ports);
    assert(last_cmd == RESP_NACK && last_payload[0] == ERR_ADDRESS);

    reset_fakes();
    start[0] = 0x10; start[1] = 0x00;
    ota_frame_t goodStart = make_frame(3, CMD_START_UPDATE, start,
                                       LEN_START_UPDATE);
    assert(ota_session_handle(&session, &goodStart, 3, &ports) == 1);
    assert(last_cmd == RESP_ACK && flag_writes == 1);
    assert(ota_session_staying(&session) == 1);

    /* Valid pages reach the NVM adapter; out-of-range pages do not. */
    reset_fakes();
    ota_session_handle(&session, &page, 3, &ports);
    assert(page_writes == 1 && last_page == 5 && last_cmd == RESP_ACK);
    pagePayload[0] = pagePayload[1] = 0xFF;
    page = make_frame(3, CMD_WRITE_PAGE, pagePayload, LEN_WRITE_PAGE);
    reset_fakes();
    ota_session_handle(&session, &page, 3, &ports);
    assert(last_cmd == RESP_NACK && last_payload[0] == ERR_ADDRESS);
    assert(page_writes == 0);

    /* FINISH: wrong declared CRC, wrong image CRC, then success. */
    uint8_t finish[LEN_FINISH] = {0x11, 0x22, 0x33, 0x44};
    reset_fakes();
    ota_frame_t badFinish = make_frame(3, CMD_FINISH, finish, LEN_FINISH);
    finish[0] = 0;
    badFinish = make_frame(3, CMD_FINISH, finish, LEN_FINISH);
    ota_session_handle(&session, &badFinish, 3, &ports);
    assert(last_cmd == RESP_NACK && last_payload[0] == ERR_SEQUENCE);
    assert(flag_writes == 0 && resets == 0);

    reset_fakes();
    app_crc = 0x99AABBCCUL;
    finish[0] = 0x11;
    badFinish = make_frame(3, CMD_FINISH, finish, LEN_FINISH);
    ota_session_handle(&session, &badFinish, 3, &ports);
    assert(last_cmd == RESP_NACK && last_payload[0] == ERR_VERIFY);

    reset_fakes();
    app_crc = 0x11223344UL;
    ota_frame_t goodFinish = make_frame(3, CMD_FINISH, finish, LEN_FINISH);
    ota_session_handle(&session, &goodFinish, 3, &ports);
    assert(last_cmd == RESP_ACK && flag_writes == 1 && resets == 1);

    /* RESET clears the flag and reboots without a session. */
    ota_session_init(&session, 1);
    reset_fakes();
    ota_frame_t reset = make_frame(3, CMD_RESET, 0, 0);
    ota_session_handle(&session, &reset, 3, &ports);
    assert(last_cmd == RESP_ACK && flag_writes == 1 && resets == 1);

    printf("frame session ok\n");
}

int main(void)
{
    test_decode();
    test_session();
    printf("all frame tests passed\n");
    return 0;
}
