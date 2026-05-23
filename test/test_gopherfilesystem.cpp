#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "fossorial/GopherFileSystem.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, expr) do { \
    if (!(expr)) { \
        printf("  FAIL: %s\n", name); \
        ++tests_failed; \
    } else { \
        printf("  PASS: %s\n", name); \
        ++tests_passed; \
    } \
} while(0)

#define TEST_STR(name, actual, expected) do { \
    if (strcmp((actual), (expected)) != 0) { \
        printf("  FAIL: %s (got \"%s\", expected \"%s\")\n", name, (actual), (expected)); \
        ++tests_failed; \
    } else { \
        printf("  PASS: %s\n", name); \
        ++tests_passed; \
    } \
} while(0)

// ─── Host filesystem adapter ──────────────────────────────────────────────

struct HostFileSystem {
    bool exists(const char* path) const {
        struct stat st;
        return stat(path, &st) == 0;
    }

    bool isDir(const char* path) const {
        struct stat st;
        if (stat(path, &st) != 0) return false;
        return S_ISDIR(st.st_mode);
    }

    int readFile(const char* path, char* buf, int bufsz) const {
        FILE* f = fopen(path, "rb");
        if (!f) return -1;
        int n = fread(buf, 1, bufsz - 1, f);
        fclose(f);
        buf[n] = 0;
        return n;
    }

    bool readDir(const char* path, int& idx, char* name, int namesz, bool& is_dir) const {
        static DIR* dir = nullptr;
        static int dir_ref = -1;

        if (idx == 0) {
            if (dir) closedir(dir);
            dir = opendir(path);
            dir_ref = 0;
            if (!dir) return false;
        }

        if (dir_ref != 0 || !dir) return false;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Skip . and ..
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            size_t nlen = strlen(entry->d_name);
            if (nlen >= (size_t)namesz) continue;

            for (size_t i = 0; i < nlen && i < (size_t)namesz - 1; ++i)
                name[i] = entry->d_name[i];
            name[nlen] = 0;

            // Build full path for stat
            char full[512];
            size_t plen = strlen(path);
            for (size_t i = 0; i < plen && i < sizeof(full)-1; ++i) full[i] = path[i];
            size_t pos = plen;
            if (pos > 0 && full[pos-1] != '/') full[pos++] = '/';
            for (size_t i = 0; entry->d_name[i] && pos < sizeof(full)-1; ++i)
                full[pos++] = entry->d_name[i];
            full[pos] = 0;

            struct stat st;
            is_dir = (stat(full, &st) == 0 && S_ISDIR(st.st_mode));

            ++idx;
            return true;
        }

        closedir(dir);
        dir = nullptr;
        dir_ref = -1;
        return false;
    }
};

// ─── Test helpers ─────────────────────────────────────────────────────────

static void createTestFiles() {
    // Root gopher directory
    mkdir("/tmp/fossorial_test", 0755);

    // gophermap
    FILE* f = fopen("/tmp/fossorial_test/gophermap", "w");
    fprintf(f, "i Fossorial Test Gopher Site\n");
    fprintf(f, "1Documents\t/docs\n");
    fprintf(f, "0Welcome\t/welcome.txt\n");
    fprintf(f, "1Weather\t/weather\n");
    fprintf(f, "0Network Map\t/map.png\n");
    fclose(f);

    // welcome.txt
    f = fopen("/tmp/fossorial_test/welcome.txt", "w");
    fprintf(f, "Welcome to the Fossorial Gopher site!\n");
    fprintf(f, "This content is served over LoRa mesh.\n");
    fclose(f);

    // docs directory
    mkdir("/tmp/fossorial_test/docs", 0755);
    f = fopen("/tmp/fossorial_test/docs/gophermap", "w");
    fprintf(f, "i Documents\n");
    fprintf(f, "0Readme\t/readme.txt\n");
    fprintf(f, "0Notes\t/notes.txt\n");
    fclose(f);

    f = fopen("/tmp/fossorial_test/docs/readme.txt", "w");
    fprintf(f, "This is the readme.\n");
    fclose(f);

    f = fopen("/tmp/fossorial_test/docs/notes.txt", "w");
    fprintf(f, "Meeting notes for May 2026.\n");
    fclose(f);

    // weather directory (no gophermap — auto-generated)
    mkdir("/tmp/fossorial_test/weather", 0755);
    f = fopen("/tmp/fossorial_test/weather/today.txt", "w");
    fprintf(f, "Weather: Sunny, 22C\n");
    fclose(f);
    f = fopen("/tmp/fossorial_test/weather/tomorrow.txt", "w");
    fprintf(f, "Weather: Cloudy, 18C\n");
    fclose(f);
}

static void cleanupTestFiles() {
    unlink("/tmp/fossorial_test/weather/today.txt");
    unlink("/tmp/fossorial_test/weather/tomorrow.txt");
    rmdir("/tmp/fossorial_test/weather");
    unlink("/tmp/fossorial_test/docs/readme.txt");
    unlink("/tmp/fossorial_test/docs/notes.txt");
    unlink("/tmp/fossorial_test/docs/gophermap");
    rmdir("/tmp/fossorial_test/docs");
    unlink("/tmp/fossorial_test/welcome.txt");
    unlink("/tmp/fossorial_test/map.png");
    unlink("/tmp/fossorial_test/gophermap");
    rmdir("/tmp/fossorial_test");
}

