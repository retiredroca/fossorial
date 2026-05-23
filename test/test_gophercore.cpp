#include <stdio.h>
#include <string.h>
#include "fossorial/GopherCore.h"

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

#define TEST_EQ(name, actual, expected) do { \
    if ((actual) != (expected)) { \
        printf("  FAIL: %s (got %zu, expected %zu)\n", name, (size_t)(actual), (size_t)(expected)); \
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

// ─── Test: Selector Validation ────────────────────────────────────────────

static void test_selector_validation() {
    printf("[Selector Validation]\n");

    TEST("valid simple selector", fossorial::validateSelector("hello", 5));
    TEST("valid empty selector", fossorial::validateSelector("", 0));
    TEST("valid selector at max length", fossorial::validateSelector("", 0)); // placeholder

    // Max length test
    char long_sel[260];
    memset(long_sel, 'a', 255);
    long_sel[255] = 0;
    TEST("valid selector exactly 255", fossorial::validateSelector(long_sel, 255));
    TEST("invalid selector > 255", !fossorial::validateSelector(long_sel, 256));

    // Invalid bytes
    TEST("rejects TAB", !fossorial::validateSelector("hello\tworld", 11));
    TEST("rejects LF",  !fossorial::validateSelector("hello\nworld", 11));
    TEST("rejects CR",  !fossorial::validateSelector("hello\rworld", 11));

    // Null string
    TEST("null string is invalid", !fossorial::validateSelector((const char*)nullptr));

    TEST("valid C-string with path", fossorial::validateSelector("/docs/notes.txt"));
}

// ─── Test: GopherType ─────────────────────────────────────────────────────

static void test_gopher_types() {
    printf("[GopherType]\n");

    TEST("GOPHER_TYPES_COUNT > 0", fossorial::GOPHER_TYPES_COUNT > 0);

    TEST("fromChar '0' is TextFile",
         fossorial::gopherTypeFromChar('0') == fossorial::GopherType::TextFile);
    TEST("fromChar '1' is Directory",
         fossorial::gopherTypeFromChar('1') == fossorial::GopherType::Directory);
    TEST("fromChar 'i' is Info",
         fossorial::gopherTypeFromChar('i') == fossorial::GopherType::Info);
    TEST("fromChar 'L' is LoRaMenu",
         fossorial::gopherTypeFromChar('L') == fossorial::GopherType::LoRaMenu);

    TEST("typeName for '0'",
         strcmp(fossorial::gopherTypeName(fossorial::GopherType::TextFile), "Text file") == 0);
    TEST("typeName for '1'",
         strcmp(fossorial::gopherTypeName(fossorial::GopherType::Directory), "Directory") == 0);

    TEST("isBinary true for '9'",
         fossorial::gopherTypeIsBinary(fossorial::GopherType::Binary));
    TEST("isBinary false for '0'",
         !fossorial::gopherTypeIsBinary(fossorial::GopherType::TextFile));
}

// ─── Test: Path Utilities ─────────────────────────────────────────────────

static void test_path_utils() {
    printf("[Path Utilities]\n");

    char buf[256];

    size_t n = fossorial::normalizePath(buf, sizeof(buf), "/base", 5, "sub/file", 8);
    TEST("normalise relative", n > 0);
    TEST_STR("relative result", buf, "/base/sub/file");

    // Absolute path ignores base
    n = fossorial::normalizePath(buf, sizeof(buf), "/base", 4, "/abs/path", 9);
    TEST("absolute overrides base", n > 0);
    TEST_STR("absolute result", buf, "/abs/path");

    // ".." goes up
    n = fossorial::normalizePath(buf, sizeof(buf), "/a/b/c", 6, "../d", 4);
    TEST("dotdot goes up", n > 0);
    TEST_STR("dotdot result", buf, "/a/b/d");

    // "." is no-op
    n = fossorial::normalizePath(buf, sizeof(buf), "/root", 5, "./file", 6);
    TEST_STR("dot is noop", buf, "/root/file");

    // Empty relative returns base
    n = fossorial::normalizePath(buf, sizeof(buf), "/root", 5, "", 0);
    TEST_STR("empty rel returns base", buf, "/root");

    // C-string version
    n = fossorial::normalizePath(buf, sizeof(buf), "/base", "sub/file");
    TEST_STR("c-string relative", buf, "/base/sub/file");
}

// ─── Test: Display Truncation ─────────────────────────────────────────────

static void test_display_truncation() {
    printf("[Display Truncation]\n");

    char buf[80];

    size_t n = fossorial::truncateDisplay(buf, sizeof(buf), "short", 5);
    TEST("short string", n == 5);
    TEST_STR("short result", buf, "short");

    n = fossorial::truncateDisplay(buf, sizeof(buf), "this is a very long display string that should be truncated to seventy characters or less");
    TEST("truncation returns FOSSORIAL_MAX_DISPLAY", n == 70);
    TEST("truncated length correct", strlen(buf) == 70);

    n = fossorial::truncateDisplay(buf, sizeof(buf), (const char*)nullptr);
    TEST("null display returns 0", n == 0);
}

// ─── Test: Menu Building ─────────────────────────────────────────────────

static void test_menu_building() {
    printf("[Menu Building]\n");

    char buf[256];

    // Standard menu line
    size_t n = fossorial::buildMenuLine<fossorial::MenuFormat::Standard>(
        buf, sizeof(buf),
        fossorial::GopherType::TextFile,
        "README", "/docs/readme.txt",
        "gopher.example.com", 70);

    TEST("standard line built", n > 0);
    TEST_STR("standard line format", buf,
        "0README\t/docs/readme.txt\tgopher.example.com\t70\r\n");

    // LoRa menu line
    n = fossorial::buildMenuLine<fossorial::MenuFormat::LoRa>(
        buf, sizeof(buf),
        fossorial::GopherType::Directory,
        "Documents", "/docs",
        nullptr, 0);

    TEST("LoRa line built", n > 0);
    TEST_STR("LoRa line format", buf,
        "1Documents\t/docs\t\r\n");

    // Info line
    n = fossorial::buildMenuLine<fossorial::MenuFormat::Standard>(
        buf, sizeof(buf),
        fossorial::GopherType::Info,
        "Welcome to the mesh",
        "fake", "error.host", 1);
    TEST_STR("info line", buf,
        "iWelcome to the mesh\tfake\terror.host\t1\r\n");
}

// ─── Test: Menu Building with Callback ────────────────────────────────────

static void test_menu_callback() {
    printf("[Menu Callback]\n");

    fossorial::MenuEntry entries[] = {
        {fossorial::GopherType::Info,     "My Gopher Hole",  "",             "",      0},
        {fossorial::GopherType::Directory,"Files",           "/files",       "local", 0},
        {fossorial::GopherType::TextFile, "Welcome",         "/welcome.txt", "local", 0},
        {fossorial::GopherType::LoRaMenu, "Weather",         "/weather",     "",      0},
    };

    char result[1024];
    size_t pos = 0;

    size_t total = fossorial::buildMenu<fossorial::MenuFormat::LoRa>(
        entries, 4,
        [&](const char* chunk, size_t len) {
            if (pos + len < sizeof(result)) {
                memcpy(result + pos, chunk, len);
                pos += len;
            }
        });
    result[pos] = 0;

    TEST("callback menu built", total > 0);
    TEST_STR("callback menu line 1", result,
        "iMy Gopher Hole\t\t\r\n1Files\t/files\t\r\n0Welcome\t/welcome.txt\t\r\nLWeather\t/weather\t\r\n");
}

// ─── Test: Gophermap Parsing ──────────────────────────────────────────────

static void test_gophermap_parsing() {
    printf("[Gophermap Parsing]\n");

    {
        const char* line = "0README\t/readme.txt\tgopher.example.com\t70\r\n";
        fossorial::GopherType type;
        const char* ds; size_t dl;
        const char* ss; size_t sl;
        const char* hs; size_t hl;
        uint16_t port;

        bool ok = fossorial::parseGophermapLine(line, strlen(line), type, ds, dl, ss, sl, hs, hl, port);
        TEST("standard line parses", ok);
        TEST("type is TextFile", type == fossorial::GopherType::TextFile);
        TEST("display is README", dl == 6 && memcmp(ds, "README", 6) == 0);
        TEST("selector is /readme.txt", sl == 11 && memcmp(ss, "/readme.txt", 11) == 0);
        TEST("host is gopher.example.com", hl == 18 && memcmp(hs, "gopher.example.com", 18) == 0);
        TEST("port is 70", port == 70);
    }

    {
        const char* line = "1Documents\t/docs";
        fossorial::GopherType type;
        const char* ds; size_t dl;
        const char* ss; size_t sl;
        const char* hs; size_t hl;
        uint16_t port;

        bool ok = fossorial::parseGophermapLine(line, strlen(line), type, ds, dl, ss, sl, hs, hl, port);
        TEST("short line parses", ok);
        TEST("type is Directory", type == fossorial::GopherType::Directory);
        TEST("display is Documents", dl == 9 && memcmp(ds, "Documents", 9) == 0);
        TEST("selector is /docs", sl == 5 && memcmp(ss, "/docs", 5) == 0);
        TEST("no host", hl == 0);
        TEST("default port", port == 70);
    }

    {
        const char* line = "  This is an info line without tabs";
        fossorial::GopherType type;
        const char* ds; size_t dl;
        const char* ss; size_t sl;
        const char* hs; size_t hl;
        uint16_t port;

        bool ok = fossorial::parseGophermapLine(line, strlen(line), type, ds, dl, ss, sl, hs, hl, port);
        TEST("info line parses", ok);
        TEST("type is Info", type == fossorial::GopherType::Info);
        TEST("display includes spaces", dl == strlen(line));
    }

    {
        const char* line = "";
        fossorial::GopherType type;
        const char* ds; size_t dl;
        const char* ss; size_t sl;
        const char* hs; size_t hl;
        uint16_t port;
        bool ok = fossorial::parseGophermapLine(line, 0, type, ds, dl, ss, sl, hs, hl, port);
        TEST("empty line fails", !ok);
    }

    // C-string version (handles trailing newline)
    {
        const char* line = "0File\t/file.txt\texample.com\t70\r\n";
        fossorial::GopherType type;
        const char* ds; size_t dl;
        const char* ss; size_t sl;
        const char* hs; size_t hl;
        uint16_t port;
        bool ok = fossorial::parseGophermapLine(line, type, ds, dl, ss, sl, hs, hl, port);
        TEST("c-string version parses", ok);
        TEST("c-string type", type == fossorial::GopherType::TextFile);
    }
}

// ─── Test: Response Chunker ───────────────────────────────────────────────

static void test_response_chunker() {
    printf("[Response Chunker]\n");

    const char* response = "Hello, world!";
    size_t resp_len = strlen(response);

    // Single chunk (fits in one)
    {
        auto c = fossorial::ResponseChunker::init(response, resp_len, 0, 64);
        TEST("single chunk not complete initially", !c.isComplete());
        TEST("remaining == total initially", c.remaining() == resp_len);

        size_t plen;
        fossorial::ChunkFlags flags;
        const char* p = c.next(plen, flags);
        TEST("got payload", p != nullptr);
        TEST("payload length correct", plen == resp_len);
        TEST("flags is Sole", flags == fossorial::ChunkFlags::Sole);
        TEST("memcmp matches", memcmp(p, response, resp_len) == 0);
        TEST("chunker complete", c.isComplete());

        // No more chunks
        p = c.next(plen, flags);
        TEST("no more chunks", p == nullptr);
    }

    // Multi-chunk (small chunk size)
    {
        const char* big = "This is a longer response that needs to be split into multiple chunks for transmission over the LoRa mesh network.";
        size_t big_len = strlen(big);

        auto c = fossorial::ResponseChunker::init(big, big_len, 0, 32);

        size_t total = 0;
        int chunks = 0;
        bool saw_first = false;
        bool saw_last = false;

        while (true) {
            size_t plen;
            fossorial::ChunkFlags flags;
            const char* p = c.next(plen, flags);
            if (!p) break;
            total += plen;
            ++chunks;

            if (flags == fossorial::ChunkFlags::First) saw_first = true;
            if (flags == fossorial::ChunkFlags::Sole) saw_first = saw_last = true;
            if (flags == fossorial::ChunkFlags::Last ||
                flags == fossorial::ChunkFlags::Sole) saw_last = true;
        }

        TEST("multiple chunks produced", chunks > 1);
        TEST("total matches", total == big_len);
        TEST("saw first chunk", saw_first);
        TEST("saw last chunk", saw_last);
        TEST("chunker complete", c.isComplete());
    }
}

// ─── Test: Error Response ─────────────────────────────────────────────────

static void test_error_response() {
    printf("[Error Response]\n");

    char buf[128];
    size_t n = fossorial::buildErrorResponse(buf, sizeof(buf), "File not found");
    TEST("error response built", n > 0);
    TEST_STR("error format matches", buf, "3File not found\tX\t\r\n");
}

// ─── Test: Terminator ─────────────────────────────────────────────────────

static void test_terminator() {
    printf("[Terminator]\n");

    char buf[16];
    memset(buf, 0, sizeof(buf));

    size_t n = fossorial::appendTerminator(buf, sizeof(buf), 0);
    TEST("terminator written", n == 3);
    TEST_STR("terminator content", buf, ".\r\n");
}

// ─── Test: Type from Extension ────────────────────────────────────────────

static void test_type_from_extension() {
    printf("[Type from Extension]\n");

    TEST("txt -> TextFile",
         fossorial::typeFromExtension("txt", 3) == fossorial::GopherType::TextFile);
    TEST("TXT -> TextFile (case insensitive)",
         fossorial::typeFromExtension("TXT", 3) == fossorial::GopherType::TextFile);
    TEST("png -> Image",
         fossorial::typeFromExtension("png", 3) == fossorial::GopherType::Image);
    TEST("gif -> GIF",
         fossorial::typeFromExtension("gif", 3) == fossorial::GopherType::GIF);
    TEST("html -> HTML",
         fossorial::typeFromExtension("html", 4) == fossorial::GopherType::HTML);
    TEST("unknown -> Binary",
         fossorial::typeFromExtension("xyz", 3) == fossorial::GopherType::Binary);
    TEST("empty -> Binary",
         fossorial::typeFromExtension("", 0) == fossorial::GopherType::Binary);

    TEST("filename readme.txt -> TextFile",
         fossorial::typeFromFilename("readme.txt") == fossorial::GopherType::TextFile);
    TEST("filename image.png -> Image",
         fossorial::typeFromFilename("image.png") == fossorial::GopherType::Image);
    TEST("filename noext -> Binary",
         fossorial::typeFromFilename("Makefile") == fossorial::GopherType::Binary);
}

// ─── Main ─────────────────────────────────────────────────────────────────

int main() {
    printf("=== Fossorial GopherCore Test Suite ===\n\n");

    test_selector_validation();
    test_gopher_types();
    test_path_utils();
    test_display_truncation();
    test_menu_building();
    test_menu_callback();
    test_gophermap_parsing();
    test_response_chunker();
    test_error_response();
    test_terminator();
    test_type_from_extension();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
