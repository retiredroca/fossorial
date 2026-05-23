#include "GopherMesh.h"

#include <helpers/sensors/EnvironmentSensorManager.h>

extern EnvironmentSensorManager sensors;

#define SERVER_RESPONSE_DELAY    300
#define GOPHER_RESPONSE_DELAY    500
#define LAZY_CONTACTS_WRITE_DELAY  5000
#define ADV_TYPE_GOPHER          5

// ─── Gopher response header bytes in TXT_MSG text payload ──────────────
//
// Byte 5: marker  = 'G' (0x47) — identifies this as a Gopher response
// Byte 6: flags   — GOPHER_FLAG_SOLE / FIRST / MID / LAST
// Byte 7: seq     — sequence number (0–255)
// Byte 8+:        — Gopher content (menu lines or file text)

#define GOPHER_MARKER   'G'

static inline void writeGopherHdr(uint8_t* dst, uint8_t flags, uint8_t seq) {
    dst[0] = GOPHER_MARKER;
    dst[1] = flags;
    dst[2] = seq;
}

static inline bool isGopherResponse(const uint8_t* text) {
    return text[0] == GOPHER_MARKER;
}

// ═══════════════════════════════════════════════════════════════════════
//  Constructor
// ═══════════════════════════════════════════════════════════════════════

GopherMesh::GopherMesh(mesh::MainBoard& board, mesh::Radio& radio,
                       mesh::MillisecondClock& ms, mesh::RNG& rng,
                       mesh::RTCClock& rtc, mesh::MeshTables& tables)
    : mesh::Mesh(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), tables),
      region_map(key_store), temp_map(key_store),
      _cli(board, rtc, sensors, region_map, acl, &_prefs, this)
{
    last_millis = 0;
    uptime_millis = 0;
    next_local_advert = next_flood_advert = 0;
    dirty_contacts_expiry = 0;
    _logging = false;

    memset(&_prefs, 0, sizeof(_prefs));
    _prefs.airtime_factor = 1.0;
    _prefs.rx_delay_base = 0.0f;
    _prefs.tx_delay_factor = 0.5f;
    _prefs.direct_tx_delay_factor = 0.2f;
    StrHelper::strncpy(_prefs.node_name, "Fossorial Gopher", sizeof(_prefs.node_name));
    StrHelper::strncpy(_prefs.password, "admin", sizeof(_prefs.password));
    _prefs.freq = 915.0;
    _prefs.sf = 10;
    _prefs.bw = 250;
    _prefs.cr = 5;
    _prefs.tx_power_dbm = 20;
    _prefs.disable_fwd = 1;
    _prefs.advert_interval = 1;
    _prefs.flood_advert_interval = 12;
    _prefs.flood_max = 64;
    _prefs.interference_threshold = 0;
    _prefs.gps_enabled = 0;
    _prefs.gps_interval = 0;
    _prefs.advert_loc_policy = ADVERT_LOC_PREFS;

    memset(_clients, 0, sizeof(_clients));
}

// ═══════════════════════════════════════════════════════════════════════
//  begin / loop
// ═══════════════════════════════════════════════════════════════════════

void GopherMesh::begin(FILESYSTEM* fs) {
    mesh::Mesh::begin();
    _fs = fs;
    _gopher_fs.init(fs);
    _cli.loadPrefs(_fs);
    acl.load(_fs, self_id);
    region_map.load(_fs);

    RegionEntry* r = region_map.getDefaultRegion();
    if (r) {
        TransportKey scope;
        region_map.getTransportKeysFor(*r, &scope, 1);
    }

    radio_set_params(_prefs.freq, _prefs.bw, _prefs.sf, _prefs.cr);
    radio_set_tx_power(_prefs.tx_power_dbm);
    updateAdvertTimer();
    updateFloodAdvertTimer();
    board.setAdcMultiplier(_prefs.adc_multiplier);
}

