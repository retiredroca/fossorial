#pragma once

#include "FSCommon.h"
#include "mesh/SinglePortModule.h"
#include "fossorial/GopherCore.h"
#include "fossorial/GopherFileSystem.h"

#define FOSSORIAL_MAX_SELECTOR  128
#define FOSSORIAL_CHUNK_SIZE    180
#define FOSSORIAL_GOPHER_ROOT   "/gopher"

// ─── Response chunk wire format ──────────────────────────────────────────
//
// Byte 0: 'G' (0x47) — marker
// Byte 1: flags    — 0=Sole, 1=First, 2=Mid, 4=Last
// Byte 2: seq      — sequence number (0–255)
// Byte 3+:         — Gopher content

#define FOSSORIAL_MARKER  'G'

// ─── Meshtastic filesystem adapter ────────────────────────────────────────

struct MeshtasticFS {
    bool exists(const char* path) const {
        return FSCom.exists(path);
    }

    bool isDir(const char* path) const {
        File f = FSCom.open(path, FILE_O_READ);
        if (!f) return false;
        bool dir = f.isDirectory();
        f.close();
        return dir;
    }

    int readFile(const char* path, char* buf, int bufsz) const {
        File f = FSCom.open(path, FILE_O_READ);
        if (!f) return -1;
        int n = f.read((uint8_t*)buf, bufsz - 1);
        f.close();
        if (n < 0) return 0;
        buf[n] = 0;
        return n;
    }

    bool readDir(const char* path, int& idx, char* name, int namesz, bool& is_dir) const {
        File dir = FSCom.open(path, FILE_O_READ);
        if (!dir || !dir.isDirectory()) {
            if (dir) dir.close();
            return false;
        }
        dir.rewindDirectory();

        int cur = 0;
        File entry;
        while (true) {
            entry = dir.openNextFile();
            if (!entry) { dir.close(); return false; }
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
};

// ─── Fossorial Meshtastic Module ─────────────────────────────────────────

class FossorialModule : public SinglePortModule
{
  public:
    FossorialModule();

  protected:
    ProcessMessage handleReceived(const meshtastic_MeshPacket &p) override;

  private:
    MeshtasticFS _fs;

    // Build and send Gopher response for a selector
    void serveSelector(const meshtastic_MeshPacket &req,
                       const char *selector, size_t sel_len);

    // Send a single response chunk to the requesting node
    void sendGopherChunk(const meshtastic_MeshPacket &req,
                          const char *data, size_t len,
                          uint8_t flags, uint8_t seq);
};
