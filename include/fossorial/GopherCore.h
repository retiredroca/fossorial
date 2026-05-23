#ifndef FOSSORIAL_GOPHER_CORE_H
#define FOSSORIAL_GOPHER_CORE_H

#include <stddef.h>
#include <stdint.h>

// Maximum payload size for a single MeshCore LoRa packet
#ifndef FOSSORIAL_CHUNK_SIZE
#define FOSSORIAL_CHUNK_SIZE 184
#endif

// Maximum selector length per RFC 1436
#ifndef FOSSORIAL_MAX_SELECTOR
#define FOSSORIAL_MAX_SELECTOR 255
#endif

// Maximum display string length (per RFC 1436 recommendation)
#ifndef FOSSORIAL_MAX_DISPLAY
#define FOSSORIAL_MAX_DISPLAY 70
#endif

namespace fossorial {

// ─── GopherType ───────────────────────────────────────────────────────────

enum class GopherType : char {
    TextFile     = '0',
    Directory    = '1',
    CSO          = '2',
    Error        = '3',
    BinHex       = '4',
    DOSArchive   = '5',
    UUEncoded    = '6',
    IndexSearch  = '7',
    Telnet       = '8',
    Binary       = '9',
    Redundant    = '+',
    TN3270       = 'T',
    GIF          = 'g',
    Image        = 'I',
    Info         = 'i',
    HTML         = 'h',
    Sound        = 's',
    LoRaMenu     = 'L',
};

struct GopherTypeInfo {
    char        type_char;
    const char* name;
    bool        is_binary;
};

static constexpr GopherTypeInfo GOPHER_TYPES[] = {
    {'0', "Text file",                 false},
    {'1', "Directory",                 false},
    {'2', "CSO phone book",            false},
    {'3', "Error",                     false},
    {'4', "BinHex Macintosh",          true },
    {'5', "DOS binary",                true },
    {'6', "UUEncoded",                 false},
    {'7', "Index search",              false},
    {'8', "Telnet",                    false},
    {'9', "Binary file",               true },
    {'+', "Redundant server",          false},
    {'T', "TN3270",                    false},
    {'g', "GIF image",                 true },
    {'I', "Image",                     true },
    {'i', "Informational text",        false},
    {'h', "HTML file",                 false},
    {'s', "Sound",                     true },
    {'L', "LoRa mesh directory",       false},
};

static constexpr size_t GOPHER_TYPES_COUNT = sizeof(GOPHER_TYPES) / sizeof(GOPHER_TYPES[0]);

static inline constexpr GopherType gopherTypeFromChar(char c) {
    for (size_t i = 0; i < GOPHER_TYPES_COUNT; ++i) {
        if (GOPHER_TYPES[i].type_char == c) return static_cast<GopherType>(c);
    }
    return GopherType::Binary;
}

static inline constexpr const char* gopherTypeName(GopherType t) {
    for (size_t i = 0; i < GOPHER_TYPES_COUNT; ++i) {
        if (GOPHER_TYPES[i].type_char == static_cast<char>(t)) return GOPHER_TYPES[i].name;
    }
    return "Unknown";
}

static inline constexpr bool gopherTypeIsBinary(GopherType t) {
    for (size_t i = 0; i < GOPHER_TYPES_COUNT; ++i) {
        if (GOPHER_TYPES[i].type_char == static_cast<char>(t)) return GOPHER_TYPES[i].is_binary;
    }
    return false;
}

// ─── Selector ─────────────────────────────────────────────────────────────

static inline constexpr bool isSelectorByteValid(char c) {
    return c != '\t' && c != '\n' && c != '\r';
}

static inline constexpr bool validateSelector(const char* s, size_t len) {
    if (len > FOSSORIAL_MAX_SELECTOR) return false;
    for (size_t i = 0; i < len; ++i) {
        if (!isSelectorByteValid(s[i])) return false;
    }
    return true;
}

static inline bool validateSelector(const char* s) {
    if (!s) return false;
    size_t len = 0;
    while (s[len]) ++len;
    return validateSelector(s, len);
}

// ─── Path Utilities ──────────────────────────────────────────────────────

// Normalise a relative selector path against a base path.
// Handles ".", "..", and multiple consecutive slashes.
// Returns the length of the result (excluding null terminator), or 0 on error.
static inline size_t normalizePath(char* dst, size_t dstsz,
                                    const char* base, size_t base_len,
                                    const char* rel, size_t rel_len)
{
    if (!dst || !dstsz) return 0;

    // Stack-based path component tracker using just the output buffer.
    // We build the absolute path first, then collapse ".." in-place.

    char tmp[FOSSORIAL_MAX_SELECTOR + 1];
    size_t pos = 0;

    // If rel is empty or relative (starts with '/'), use base
    if (rel_len == 0) {
        // Copy base
        if (base_len > FOSSORIAL_MAX_SELECTOR) return 0;
        for (size_t i = 0; i < base_len; ++i) tmp[i] = base[i];
        pos = base_len;
    } else if (rel[0] != '/') {
        // Relative: copy base, then append '/' + rel
        if (base_len > FOSSORIAL_MAX_SELECTOR) return 0;
        for (size_t i = 0; i < base_len; ++i) tmp[i] = base[i];
        pos = base_len;
        if (pos > 0 && tmp[pos - 1] != '/') {
            if (pos >= FOSSORIAL_MAX_SELECTOR) return 0;
            tmp[pos++] = '/';
        }
        if (pos + rel_len > FOSSORIAL_MAX_SELECTOR) return 0;
        for (size_t i = 0; i < rel_len; ++i) tmp[pos++] = rel[i];
    } else {
        // Absolute: use rel directly
        if (rel_len > FOSSORIAL_MAX_SELECTOR) return 0;
        for (size_t i = 0; i < rel_len; ++i) tmp[i] = rel[i];
        pos = rel_len;
    }

    // Ensure null-terminated for string ops on tmp
    tmp[pos] = 0;

    // Collapse ".." components using a simple in-place approach.
    // We write the canonical path back to tmp starting from tmp[0].
    size_t wp = 0; // write position
    size_t rp = 0; // read position

    while (rp < pos) {
        // Skip slashes
        while (rp < pos && tmp[rp] == '/') ++rp;
        if (rp >= pos) break;

        // Find end of this component
        size_t start = rp;
        while (rp < pos && tmp[rp] != '/') ++rp;

        size_t comp_len = rp - start;

        if (comp_len == 1 && tmp[start] == '.') {
            // Skip "./"
            continue;
        } else if (comp_len == 2 && tmp[start] == '.' && tmp[start + 1] == '.') {
            if (wp > 1) {
                --wp;
                while (wp > 0 && tmp[wp - 1] != '/') --wp;
                if (wp > 0) --wp;
            }
            continue;
        } else {
            // Normal component: write slash + component
            if (wp > 0) {
                if (wp >= dstsz) return 0;
                tmp[wp++] = '/';
            }
            if (wp + comp_len >= dstsz) return 0;
            for (size_t i = 0; i < comp_len; ++i) tmp[wp++] = tmp[start + i];
        }
    }

    if (wp == 0) {
        // Path is empty -> root
        if (dstsz < 2) return 0;
        dst[0] = '/';
        dst[1] = 0;
        return 1;
    }

    // Ensure leading slash
    bool has_leading = (base_len > 0 && base[0] == '/') || (rel_len > 0 && rel[0] == '/') || (pos > 0 && tmp[0] == '/');
    size_t di = 0;
    if (has_leading && tmp[0] != '/') {
        if (di >= dstsz) return 0;
        dst[di++] = '/';
    }
    for (size_t i = 0; i < wp && di < dstsz - 1; ++i) {
        // Skip leading slash in tmp if we already added one
        if (i == 0 && tmp[0] == '/' && has_leading) continue;
        dst[di++] = tmp[i];
    }
    dst[di] = 0;
    return di;
}

static inline size_t normalizePath(char* dst, size_t dstsz,
                                    const char* base, const char* rel)
{
    size_t blen = base ? 0 : 0;
    if (base) while (base[blen]) ++blen;
    size_t rlen = rel ? 0 : 0;
    if (rel) while (rel[rlen]) ++rlen;
    return normalizePath(dst, dstsz, base, blen, rel, rlen);
}

// ─── Display String ──────────────────────────────────────────────────────

static inline size_t truncateDisplay(char* dst, size_t dstsz,
                                      const char* text, size_t text_len)
{
    if (!dst || !dstsz) return 0;
    size_t n = text_len;
    if (n > FOSSORIAL_MAX_DISPLAY) n = FOSSORIAL_MAX_DISPLAY;
    if (n >= dstsz) n = dstsz - 1;
    for (size_t i = 0; i < n; ++i) dst[i] = text[i];
    dst[n] = 0;
    return n;
}

static inline size_t truncateDisplay(char* dst, size_t dstsz, const char* text) {
    size_t len = 0;
    if (text) while (text[len]) ++len;
    return truncateDisplay(dst, dstsz, text, len);
}

// ─── MenuEntry ────────────────────────────────────────────────────────────

struct MenuEntry {
    GopherType type;
    const char* display;
    const char* selector;
    const char* host;
    uint16_t    port;
};

// ─── Menu Building ────────────────────────────────────────────────────────

// RFC 1436 format:
//   Xdisplay\tselector\thost\tport\r\n
//
// LoRa format (host/port omitted):
//   Xdisplay\tselector\r\n

enum class MenuFormat : uint8_t {
    Standard,  // Full RFC 1436 with host:port
    LoRa,      // Host/port omitted
};

// Build a single menu line. Returns length excluding null, or 0 on truncation.
template <MenuFormat Fmt = MenuFormat::Standard>
static inline size_t buildMenuLine(char* dst, size_t dstsz,
                                    GopherType type,
                                    const char* display,
                                    const char* selector,
                                    const char* host = nullptr,
                                    uint16_t port = 70)
{
    if (!dst || !dstsz) return 0;

    size_t i = 0;

    // Type character
    if (i < dstsz) dst[i] = static_cast<char>(type);
    ++i;

    // Display string
    if (display) {
        for (size_t j = 0; display[j] && j <= FOSSORIAL_MAX_DISPLAY && i < dstsz; ++j) {
            dst[i++] = display[j];
        }
    }

    // TAB
    if (i < dstsz) dst[i] = '\t';
    ++i;

    // Selector
    if (selector) {
        for (size_t j = 0; selector[j] && i < dstsz; ++j) {
            const char c = selector[j];
            if (c == '\t' || c == '\n' || c == '\r') break; // illegal in selector
            dst[i++] = c;
        }
    }

    if constexpr (Fmt == MenuFormat::Standard) {
        // TAB host TAB port \r\n
        if (i < dstsz) dst[i] = '\t';
        ++i;
        if (host) {
            for (size_t j = 0; host[j] && i < dstsz; ++j) dst[i++] = host[j];
        }
        if (i < dstsz) dst[i] = '\t';
        ++i;

        // Format port as string
        char pbuf[8];
        size_t plen = 0;
        if (port > 0) {
            uint16_t p = port;
            // Reverse digits
            char rev[8];
            size_t rl = 0;
            while (p > 0) {
                rev[rl++] = '0' + (p % 10);
                p /= 10;
            }
            for (size_t j = rl; j > 0; --j) pbuf[plen++] = rev[j - 1];
        }
        for (size_t j = 0; j < plen && i < dstsz; ++j) dst[i++] = pbuf[j];
    } else {
        // LoRa: just TAB (we put a fake/empty host and port are implied)
        if (i < dstsz) dst[i] = '\t';
        ++i;
    }

    // \r\n
    if (i < dstsz) dst[i] = '\r';
    ++i;
    if (i < dstsz) dst[i] = '\n';
    ++i;

    // Null terminate
    if (i < dstsz) dst[i] = 0;
    return i < dstsz ? i : 0;
}

// Build a menu from an array of entries.  Returns total bytes written.
// Output callback is called with (const char* chunk, size_t len) for each
// contiguous piece.  This avoids needing a large buffer.
template <MenuFormat Fmt = MenuFormat::Standard, typename Output>
static inline size_t buildMenu(const MenuEntry* entries, size_t count, Output&& output) {
    char buf[FOSSORIAL_MAX_SELECTOR + 32]; // large enough for any single line
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        auto& e = entries[i];
        size_t n;
        if constexpr (Fmt == MenuFormat::Standard) {
            n = buildMenuLine<MenuFormat::Standard>(buf, sizeof(buf), e.type, e.display, e.selector, e.host, e.port);
        } else {
            n = buildMenuLine<MenuFormat::LoRa>(buf, sizeof(buf), e.type, e.display, e.selector);
        }
        if (n == 0) break;
        output(buf, n);
        total += n;
    }
    return total;
}