void GopherMesh::loop() {
    // uptime counter
    uint32_t now = getRTCClock()->getCurrentTime();
    uint32_t ms = millis();
    if (ms - last_millis >= 1000) {
        uptime_millis += ms - last_millis;
        last_millis = ms;
    }

    // Periodic self-advertisement
    if (next_local_advert && ms >= next_local_advert) {
        sendSelfAdvertisement(0, false);
        updateAdvertTimer();
    }
    if (next_flood_advert && ms >= next_flood_advert) {
        sendSelfAdvertisement(0, true);
        updateFloodAdvertTimer();
    }

    // Lazy contacts write
    if (dirty_contacts_expiry && ms >= dirty_contacts_expiry) {
        acl.save(_fs);
        dirty_contacts_expiry = 0;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Packet handling
// ═══════════════════════════════════════════════════════════════════════

bool GopherMesh::allowPacketForward(const mesh::Packet* packet) {
    if (_prefs.disable_fwd) return false;
    if (packet->isRouteFlood() && packet->getPathHashCount() >= _prefs.flood_max) return false;
    return true;
}

void GopherMesh::onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret,
                                const mesh::Identity& sender,
                                uint8_t* data, size_t len) {
    if (packet->getPayloadType() != PAYLOAD_TYPE_ANON_REQ) return;
    if (len < 5) return;

    uint32_t sender_timestamp;
    memcpy(&sender_timestamp, data, 4);

    // Check if this is a Gopher request (starts with '/')
    if (data[4] == '/') {
        size_t sel_len = len - 4;
        const char* selector = (const char*)&data[4];

        // Validate selector length
        if (sel_len > MAX_SELECTOR_LEN) return;

        handleGopherRequest(sender, secret, selector, sel_len,
                            packet->path, packet->path_len,
                            packet->getPathHashSize(), true);
        return;
    }

    // Otherwise treat as login attempt (for admin CLI access)
    data[len] = 0;
    ClientInfo* client = NULL;
    if (data[8] == 0) {
        client = acl.getClient(sender.pub_key, PUB_KEY_SIZE);
    }
    if (client == NULL) {
        uint8_t perm;
        if (strcmp((char*)&data[8], _prefs.password) == 0) {
            perm = PERM_ACL_ADMIN;
        } else if (_prefs.allow_read_only) {
            perm = PERM_ACL_GUEST;
        } else {
            return;
        }
        client = acl.putClient(sender, 0);
        if (!client) return;
        if (sender_timestamp <= client->last_timestamp) return;

        client->last_timestamp = sender_timestamp;
        client->last_activity = getRTCClock()->getCurrentTime();
        client->permissions &= ~0x03;
        client->permissions |= perm;
        memcpy(client->shared_secret, secret, PUB_KEY_SIZE);
        dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);

        // Track in our client state array
        int idx = findOrCreateClient(sender, secret);
        if (idx >= 0) {
            memcpy(_clients[idx].shared_secret, secret, PUB_KEY_SIZE);
        }
    }

    if (packet->isRouteFlood()) {
        client->out_path_len = OUT_PATH_UNKNOWN;
    }

    // Send login response
    uint32_t now = getRTCClock()->getCurrentTimeUnique();
    memcpy(_reply_data, &now, 4);
    _reply_data[4] = 0; // login OK
    _reply_data[5] = 0;
    _reply_data[6] = (client->isAdmin() ? 1 : 2);
    _reply_data[7] = client->permissions;
    getRNG()->random(&_reply_data[8], 4);
    _reply_data[12] = 1;

    if (packet->isRouteFlood()) {
        mesh::Packet* path = createPathReturn(sender, client->shared_secret,
                                              packet->path, packet->path_len,
                                              PAYLOAD_TYPE_RESPONSE, _reply_data, 13);
        if (path) sendFloodReply(path, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
    } else {
        mesh::Packet* reply = createDatagram(PAYLOAD_TYPE_RESPONSE, sender,
                                             client->shared_secret, _reply_data, 13);
        if (reply) {
            if (client->out_path_len != OUT_PATH_UNKNOWN) {
                sendDirect(reply, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
            } else {
                sendFloodReply(reply, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
            }
        }
    }
}

int GopherMesh::searchPeersByHash(const uint8_t* hash) {
    int n = 0;
    for (int i = 0; i < acl.getNumClients(); i++) {
        if (acl.getClientByIdx(i)->id.isHashMatch(hash)) {
            matching_peer_indexes[n++] = i;
        }
    }
    return n;
}

void GopherMesh::getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) {
    int i = matching_peer_indexes[peer_idx];
    if (i >= 0 && i < acl.getNumClients()) {
        memcpy(dest_secret, acl.getClientByIdx(i)->shared_secret, PUB_KEY_SIZE);
    }
}

void GopherMesh::onPeerDataRecv(mesh::Packet* packet, uint8_t type,
                                int sender_idx, const uint8_t* secret,
                                uint8_t* data, size_t len) {
    int i = matching_peer_indexes[sender_idx];
    if (i < 0 || i >= acl.getNumClients()) return;
    auto client = acl.getClientByIdx(i);

    if (type == PAYLOAD_TYPE_TXT_MSG && len > 5) {
        uint32_t sender_timestamp;
        memcpy(&sender_timestamp, data, 4);
        uint8_t flags = (data[4] >> 2);

        if (flags != TXT_TYPE_PLAIN && flags != TXT_TYPE_CLI_DATA) return;
        if (sender_timestamp < client->last_timestamp) return;

        client->last_timestamp = sender_timestamp;
        client->last_activity = getRTCClock()->getCurrentTime();
        data[len] = 0;

        const char* text = (const char*)&data[5];
        if (flags == TXT_TYPE_CLI_DATA && client->isAdmin()) {
            // CLI command
            char reply[160];
            reply[0] = 0;
            _cli.handleCommand(sender_timestamp, (char*)text, reply);
            int rlen = strlen(reply);
            if (rlen > 0) {
                uint32_t now = getRTCClock()->getCurrentTimeUnique();
                memcpy(_reply_data, &now, 4);
                _reply_data[4] = (TXT_TYPE_CLI_DATA << 2);
                memcpy(&_reply_data[5], reply, rlen + 1);
                auto pkt = createDatagram(PAYLOAD_TYPE_TXT_MSG, client->id, secret, _reply_data, 5 + rlen + 1);
                if (pkt) {
                    if (client->out_path_len != OUT_PATH_UNKNOWN) {
                        sendDirect(pkt, client->out_path, client->out_path_len, SERVER_RESPONSE_DELAY);
                    } else {
                        sendFloodReply(pkt, SERVER_RESPONSE_DELAY, packet->getPathHashSize());
                    }
                }
            }
        } else if (text[0] == '/') {
            // Gopher selector
            size_t sel_len = strlen(text);
            if (sel_len > MAX_SELECTOR_LEN) sel_len = MAX_SELECTOR_LEN;

            // Track client path
            int idx = findOrCreateClient(client->id, secret);
            if (idx >= 0) {
                if (packet->isRouteFlood()) {
                    setClientPath(idx, packet->path, packet->path_len);
                } else if (client->out_path_len != OUT_PATH_UNKNOWN) {
                    setClientPath(idx, client->out_path, client->out_path_len);
                }
                memcpy(_clients[idx].shared_secret, secret, PUB_KEY_SIZE);
            }

            handleGopherRequest(client->id, secret, text, sel_len,
                                packet->path, packet->path_len,
                                packet->getPathHashSize(), false);
        }
    } else if (type == PAYLOAD_TYPE_REQ && len >= 5) {
        uint32_t sender_timestamp;
        memcpy(&sender_timestamp, data, 4);
        if (sender_timestamp < client->last_timestamp) return;
        client->last_timestamp = sender_timestamp;
        client->last_activity = getRTCClock()->getCurrentTime();
    }
}

bool GopherMesh::onPeerPathRecv(mesh::Packet* packet, int sender_idx,
                                const uint8_t* secret,
                                uint8_t* path, uint8_t path_len,
                                uint8_t extra_type, uint8_t* extra,
                                uint8_t extra_len) {
    int i = matching_peer_indexes[sender_idx];
    if (i >= 0 && i < acl.getNumClients()) {
        auto client = acl.getClientByIdx(i);
        client->out_path_len = mesh::Packet::copyPath(client->out_path, path, path_len);
        client->last_activity = getRTCClock()->getCurrentTime();
    }

    if (extra_type == PAYLOAD_TYPE_ACK && extra_len >= 4) {
        // Process ack if needed
    }
    return false;
}

void GopherMesh::onAckRecv(mesh::Packet* packet, uint32_t ack_crc) {
    // ACK handling for future use
}

// ═══════════════════════════════════════════════════════════════════════
//  Gopher Request Handling
// ═══════════════════════════════════════════════════════════════════════

void GopherMesh::handleGopherRequest(const mesh::Identity& sender,
                                      const uint8_t* secret,
                                      const char* selector, size_t sel_len,
                                      const uint8_t* path, uint8_t path_len,
                                      uint8_t path_hash_size,
                                      bool is_anon) {
    // Build a response buffer
    char response[4096];
    size_t resp_len = 0;

    // Resolve and serve the selector
    auto serveResult = fossorial::serveSelector<GopherFS, fossorial::MenuFormat::LoRa>(
        _gopher_fs, selector, sel_len,
        [&](const char* chunk, size_t chunk_len) {
            size_t to_copy = chunk_len;
            if (resp_len + to_copy >= sizeof(response)) {
                to_copy = sizeof(response) - resp_len - 1;
            }
            if (to_copy > 0) {
                memcpy(response + resp_len, chunk, to_copy);
                resp_len += to_copy;
            }
        }
    );

    if (serveResult == fossorial::ResponseType::NotFound) {
        char errbuf[128];
        resp_len = fossorial::buildErrorResponse(errbuf, sizeof(errbuf), "Not found");
        sendGopherResponse(sender, secret, errbuf, resp_len,
                           path, path_len, path_hash_size, is_anon);
        return;
    }

    // Append Gopher terminator
    if (resp_len + 3 < sizeof(response)) {
        response[resp_len++] = '.';
        response[resp_len++] = '\r';
        response[resp_len++] = '\n';
    }

    // Find out-path for response
    uint8_t out_path[MAX_PATH_SIZE];
    uint8_t out_path_len = 0;
    int idx = findOrCreateClient(const_cast<mesh::Identity&>(sender), secret);
    if (idx >= 0 && _clients[idx].has_path) {
        out_path_len = mesh::Packet::copyPath(out_path, _clients[idx].out_path, _clients[idx].out_path_len);
    }

    sendGopherResponse(sender, secret, response, resp_len,
                       out_path, out_path_len, path_hash_size, is_anon);
}

void GopherMesh::sendGopherResponse(const mesh::Identity& sender,
                                     const uint8_t* secret,
                                     const char* data, size_t len,
                                     const uint8_t* out_path, uint8_t out_path_len,
                                     uint8_t path_hash_size,
                                     bool is_anon) {
    const size_t max_text = CHUNK_MAX_DATA - 3; // 3 bytes for gopher header (marker, flags, seq)
    uint32_t base_ts = getRTCClock()->getCurrentTimeUnique();
    uint8_t seq = 0;
    size_t offset = 0;

    while (offset < len) {
        bool is_first = (offset == 0);
        size_t remaining = len - offset;
        size_t chunk_len = (remaining > max_text) ? max_text : remaining;
        bool is_last = (offset + chunk_len >= len);

        uint8_t flags;
        if (is_first && is_last) flags = GOPHER_FLAG_SOLE;
        else if (is_first)       flags = GOPHER_FLAG_FIRST;
        else if (is_last)        flags = GOPHER_FLAG_LAST;
        else                     flags = GOPHER_FLAG_MID;

        // Build packet payload: timestamp(4) + type_flags(1) + gopher_hdr(3) + content
        memcpy(_reply_data, &base_ts, 4);
        uint8_t attempt;
        getRNG()->random(&attempt, 1);
        _reply_data[4] = (TXT_TYPE_CLI_DATA << 2) | (attempt & 3);
        writeGopherHdr(&_reply_data[5], flags, seq);

        uint8_t* content_start = &_reply_data[8];
        memcpy(content_start, data + offset, chunk_len);

        size_t pkt_len = 8 + chunk_len;
        auto pkt = createDatagram(PAYLOAD_TYPE_TXT_MSG, sender, secret, _reply_data, pkt_len);
        if (pkt) {
            if (out_path_len > 0) {
                sendDirect(pkt, out_path, out_path_len, GOPHER_RESPONSE_DELAY);
            } else {
                sendFloodReply(pkt, GOPHER_RESPONSE_DELAY, path_hash_size);
            }
        }

        offset += chunk_len;
        ++seq;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Client tracking
// ═══════════════════════════════════════════════════════════════════════

int GopherMesh::findOrCreateClient(const mesh::Identity& id, const uint8_t* secret) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (memcmp(_clients[i].shared_secret, secret, PUB_KEY_SIZE) == 0) {
            return i;
        }
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!_clients[i].has_path) {
            memcpy(_clients[i].shared_secret, secret, PUB_KEY_SIZE);
            _clients[i].has_path = false;
            _clients[i].out_path_len = 0;
            return i;
        }
    }
    return -1;
}

void GopherMesh::setClientPath(int idx, const uint8_t* path, uint8_t path_len) {
    if (idx < 0 || idx >= MAX_CLIENTS) return;
    _clients[idx].out_path_len = mesh::Packet::copyPath(_clients[idx].out_path, path, path_len);
    _clients[idx].has_path = true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Advertisements
// ═══════════════════════════════════════════════════════════════════════

mesh::Packet* GopherMesh::createSelfAdvert() {
    uint8_t app_data[MAX_ADVERT_DATA_SIZE];
    uint8_t app_data_len = _cli.buildAdvertData(ADV_TYPE_GOPHER, app_data);
    return createAdvert(self_id, app_data, app_data_len);
}

void GopherMesh::sendSelfAdvertisement(int delay_millis, bool flood) {
    mesh::Packet* pkt = createSelfAdvert();
    if (pkt) {
        TransportKey scope;
        if (flood) {
            RegionEntry* r = region_map.getDefaultRegion();
            if (r) {
                region_map.getTransportKeysFor(*r, &scope, 1);
                if (!scope.isNull()) {
                    uint16_t codes[2];
                    codes[0] = scope.calcTransportCode(pkt);
                    codes[1] = 0;
                    sendFlood(pkt, codes, delay_millis, _prefs.path_hash_mode + 1);
                    return;
                }
            }
            sendFlood(pkt, delay_millis, _prefs.path_hash_mode + 1);
        } else {
            sendZeroHop(pkt, delay_millis);
        }
    }
}

void GopherMesh::updateAdvertTimer() {
    if (_prefs.advert_interval > 0) {
        next_local_advert = futureMillis((uint32_t)_prefs.advert_interval * 2 * 60 * 1000);
    } else {
        next_local_advert = 0;
    }
}

void GopherMesh::updateFloodAdvertTimer() {
    if (_prefs.flood_advert_interval > 0) {
        next_flood_advert = futureMillis((uint32_t)_prefs.flood_advert_interval * 60 * 60 * 1000);
    } else {
        next_flood_advert = 0;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Transport / Flood helpers
// ═══════════════════════════════════════════════════════════════════════

void GopherMesh::sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt,
                                  uint32_t delay_millis, uint8_t path_hash_size) {
    if (scope.isNull()) {
        sendFlood(pkt, delay_millis, path_hash_size);
    } else {
        uint16_t codes[2];
        codes[0] = scope.calcTransportCode(pkt);
        codes[1] = 0;
        sendFlood(pkt, codes, delay_millis, path_hash_size);
    }
}

void GopherMesh::sendFloodReply(mesh::Packet* packet, unsigned long delay_millis,
                                 uint8_t path_hash_size) {
    sendFlood(packet, delay_millis, path_hash_size);
}

// ═══════════════════════════════════════════════════════════════════════
//  Logging
// ═══════════════════════════════════════════════════════════════════════

File GopherMesh::openAppend(const char* fname) {
#if defined(NRF52_PLATFORM)
    return _fs->open(fname, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    return _fs->open(fname, "a");
#else
    return _fs->open(fname, "a", true);
#endif
}

const char* GopherMesh::getLogDateTime() {
    static char tmp[32];
    uint32_t now = getRTCClock()->getCurrentTime();
    DateTime dt = DateTime(now);
    sprintf(tmp, "%02d:%02d:%02d - %d/%d/%d U",
            dt.hour(), dt.minute(), dt.second(),
            dt.day(), dt.month(), dt.year());
    return tmp;
}

void GopherMesh::logRxRaw(float snr, float rssi, const uint8_t raw[], int len) {
#if MESH_PACKET_LOGGING
    Serial.print(getLogDateTime());
    Serial.print(" RAW: ");
    mesh::Utils::printHex(Serial, raw, len);
    Serial.println();
#endif
}

void GopherMesh::logRx(mesh::Packet* pkt, int len, float score) {
    if (_logging) {
        File f = openAppend(PACKET_LOG_FILE);
        if (f) {
            f.print(getLogDateTime());
            f.printf(": RX, len=%d (type=%d, route=%s, payload_len=%d) SNR=%d RSSI=%d score=%d",
                     len, pkt->getPayloadType(),
                     pkt->isRouteDirect() ? "D" : "F", pkt->payload_len,
                     (int)radio_driver.getLastSNR(), (int)radio_driver.getLastRSSI(),
                     (int)(score * 1000));
            if (pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
                f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
            } else {
                f.printf("\n");
            }
            f.close();
        }
    }
}

void GopherMesh::logTx(mesh::Packet* pkt, int len) {
    if (_logging) {
        File f = openAppend(PACKET_LOG_FILE);
        if (f) {
            f.print(getLogDateTime());
            f.printf(": TX, len=%d (type=%d, route=%s, payload_len=%d)",
                     len, pkt->getPayloadType(),
                     pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
            if (pkt->getPayloadType() == PAYLOAD_TYPE_TXT_MSG) {
                f.printf(" [%02X -> %02X]\n", (uint32_t)pkt->payload[1], (uint32_t)pkt->payload[0]);
            } else {
                f.printf("\n");
            }
            f.close();
        }
    }
}

void GopherMesh::logTxFail(mesh::Packet* pkt, int len) {
    if (_logging) {
        File f = openAppend(PACKET_LOG_FILE);
        if (f) {
            f.print(getLogDateTime());
            f.printf(": TX FAIL!, len=%d (type=%d, route=%s, payload_len=%d)\n",
                     len, pkt->getPayloadType(),
                     pkt->isRouteDirect() ? "D" : "F", pkt->payload_len);
            f.close();
        }
    }
}

int GopherMesh::calcRxDelay(float score, uint32_t air_time) const {
    if (_prefs.rx_delay_base <= 0.0f) return 0;
    return (int)((pow(_prefs.rx_delay_base, 0.85f - score) - 1.0) * air_time);
}

uint32_t GopherMesh::getRetransmitDelay(const mesh::Packet* packet) {
    uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.tx_delay_factor);
    return getRNG()->nextInt(0, 5 * t + 1);
}

uint32_t GopherMesh::getDirectRetransmitDelay(const mesh::Packet* packet) {
    uint32_t t = (_radio->getEstAirtimeFor(packet->getPathByteLen() + packet->payload_len + 2) * _prefs.direct_tx_delay_factor);
    return getRNG()->nextInt(0, 5 * t + 1);
}

// ═══════════════════════════════════════════════════════════════════════
//  Format helpers (CLI callbacks)
// ═══════════════════════════════════════════════════════════════════════

void GopherMesh::formatStatsReply(char* reply) {
    sprintf(reply, "uptime:%lu secs, batt:%u mV, tx_q:%u",
            uptime_millis / 1000, board.getBattMilliVolts(),
            _mgr->getOutboundTotal());
}

void GopherMesh::formatRadioStatsReply(char* reply) {
    sprintf(reply, "rssi:%d, snr:%d, noise:%d",
            (int)radio_driver.getLastRSSI(), (int)radio_driver.getLastSNR(),
            (int)_radio->getNoiseFloor());
}

void GopherMesh::formatPacketStatsReply(char* reply) {
    sprintf(reply, "rx:%lu tx:%lu flood:%lu/%lu direct:%lu/%lu",
            radio_driver.getPacketsRecv(), radio_driver.getPacketsSent(),
            getNumRecvFlood(), getNumSentFlood(),
            getNumRecvDirect(), getNumSentDirect());
}

// ═══════════════════════════════════════════════════════════════════════
//  CommonCLICallbacks
// ═══════════════════════════════════════════════════════════════════════

bool GopherMesh::formatFileSystem() {
#if defined(NRF52_PLATFORM)
    return InternalFS.format();
#elif defined(RP2040_PLATFORM)
    return LittleFS.format();
#elif defined(ESP32)
    return SPIFFS.format();
#else
    return false;
#endif
}

void GopherMesh::dumpLogFile() {
#if defined(RP2040_PLATFORM)
    File f = _fs->open(PACKET_LOG_FILE, "r");
#else
    File f = _fs->open(PACKET_LOG_FILE);
#endif
    if (f) {
        while (f.available()) {
            int c = f.read();
            if (c < 0) break;
            Serial.print((char)c);
        }
        f.close();
    }
}

void GopherMesh::setTxPower(int8_t power_dbm) {
    radio_set_tx_power(power_dbm);
}

void GopherMesh::saveIdentity(const mesh::LocalIdentity& new_id) {
    IdentityStore store(*_fs, "");
    store.save("_main", new_id);
}

void GopherMesh::clearStats() {
    resetStats();
}

void GopherMesh::applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) {
    // Not implemented yet
}
