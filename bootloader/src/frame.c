#include "frame.h"
#include "codec.h"

uint8_t ota_frame_receive(ota_fetch_fn fetch, ota_frame_t *out)
{
    uint16_t crc = 0xFFFF;
    int consumed = 0;

    for (uint8_t i = 0; i < 3; i++) {
        int d = ota_escape_decode(fetch, &consumed);
        if (d < 0) return 0;
        uint8_t byte = (uint8_t)d;
        if (i == 0) out->addr = byte;
        else if (i == 1) out->cmd = byte;
        else out->len = byte;
        crc = ota_crc16_byte(crc, byte);
    }
    if (out->len > OTA_MAX_PAYLOAD) return 0;

    for (uint8_t i = 0; i < out->len; i++) {
        int d = ota_escape_decode(fetch, &consumed);
        if (d < 0) return 0;
        out->payload[i] = (uint8_t)d;
        crc = ota_crc16_byte(crc, (uint8_t)d);
    }

    int hi = ota_escape_decode(fetch, &consumed);
    int lo = (hi < 0) ? -1 : ota_escape_decode(fetch, &consumed);
    if (hi < 0 || lo < 0) return 0;

    return (uint16_t)((hi << 8) | lo) == crc;
}

void ota_session_init(ota_session_t *session, uint8_t staying)
{
    session->staying = staying ? 1 : 0;
    session->size = 0;
    session->crc[0] = session->crc[1] = session->crc[2] = session->crc[3] = 0;
}

uint8_t ota_session_staying(const ota_session_t *session)
{
    return session->staying;
}

static void send_ack(const ota_session_ports_t *ports)
{
    ports->send_response(RESP_ACK, 0, 0);
}

static void send_nack(const ota_session_ports_t *ports, uint8_t error)
{
    ports->send_response(RESP_NACK, &error, 1);
}

uint8_t ota_session_handle(ota_session_t *session, const ota_frame_t *frame,
                           uint8_t module_id,
                           const ota_session_ports_t *ports)
{
    if (frame->addr != module_id) return 0;

    switch (frame->cmd) {
    case CMD_PING: {
        uint8_t info[3] = { BOOTLOADER_VERSION, module_id,
                            (uint8_t)(session->staying ? 1 : 0) };
        ports->send_response(RESP_ACK, info, sizeof(info));
        break;
    }
    case CMD_START_UPDATE: {
        if (frame->len != LEN_START_UPDATE) {
            send_nack(ports, ERR_LENGTH);
            break;
        }
        uint16_t size = (uint16_t)((frame->payload[0] << 8) | frame->payload[1]);
        if (size == 0 || size > APP_MAX_SIZE) {
            send_nack(ports, ERR_ADDRESS);
            break;
        }
        session->size = size;
        for (uint8_t i = 0; i < 4; i++)
            session->crc[i] = frame->payload[2 + i];
        /* Arm before the first page: a power cut mid-session re-enters us. */
        ports->set_boot_flag(OTA_BOOT_FLAG_VALUE);
        session->staying = 1;
        send_ack(ports);
        break;
    }
    case CMD_WRITE_PAGE: {
        if (frame->len != LEN_WRITE_PAGE) {
            send_nack(ports, ERR_LENGTH);
            break;
        }
        if (!session->staying) {
            send_nack(ports, ERR_SEQUENCE);
            break;
        }
        uint16_t page = (uint16_t)((frame->payload[0] << 8) | frame->payload[1]);
        if (page >= APP_MAX_PAGES) {
            send_nack(ports, ERR_ADDRESS);
            break;
        }
        ports->write_page(page, frame->payload + 2);
        send_ack(ports);
        break;
    }
    case CMD_FINISH: {
        if (frame->len != LEN_FINISH) {
            send_nack(ports, ERR_LENGTH);
            break;
        }
        if (!session->staying || session->size == 0) {
            send_nack(ports, ERR_SEQUENCE);
            break;
        }
        for (uint8_t i = 0; i < 4; i++)
            if (frame->payload[i] != session->crc[i]) {
                send_nack(ports, ERR_SEQUENCE);
                return 1;
            }

        uint32_t actual = ports->app_crc32(session->size);
        for (uint8_t i = 0; i < 4; i++)
            if (frame->payload[i] != (uint8_t)(actual >> (24 - 8 * i))) {
                /* Flag stays set so the master can re-flash. */
                send_nack(ports, ERR_VERIFY);
                return 1;
            }

        ports->set_boot_flag(0x00);
        send_ack(ports);
        ports->reset();
        break;
    }
    case CMD_RESET:
        ports->set_boot_flag(0x00);
        send_ack(ports);
        ports->reset();
        break;
    default:
        break;
    }

    return 1;
}