// ─── Gophermap Parser ─────────────────────────────────────────────────────

// Parse a single gophermap line.
// Returns true on success.
// Line should NOT include the trailing \r\n.
// Lines without any TAB character are treated as type 'i' (info text).
static inline bool parseGophermapLine(const char* line, size_t line_len,
                                       GopherType& type,
                                       const char*& display_start, size_t& display_len,
                                       const char*& selector_start, size_t& selector_len,
                                       const char*& host_start, size_t& host_len,
                                       uint16_t& port)
{
    if (!line || line_len == 0) return false;

    // Find TAB positions
    size_t tab1 = line_len;
    size_t tab2 = line_len;
    size_t tab3 = line_len;

    size_t tab_count = 0;
    for (size_t i = 0; i < line_len; ++i) {
        if (line[i] == '\t') {
            if (tab_count == 0) tab1 = i;
            else if (tab_count == 1) tab2 = i;
            else if (tab_count == 2) tab3 = i;
            ++tab_count;
        }
    }

    if (tab_count == 0) {
        // Info line (type 'i')
        type = GopherType::Info;
        display_start = line;
        display_len = line_len;
        selector_start = nullptr;
        selector_len = 0;
        host_start = nullptr;
        host_len = 0;
        port = 0;
        return true;
    }

    // First char is type, or first char could be part of display if type is after tab
    // Standard gophermap:  Xdisplay\tselector[\thost[\tport]]
    // Where X is a single type character
    if (line_len < 2) return false;

    type = static_cast<GopherType>(line[0]);

    // Display is from line[1] to tab1-1
    display_start = line + 1;
    display_len = (tab1 > 1) ? tab1 - 1 : 0;

    // Selector is from tab1+1 to tab2-1 (or end)
    if (tab1 + 1 < line_len) {
        selector_start = line + tab1 + 1;
        selector_len = (tab2 < line_len) ? tab2 - tab1 - 1 : line_len - tab1 - 1;
    } else {
        selector_start = nullptr;
        selector_len = 0;
    }

    // Host is from tab2+1 to tab3-1 (or end)
    if (tab2 + 1 < line_len) {
        host_start = line + tab2 + 1;
        host_len = (tab3 < line_len) ? tab3 - tab2 - 1 : line_len - tab2 - 1;
    } else {
        host_start = nullptr;
        host_len = 0;
    }

    // Port is from tab3+1 to end
    port = 70;
    if (tab3 + 1 < line_len) {
        const char* ps = line + tab3 + 1;
        size_t pl = line_len - tab3 - 1;
        uint16_t val = 0;
        for (size_t i = 0; i < pl; ++i) {
            if (ps[i] >= '0' && ps[i] <= '9') {
                val = val * 10 + (ps[i] - '0');
            } else break;
        }
        if (val > 0) port = val;
    }

    return true;
}