// ─── Tests ────────────────────────────────────────────────────────────────

static void test_selector_to_path() {
    printf("[Selector to Path]\n");

    char buf[256];

    size_t n = fossorial::selectorToPath(buf, sizeof(buf), "", 0);
    TEST("empty selector maps to root", n > 0);
    TEST_STR("root path", buf, FOSSORIAL_GOPHER_ROOT);

    n = fossorial::selectorToPath(buf, sizeof(buf), "/docs", 5);
    TEST("selector to subdir", n > 0);
    TEST("ends with /docs", strstr(buf, "/docs") != nullptr);

    n = fossorial::selectorToPath(buf, sizeof(buf), "/docs/readme.txt", 16);
    TEST("selector to file", n > 0);
    TEST("ends with readme.txt", strstr(buf, "readme.txt") != nullptr);

    // C-string version
    n = fossorial::selectorToPath(buf, sizeof(buf), "/welcome.txt");
    TEST("C-string selector path", n > 0);
}

static void test_resolve_selector() {
    printf("[Resolve Selector]\n");

    HostFileSystem fs;

    // Root (empty selector)
    auto rs = fossorial::resolveSelector(fs, "", 0);
    TEST("root resolves", rs.result != fossorial::SelectorResult::NotFound);
    TEST("root has gophermap", rs.result == fossorial::SelectorResult::Gophermap);

    // Existing file
    rs = fossorial::resolveSelector(fs, "/welcome.txt", 12);
    TEST("welcome.txt found", rs.result == fossorial::SelectorResult::File);

    // Existing directory with gophermap
    rs = fossorial::resolveSelector(fs, "/docs", 5);
    TEST("docs resolves", rs.result != fossorial::SelectorResult::NotFound);

    // Nonexistent
    rs = fossorial::resolveSelector(fs, "/nonexistent", 12);
    TEST("nonexistent not found", rs.result == fossorial::SelectorResult::NotFound);

    // Directory without gophermap
    rs = fossorial::resolveSelector(fs, "/weather", 8);
    TEST("weather resolves", rs.result != fossorial::SelectorResult::NotFound);
}

struct StringOutput {
    char* buf;
    size_t capacity;
    size_t pos;

    void operator()(const char* data, size_t len) {
        if (pos + len < capacity) {
            memcpy(buf + pos, data, len);
            pos += len;
        }
    }
};

static void test_serve_gophermap() {
    printf("[Serve Gophermap]\n");

    HostFileSystem fs;
    char output[4096];
    StringOutput out{output, sizeof(output), 0};

    bool ok = fossorial::serveGophermap<HostFileSystem, fossorial::MenuFormat::LoRa>(
        fs, "/tmp/fossorial_test/gophermap", "/tmp/fossorial_test", out);
    output[out.pos] = 0;

    TEST("gophermap served", ok);
    TEST("output has content", out.pos > 0);

    // Check for specific lines
    TEST("contains info line", strstr(output, "i Fossorial Test Gopher Site") != nullptr);
    TEST("contains docs link", strstr(output, "1Documents\t/docs\t") != nullptr);
    TEST("contains welcome link", strstr(output, "0Welcome\t/welcome.txt\t") != nullptr);
}

static void test_serve_file() {
    printf("[Serve File]\n");

    HostFileSystem fs;
    char output[4096];
    StringOutput out{output, sizeof(output), 0};

    auto rt = fossorial::serveSelector<HostFileSystem, fossorial::MenuFormat::LoRa>(
        fs, "/welcome.txt", 12, out);
    output[out.pos] = 0;

    TEST("file served", rt == fossorial::ResponseType::File);
    TEST("file content present", out.pos > 0);
    TEST("contains welcome", strstr(output, "Welcome to the Fossorial") != nullptr);
}

static void test_serve_directory_autogen() {
    printf("[Serve Directory (auto-generated)]\n");

    HostFileSystem fs;
    char output[4096];
    StringOutput out{output, sizeof(output), 0};

    auto rt = fossorial::serveSelector<HostFileSystem, fossorial::MenuFormat::LoRa>(
        fs, "/weather", 8, out);
    output[out.pos] = 0;

    TEST("weather served", rt == fossorial::ResponseType::Menu);
    TEST("weather has content", out.pos > 0);
    TEST("contains today.txt", strstr(output, "today.txt") != nullptr);
    TEST("contains tomorrow.txt", strstr(output, "tomorrow.txt") != nullptr);
}

static void test_serve_not_found() {
    printf("[Serve Not Found]\n");

    HostFileSystem fs;
    char output[4096];
    StringOutput out{output, sizeof(output), 0};

    auto rt = fossorial::serveSelector<HostFileSystem, fossorial::MenuFormat::LoRa>(
        fs, "/nonexistent", 12, out);

    TEST("not found returns NotFound", rt == fossorial::ResponseType::NotFound);
}

// ─── Main ─────────────────────────────────────────────────────────────────

int main() {
    printf("=== Fossorial GopherFileSystem Test Suite ===\n\n");

    createTestFiles();

    test_selector_to_path();
    test_resolve_selector();
    test_serve_gophermap();
    test_serve_file();
    test_serve_directory_autogen();
    test_serve_not_found();

    cleanupTestFiles();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
