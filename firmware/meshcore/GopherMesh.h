#pragma once

#include <Arduino.h>
#include <Mesh.h>

#if defined(NRF52_PLATFORM)
  #include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
#endif

#include <helpers/ArduinoHelpers.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/IdentityStore.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/CommonCLI.h>
#include <helpers/StatsFormatHelper.h>
#include <helpers/ClientACL.h>
#include <helpers/RegionMap.h>
#include <RTClib.h>
#include <target.h>

#include <fossorial/GopherCore.h>
#include <fossorial/GopherFileSystem.h>

#define FIRMWARE_ROLE  "fossorial"
#define PACKET_LOG_FILE "/packet_log"

#define MAX_SELECTOR_LEN    128
#define CHUNK_MAX_DATA      (MAX_PACKET_PAYLOAD - 9)

// Response chunk flags
#define GOPHER_FLAG_SOLE    0x00
#define GOPHER_FLAG_FIRST   0x01
#define GOPHER_FLAG_MID     0x02
#define GOPHER_FLAG_LAST    0x04

// ─── MeshCore ↔ Fossorial FS Adapter ───────────────────────────────────

struct GopherFS {
    FILESYSTEM* fs;

    bool exists(const char* path) const { return fs->exists(path); }

    bool isDir(const char* path) const {
        File f = fs->open(path, "r");
        if (!f) return false;
        bool dir = f.isDirectory();
        f.close();
        return dir;
    }

    int readFile(const char* path, char* buf, int bufsz) const {
        File f = fs->open(path, "r");
        if (!f) return -1;
        int n = f.read((uint8_t*)buf, bufsz - 1);
        f.close();
        if (n < 0) return 0;
        buf[n] = 0;
        return n;
    }

    bool readDir(const char* path, int& idx, char* name, int namesz, bool& is_dir) const {
        // Stateless: open dir each call, advance to idx-th entry
        File dir = fs->open(path, "r");
        if (!dir || !dir.isDirectory()) {
            if (dir) dir.close();
            return false;
        }
        dir.rewindDirectory();

        int cur = 0;
        File entry;
        while (true) {
            entry = dir.openNextFile();
            if (!entry) {
                dir.close();
                return false;
            }
            const char* ename = entry.name();
            if (!ename || ename[0] == 0 ||
                strcmp(ename, ".") == 0 || strcmp(ename, "..") == 0) {
                entry.close();
                continue;
            }
            if (cur == idx) break;
            entry.close();
            cur++;
        }

        size_t nlen = strlen(entry.name());
        if (nlen >= (size_t)namesz) { entry.close(); dir.close(); return false; }
        memcpy(name, entry.name(), nlen);
        name[nlen] = 0;
        is_dir = entry.isDirectory();
        entry.close();
        dir.close();
        ++idx;
        return true;
    }

    GopherFS() : fs(nullptr) {}
    void init(FILESYSTEM* f) { fs = f; }
};

class GopherMesh : public mesh::Mesh, public CommonCLICallbacks {
public:
    GopherMesh(mesh::MainBoard& board, mesh::Radio& radio,
               mesh::MillisecondClock& ms, mesh::RNG& rng,
               mesh::RTCClock& rtc, mesh::MeshTables& tables);
    void begin(FILESYSTEM* fs);
    void loop();

    // CommonCLICallbacks
    void savePrefs() override { _cli.savePrefs(_fs); }
    const char* getFirmwareVer() override { return FIRMWARE_VERSION; }
    const char* getBuildDate() override { return FIRMWARE_BUILD_DATE; }
    const char* getRole() override { return FIRMWARE_ROLE; }
    bool formatFileSystem() override;
    void sendSelfAdvertisement(int delay_millis, bool flood) override;
    void updateAdvertTimer() override;
    void updateFloodAdvertTimer() override;
    void setLoggingOn(bool enable) override { _logging = enable; }
    void eraseLogFile() override { _fs->remove(PACKET_LOG_FILE); }
    void dumpLogFile() override;
    void setTxPower(int8_t power_dbm) override;
    void formatNeighborsReply(char* reply) override { strcpy(reply, "not supported"); }
    void formatStatsReply(char* reply) override;
    void formatRadioStatsReply(char* reply) override;
    void formatPacketStatsReply(char* reply) override;
    mesh::LocalIdentity& getSelfId() override { return self_id; }
    void saveIdentity(const mesh::LocalIdentity& new_id) override;
    void clearStats() override;
    void applyTempRadioParams(float freq, float bw, uint8_t sf, uint8_t cr, int timeout_mins) override;