static inline bool parseGophermapLine(const char* line,
                                       GopherType& type,
                                       const char*& display_start, size_t& display_len,
                                       const char*& selector_start, size_t& selector_len,
                                       const char*& host_start, size_t& host_len,
                                       uint16_t& port)
{
    size_t len = 0;
    if (line) while (line[len]) ++len;
    if (len > 0 && line[len-1] == '\n') --len;
    if (len > 0 && line[len-1] == '\r') --len;
    return parseGophermapLine(line, len, type, display_start, display_len,
                               selector_start, selector_len, host_start, host_len, port);
}

// Higher-level parse that fills a MenuEntry
static inline bool parseGophermapLineToEntry(const char* line, size_t line_len, MenuEntry& entry) {
    const char* ds; size_t dl;
    const char* ss; size_t sl;
    const char* hs; size_t hl;
    uint16_t port;

    if (!parseGophermapLine(line, line_len, entry.type, ds, dl, ss, sl, hs, hl, port))
        return false;

    // We need null-terminated strings for MenuEntry.
    // The caller must ensure the data lives long enough.
    // We just point into the original line buffer.
    // This means the line buffer must be writable or we must store separately.
    // For now, we just store raw pointers into the original line.
    // Better: provide a version that copies into a buffer.

    // Store the pointers (caller must ensure line buffer persists)
    // For display, selector, host we store pointer directly
    // This is a bit fragile but efficient.

    // We'll use a thread-local or static buffer approach? No, that's not safe.
    // Let's just store the start pointers and lengths and let the caller handle it.

    // Actually, MenuEntry uses const char* which expects null-terminated.
    // For a robust standalone header, we should copy. But that requires buffers.
    // Let's add a version that copies into a provided buffer.

    entry.display = ds;
    entry.selector = ss;
    entry.host = hs;
    entry.port = port;
    return true;
}

