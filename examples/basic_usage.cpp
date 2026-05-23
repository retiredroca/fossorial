#include <stdio.h>
#include <string.h>
#include "fossorial/GopherCore.h"

int main() {
    using namespace fossorial;

    printf("=== Fossorial GopherCore Example ===\n\n");

    // 1. Validate a Gopher selector
    const char* sel = "/docs/readme.txt";
    printf("Selector: \"%s\" → %s\n", sel,
           validateSelector(sel) ? "valid" : "INVALID");

    // 2. Build a Gopher menu (LoRa format, no host/port)
    MenuEntry menu[] = {
        {GopherType::Info,     " Fossorial Gopher Hole",    "",                "", 0},
        {GopherType::Directory,"Documents",                  "/docs",           "", 0},
        {GopherType::TextFile, "Welcome message",            "/welcome.txt",    "", 0},
        {GopherType::Directory,"Downloads",                  "/downloads",      "", 0},
        {GopherType::LoRaMenu, "Weather Station",            "/weather",        "", 0},
        {GopherType::Image,    "Network Map",                 "/map.png",        "", 0},
        {GopherType::Info,     "",                            "",                "", 0},
        {GopherType::Info,     " Send a message with the",    "",                "", 0},
        {GopherType::Info,     " selector /search:<query>",   "",                "", 0},
    };

    printf("\nGopher Menu (LoRa):\n");
    printf("-------------------\n");

    buildMenu<MenuFormat::LoRa>(menu, sizeof(menu)/sizeof(menu[0]),
        [](const char* chunk, size_t len) {
            // Chunk may not be null-terminated; write byte-by-byte
            for (size_t i = 0; i < len; ++i) putchar(chunk[i]);
        });

    printf("\n");

    // 3. Build a Standard-format menu line (like TCP Gopher)
    char line[256];
    buildMenuLine<MenuFormat::Standard>(line, sizeof(line),
        GopherType::TextFile, "README",
        "/readme.txt", "gopher.example.com", 70);
    printf("\nStandard menu line:\n  %s", line);

    // 4. Chunk a response for LoRa transmission
    const char* response_text =
        "This is a longer Gopher response that demonstrates how "
        "Fossorial splits content into small chunks suitable for "
        "transmission over a LoRa mesh network like MeshCore.";

    printf("\nChunked response (%zu bytes, chunk size=48):\n",
           strlen(response_text));

    auto chunker = ResponseChunker::init(response_text,
                                          strlen(response_text),
                                          0, 48);
    int chunk_num = 0;
    size_t plen;
    ChunkFlags flags;
    while (const char* p = chunker.next(plen, flags)) {
        printf("  Chunk %d (flags=0x%02x, %zu bytes): ",
               chunk_num++, (uint8_t)flags, plen);
        for (size_t i = 0; i < plen; ++i) putchar(p[i]);
        printf("\n");
    }

    return 0;
}
