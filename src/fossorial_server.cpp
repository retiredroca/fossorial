// fossorial-server -- standalone TCP Gopher daemon

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>

#include "fossorial/GopherCore.h"
#include "fossorial/GopherFileSystem.h"

// ─── POSIX filesystem adapter ────────────────────────────────────────────

struct HostFS {
    bool exists(const char* path) const {
        struct stat st;
        return stat(path, &st) == 0;
    }

    bool isDir(const char* path) const {
        struct stat st;
        return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    }

    int readFile(const char* path, char* buf, int bufsz) const {
        FILE* f = fopen(path, "rb");
        if (!f) return -1;
        int n = fread(buf, 1, bufsz - 1, f);
        fclose(f);
        buf[n < 0 ? 0 : n] = 0;
        return n < 0 ? 0 : n;
    }

    bool readDir(const char* path, int& idx, char* name, int namesz, bool& is_dir) const {
        if (idx == 0) {
            if (_dir) closedir(_dir);
            _dir = opendir(path);
            if (!_dir) return false;
        }
        if (!_dir) return false;

        struct dirent* entry;
        while ((entry = readdir(_dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            size_t nlen = strlen(entry->d_name);
            if (nlen >= (size_t)namesz) continue;
            memcpy(name, entry->d_name, nlen);
            name[nlen] = 0;

            char full[512];
            size_t plen = strlen(path);
            size_t pos = plen < sizeof(full) ? plen : sizeof(full) - 1;
            memcpy(full, path, pos);
            if (pos > 0 && full[pos-1] != '/' && pos < sizeof(full)-1)
                full[pos++] = '/';
            size_t ni = 0;
            while (name[ni] && pos < sizeof(full)-1) full[pos++] = name[ni++];
            full[pos] = 0;

            struct stat st;
            is_dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));
            ++idx;
            return true;
        }

        closedir(_dir);
        _dir = nullptr;
        return false;
    }

    ~HostFS() { if (_dir) closedir(_dir); }

private:
    mutable DIR* _dir = nullptr;
};

// ─── Buffered output ─────────────────────────────────────────────────────

struct BufferOutput {
    char* buf;
    size_t cap;
    size_t pos;

    void operator()(const char* data, size_t len) {
        size_t room = pos < cap ? cap - pos : 0;
        size_t n = len < room ? len : room;
        memcpy(buf + pos, data, n);
        pos += n;
    }
};

// ─── Build response ──────────────────────────────────────────────────────

static size_t buildResponse(char* buf, size_t bufsz,
                             const HostFS& fs,
                             const char* selector, size_t sel_len)
{
    BufferOutput out{buf, bufsz, 0};
    char line[256];

    // Banner
    size_t n = fossorial::buildMenuLine<fossorial::MenuFormat::Standard>(
        line, sizeof(line),
        fossorial::GopherType::Info,
        " Fossorial Gopher Server",
        "fake", "error.host", 1);
    out(line, n);

    // Serve the selector
    auto rt = fossorial::serveSelector<HostFS, fossorial::MenuFormat::Standard>(
        fs, selector, sel_len, out);

    if (rt == fossorial::ResponseType::NotFound) {
        out.pos = 0;
        return fossorial::buildErrorResponse(buf, bufsz, "Not found");
    }

    // Gopher terminator: ".\r\n" on its own line
    if (out.pos + 3 <= out.cap) {
        out.buf[out.pos++] = '.';
        out.buf[out.pos++] = '\r';
        out.buf[out.pos++] = '\n';
    }
    if (out.pos < out.cap) out.buf[out.pos] = 0;
    return out.pos;
}

// ─── Handle client ───────────────────────────────────────────────────────