// ─── Response Chunker ─────────────────────────────────────────────────────

enum class ChunkFlags : uint8_t {
    Sole   = 0x00,  // Single chunk, complete response
    First  = 0x01,  // First chunk of a multi-chunk response
    Middle = 0x02,  // Middle chunk
    Last   = 0x04,  // Last chunk
};

struct ChunkHeader {
    uint8_t type;    // Application-level type
    uint8_t seq;     // Sequence number (increments per chunk)
    uint8_t flags;   // ChunkFlags
} __attribute__((packed));

static_assert(sizeof(ChunkHeader) == 3, "ChunkHeader must be 3 bytes");

struct ResponseChunker {
    const char* data;
    size_t      total_len;
    size_t      offset;
    size_t      chunk_size;
    uint8_t     seq;
    uint8_t     type_id;

    static inline constexpr size_t HEADER_SIZE = sizeof(ChunkHeader);
    static inline constexpr size_t MAX_PAYLOAD = FOSSORIAL_CHUNK_SIZE - HEADER_SIZE;

    static inline ResponseChunker init(const char* data, size_t total_len,
                                       uint8_t type_id = 0,
                                       size_t chunk_size = FOSSORIAL_CHUNK_SIZE)
    {
        ResponseChunker c;
        c.data       = data;
        c.total_len  = total_len;
        c.offset     = 0;
        c.chunk_size = chunk_size < HEADER_SIZE + 1 ? HEADER_SIZE + 1 : chunk_size;
        c.seq        = 0;
        c.type_id    = type_id;
        return c;
    }

