#include "FossorialModule.h"
#include "MeshService.h"
#include "configuration.h"
#include "main.h"

#define FOSSORIAL_RESPONSE_DELAY  500
#define FOSSORIAL_MAX_RESPONSE    4096

extern MeshService *service;

// ─── Wire format helpers ─────────────────────────────────────────────────

static void writeChunkHdr(uint8_t *dst, uint8_t flags, uint8_t seq)
{
    dst[0] = FOSSORIAL_MARKER;
    dst[1] = flags;
    dst[2] = seq;
}

static constexpr size_t CHUNK_OVERHEAD = 3; // marker + flags + seq
static constexpr size_t CHUNK_MAX_DATA =
    FOSSORIAL_CHUNK_SIZE > CHUNK_OVERHEAD ? FOSSORIAL_CHUNK_SIZE - CHUNK_OVERHEAD : 128;

// ─── Constructor ─────────────────────────────────────────────────────────

FossorialModule::FossorialModule()
    : SinglePortModule("fossorial", meshtastic_PortNum_PRIVATE_APP)
{
    LOG_INFO("Fossorial module initialised (port %d)", meshtastic_PortNum_PRIVATE_APP);
}

// ─── Packet handler ──────────────────────────────────────────────────────

ProcessMessage FossorialModule::handleReceived(const meshtastic_MeshPacket &p)
{
    auto &decoded = p.decoded;

    // Must have at least marker + one selector byte
    if (decoded.payload.size < 2) {
        return ProcessMessage::CONTINUE;
    }

    // Check for Fossorial marker
    if (decoded.payload.bytes[0] != FOSSORIAL_MARKER) {
        return ProcessMessage::CONTINUE;
    }

    // Extract selector (bytes 1 .. end)
    size_t sel_len = decoded.payload.size - 1;
    if (sel_len > FOSSORIAL_MAX_SELECTOR) {
        sel_len = FOSSORIAL_MAX_SELECTOR;
    }

    // Copy selector into a null-terminated buffer
    char selector[FOSSORIAL_MAX_SELECTOR + 1];
    memcpy(selector, &decoded.payload.bytes[1], sel_len);
    selector[sel_len] = 0;

    LOG_INFO("Fossorial request from 0x%08x: \"%s\"", p.from, selector);

    // Build and send response
    serveSelector(p, selector, sel_len);

    return ProcessMessage::STOP;
}

// ─── Selector serving ────────────────────────────────────────────────────

void FossorialModule::serveSelector(const meshtastic_MeshPacket &req,
                                     const char *selector, size_t sel_len)
{
    char response[FOSSORIAL_MAX_RESPONSE];
    size_t resp_len = 0;

    // Resolve and serve the selector through the Fossorial pipeline
    auto result = fossorial::serveSelector<MeshtasticFS, fossorial::MenuFormat::LoRa>(
        _fs, selector, sel_len,
        [&](const char *chunk, size_t chunk_len) {
            size_t room = sizeof(response) - resp_len;
            size_t n = chunk_len < room ? chunk_len : room;
            if (n > 0) {
                memcpy(response + resp_len, chunk, n);
                resp_len += n;
            }
        }
    );

    if (result == fossorial::ResponseType::NotFound) {
        char errbuf[128];
        resp_len = fossorial::buildErrorResponse(errbuf, sizeof(errbuf), "Not found");
        sendGopherChunk(req, errbuf, resp_len, 0, 0);
        return;
    }

    // Append Gopher terminator ".\r\n"
    if (resp_len + 3 <= sizeof(response)) {
        response[resp_len++] = '.';
        response[resp_len++] = '\r';
        response[resp_len++] = '\n';
    }

    // Chunk and send
    size_t offset = 0;
    uint8_t seq = 0;

    while (offset < resp_len) {
        size_t remaining = resp_len - offset;
        size_t chunk_len = remaining < CHUNK_MAX_DATA ? remaining : CHUNK_MAX_DATA;
        bool is_first = (offset == 0);
        bool is_last = (offset + chunk_len >= resp_len);

        uint8_t flags;
        if (is_first && is_last)      flags = 0;
        else if (is_first)            flags = 1;
        else if (is_last)             flags = 4;
        else                          flags = 2;

        sendGopherChunk(req, response + offset, chunk_len, flags, seq);

        offset += chunk_len;
        ++seq;
    }
}

// ─── Send a single chunk ─────────────────────────────────────────────────

void FossorialModule::sendGopherChunk(const meshtastic_MeshPacket &req,
                                       const char *data, size_t len,
                                       uint8_t flags, uint8_t seq)
{
    // Total payload = 3-byte header + content
    size_t pkt_len = CHUNK_OVERHEAD + len;

    auto p = allocDataPacket();
    if (!p) {
        LOG_WARN("Fossorial: allocDataPacket failed");
        return;
    }

    setReplyTo(p, req);
    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;

    if (pkt_len > sizeof(p->decoded.payload.bytes)) {
        LOG_WARN("Fossorial: chunk too large (%zu > %zu)", pkt_len,
                 sizeof(p->decoded.payload.bytes));
        return;
    }

    writeChunkHdr(p->decoded.payload.bytes, flags, seq);
    if (len > 0) {
        memcpy(&p->decoded.payload.bytes[CHUNK_OVERHEAD], data, len);
    }
    p->decoded.payload.size = pkt_len;

    service->sendToMesh(p, RX_SRC_LOCAL);
}