static void handleClient(int fd, const HostFS& fs) {
    char selector[FOSSORIAL_MAX_SELECTOR + 2];
    ssize_t nread = read(fd, selector, sizeof(selector) - 1);
    if (nread <= 0) { close(fd); return; }
    selector[nread] = 0;

    // Trim trailing CR/LF/whitespace
    size_t len = (size_t)nread;
    while (len > 0 && (selector[len-1] == '\r' || selector[len-1] == '\n' ||
                       selector[len-1] == ' ' || selector[len-1] == '\t'))
        --len;
    selector[len] = 0;

    printf("  <- \"%s\"\n", selector);

    if (!fossorial::validateSelector(selector, len)) {
        write(fd, "3Bad selector\r\n.\r\n", 18);
        close(fd);
        return;
    }

    // If selector is empty, use root
    if (len == 0) { selector[0] = '/'; selector[1] = 0; len = 1; }

    char response[32768];
    size_t resp_len = buildResponse(response, sizeof(response), fs, selector, len);

    size_t written = 0;
    while (written < resp_len) {
        ssize_t n = write(fd, response + written, resp_len - written);
        if (n <= 0) break;
        written += (size_t)n;
    }

    printf("  -> %zu bytes\n", resp_len);
    close(fd);
}

// ─── Create default gopher site ──────────────────────────────────────────

static void createDefaultSite(const char* root) {
    mkdir(root, 0755);
    char path[512];

    snprintf(path, sizeof(path), "%s/gophermap", root);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, " Fossorial Gopher Site\n");
    fprintf(f, " =====================\n");
    fprintf(f, " \n");
    fprintf(f, "1Documents\t/docs\n");
    fprintf(f, "0Welcome\t/welcome.txt\n");
    fprintf(f, "0About\t/about.txt\n");
    fclose(f);

    snprintf(path, sizeof(path), "%s/welcome.txt", root);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "Welcome to Fossorial!\n\n");
        fprintf(f, "This is your Gopher site served over:\n");
        fprintf(f, "  - TCP (standard Gopher protocol)\n");
        fprintf(f, "  - LoRa mesh (via MeshCore, coming soon)\n\n");
        fprintf(f, "Create a 'gophermap' file in any directory\n");
        fprintf(f, "to customize what appears in the menu.\n");
        fclose(f);
    }

    snprintf(path, sizeof(path), "%s/about.txt", root);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "Fossorial v0.1.0\n");
        fprintf(f, "Gopher protocol server for LoRa mesh networks.\n");
        fprintf(f, "Project: https://github.com/user/fossorial\n");
        fclose(f);
    }

    snprintf(path, sizeof(path), "%s/docs", root);
    mkdir(path, 0755);

    snprintf(path, sizeof(path), "%s/docs/gophermap", root);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, " Documents\n");
        fprintf(f, " ---------\n");
        fprintf(f, "0Readme\t/docs/readme.txt\n");
        fprintf(f, "0Notes\t/docs/notes.txt\n");
        fclose(f);
    }

    snprintf(path, sizeof(path), "%s/docs/readme.txt", root);
    f = fopen(path, "w");
    if (f) { fprintf(f, "Fossorial README\n\nPlace your documentation here.\n"); fclose(f); }

    snprintf(path, sizeof(path), "%s/docs/notes.txt", root);
    f = fopen(path, "w");
    if (f) { fprintf(f, "Meeting notes and other documents.\n"); fclose(f); }

    printf("  Created default Gopher site in: %s\n", root);
}

// ─── Main ────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    int port = 7070;
    const char* gopher_root = "./gopher-site";

    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) gopher_root = argv[2];

    printf("Fossorial Gopher Server v0.1.0\n");
    printf("  Port: %d\n", port);
    printf("  Root: %s\n", gopher_root);
    printf("  LoRa chunk size: %d bytes\n\n", FOSSORIAL_CHUNK_SIZE);

    // chdir so FOSSORIAL_GOPHER_ROOT='"."' resolves correctly
    if (chdir(gopher_root) != 0) {
        createDefaultSite(gopher_root);
        if (chdir(gopher_root) != 0) {
            perror("chdir");
            return 1;
        }
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(fd, 5) < 0) { perror("listen"); return 1; }

    printf("Listening on port %d...\n\n", port);

    HostFS fs;

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client = accept(fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client < 0) { perror("accept"); continue; }

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        printf("Connect from %s:%d\n", ip, ntohs(client_addr.sin_port));

        handleClient(client, fs);

        // HostFS destructor cleans up _dir between connections
    }

    close(fd);
    return 0;
}