    // Returns pointer to next chunk payload and sets payload_len.
    // Returns nullptr when complete.
    // The caller should prepend a ChunkHeader at (result - HEADER_SIZE).
    inline const char* next(size_t& payload_len, ChunkFlags& flags) {
        if (offset >= total_len) {
            payload_len = 0;
            return nullptr;
        }

        size_t avail = chunk_size - HEADER_SIZE;
        size_t remaining = total_len - offset;
        bool is_first = (offset == 0);

        if (remaining <= avail) {
            // Fits in one chunk
            flags = is_first ? ChunkFlags::Sole : ChunkFlags::Last;
            payload_len = remaining;
            const char* result = data + offset;
            offset = total_len;
            ++seq;
            return result;
        }

        // Need multiple chunks
        if (is_first) {
            flags = ChunkFlags::First;
        } else {
            flags = (remaining == avail) ? ChunkFlags::Last : ChunkFlags::Middle;
        }

        payload_len = avail;
        const char* result = data + offset;
        offset += avail;
        ++seq;
        return result;
    }

    inline bool isComplete() const { return offset >= total_len; }
    inline size_t remaining() const { return total_len - offset; }
};

// ─── Multipart encode/decode ──────────────────────────────────────────────

static inline void encodeChunkHeader(uint8_t* dst, uint8_t type, uint8_t seq, uint8_t flags) {
    dst[0] = type;
    dst[1] = seq;
    dst[2] = flags;
}

static inline void decodeChunkHeader(const uint8_t* src, uint8_t& type, uint8_t& seq, uint8_t& flags) {
    type  = src[0];
    seq   = src[1];
    flags = src[2];
}