    NodePrefs* getNodePrefs() { return &_prefs; }
    void handleCommand(uint32_t sender_timestamp, char* command, char* reply) {
        _cli.handleCommand(sender_timestamp, command, reply);
    }

protected:
    // Mesh overrides
    bool allowPacketForward(const mesh::Packet* packet) override;
    void onAnonDataRecv(mesh::Packet* packet, const uint8_t* secret,
                        const mesh::Identity& sender,
                        uint8_t* data, size_t len) override;
    int searchPeersByHash(const uint8_t* hash) override;
    void getPeerSharedSecret(uint8_t* dest_secret, int peer_idx) override;
    void onPeerDataRecv(mesh::Packet* packet, uint8_t type,
                        int sender_idx, const uint8_t* secret,
                        uint8_t* data, size_t len) override;
    bool onPeerPathRecv(mesh::Packet* packet, int sender_idx,
                        const uint8_t* secret,
                        uint8_t* path, uint8_t path_len,
                        uint8_t extra_type, uint8_t* extra,
                        uint8_t extra_len) override;
    void onAckRecv(mesh::Packet* packet, uint32_t ack_crc) override;

    // Logging
    void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
    void logRx(mesh::Packet* pkt, int len, float score) override;
    void logTx(mesh::Packet* pkt, int len) override;
    void logTxFail(mesh::Packet* pkt, int len) override;
    int calcRxDelay(float score, uint32_t air_time) const override;
    uint32_t getRetransmitDelay(const mesh::Packet* packet) override;
    uint32_t getDirectRetransmitDelay(const mesh::Packet* packet) override;
    const char* getLogDateTime() override;

private:
    FILESYSTEM* _fs;
    GopherFS _gopher_fs;
    uint8_t _reply_data[MAX_PACKET_PAYLOAD];
    uint32_t last_millis;
    uint64_t uptime_millis;
    unsigned long next_local_advert, next_flood_advert;
    bool _logging;

    NodePrefs _prefs;
    TransportKeyStore key_store;
    RegionMap region_map, temp_map;
    ClientACL acl;
    CommonCLI _cli;
    unsigned long dirty_contacts_expiry;
    int matching_peer_indexes[MAX_CLIENTS];

    // Gopher serving state
    struct ClientState {
        uint8_t out_path[MAX_PATH_SIZE];
        uint8_t out_path_len;
        uint8_t shared_secret[PUB_KEY_SIZE];
        bool has_path;
    };
    ClientState _clients[MAX_CLIENTS];

    // Internal helpers
    void sendFloodReply(mesh::Packet* packet, unsigned long delay_millis, uint8_t path_hash_size);
    void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis, uint8_t path_hash_size);
    mesh::Packet* createSelfAdvert();
    File openAppend(const char* fname);

    // Gopher dispatch
    void handleGopherRequest(const mesh::Identity& sender,
                              const uint8_t* secret,
                              const char* selector, size_t sel_len,
                              const uint8_t* path, uint8_t path_len,
                              uint8_t path_hash_size,
                              bool is_anon);

    void sendGopherResponse(const mesh::Identity& sender,
                            const uint8_t* secret,
                            const char* data, size_t len,
                            const uint8_t* out_path, uint8_t out_path_len,
                            uint8_t path_hash_size,
                            bool is_anon);

    // Track client for path responses
    int findOrCreateClient(const mesh::Identity& id, const uint8_t* secret);
    void setClientPath(int idx, const uint8_t* path, uint8_t path_len);
};