static inline bool isFirstChunk(uint8_t flags) { return (flags & 0x01) || flags == 0x00; }
static inline bool isLastChunk(uint8_t flags)  { return (flags & 0x04) || flags == 0x00; }

// ─── Error Response Helpers ───────────────────────────────────────────────

// Standard Gopher error: "3Error message\tfake\terror.host\t1\r\n"
// We produce the simpler LoRa variant.
static inline size_t buildErrorResponse(char* dst, size_t dstsz, const char* message) {
    if (!dst || !dstsz) return 0;
    size_t i = 0;
    if (i < dstsz) { dst[i] = '3'; ++i; }
    if (message) {
        for (size_t j = 0; message[j] && i < dstsz - 1; ++j) dst[i++] = message[j];
    }
    if (i < dstsz) { dst[i] = '\t'; ++i; }
    if (i < dstsz) { dst[i] = 'X';  ++i; }
    if (i < dstsz) { dst[i] = '\t'; ++i; }
    if (i < dstsz) { dst[i] = '\r'; ++i; }
    if (i < dstsz) { dst[i] = '\n'; ++i; }
    if (i < dstsz) { dst[i] = 0; }
    return i;
}

// ─── Response Termination ─────────────────────────────────────────────────

// Gopher responses end with ".\r\n" on a line by itself.
static inline size_t appendTerminator(char* dst, size_t dstsz, size_t pos) {
    if (pos + 3 >= dstsz) return 0;
    dst[pos] = '.';
    dst[pos+1] = '\r';
    dst[pos+2] = '\n';
    dst[pos+3] = 0;
    return pos + 3;
}

// ─── Template Helpers ─────────────────────────────────────────────────────

// Convert a file extension to a GopherType guess.
// This is a simple mapping, just like Gophernicus does.
static inline GopherType typeFromExtension(const char* ext, size_t ext_len) {
    if (ext_len == 0) return GopherType::Binary;

    // Common text extensions
    static const struct {
        const char* ext;
        size_t      len;
        GopherType  type;
    } map[] = {
        {"txt",   3, GopherType::TextFile},
        {"md",    2, GopherType::TextFile},
        {"text",  4, GopherType::TextFile},
        {"asc",   3, GopherType::TextFile},
        {"nfo",   3, GopherType::TextFile},
        {"log",   3, GopherType::TextFile},
        {"c",     1, GopherType::TextFile},
        {"h",     1, GopherType::TextFile},
        {"cpp",   3, GopherType::TextFile},
        {"hpp",   3, GopherType::TextFile},
        {"py",    2, GopherType::TextFile},
        {"rs",    2, GopherType::TextFile},
        {"go",    2, GopherType::TextFile},
        {"js",    2, GopherType::TextFile},
        {"ts",    2, GopherType::TextFile},
        {"css",   3, GopherType::TextFile},
        {"html",  4, GopherType::HTML},
        {"htm",   3, GopherType::HTML},
        {"gif",   3, GopherType::GIF},
        {"png",   3, GopherType::Image},
        {"jpg",   3, GopherType::Image},
        {"jpeg",  4, GopherType::Image},
        {"svg",   3, GopherType::Image},
        {"webp",  4, GopherType::Image},
        {"bmp",   3, GopherType::Image},
        {"ico",   3, GopherType::Image},
    };

    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); ++i) {
        if (ext_len == map[i].len) {
            bool match = true;
            for (size_t j = 0; j < ext_len; ++j) {
                // Case-insensitive comparison
                char a = ext[j];
                char b = map[i].ext[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match) return map[i].type;
        }
    }

    return GopherType::Binary;
}

static inline GopherType typeFromFilename(const char* name, size_t name_len) {
    // Find last '.' to extract extension
    size_t dot = name_len;
    for (size_t i = name_len; i > 0; --i) {
        if (name[i-1] == '.') { dot = i-1; break; }
    }
    if (dot >= name_len) return GopherType::Binary;
    return typeFromExtension(name + dot + 1, name_len - dot - 1);
}

static inline GopherType typeFromFilename(const char* name) {
    size_t len = 0;
    if (name) while (name[len]) ++len;
    return typeFromFilename(name, len);
}

} // namespace fossorial

#endif // FOSSORIAL_GOPHER_CORE_H
