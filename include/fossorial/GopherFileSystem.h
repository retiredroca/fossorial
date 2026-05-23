#ifndef FOSSORIAL_GOPHER_FILESYSTEM_H
#define FOSSORIAL_GOPHER_FILESYSTEM_H

#include "GopherCore.h"

namespace fossorial {

// ─── GopherFileSystem concept ─────────────────────────────────────────────
//
// A compatible FileSystem type must provide these operations:
//
//   bool  exists(const char* path) const;
//   bool  isDir(const char* path) const;
//   int   readFile(const char* path, char* buf, int bufsz) const;
//   // Directory iteration: call with idx=0 to start, increment each call.
//   // Returns true while entries remain.  Sets name (null-terminated) and is_dir.
//   // Returns false when iteration is complete.
//   bool  readDir(const char* path, int& idx, char* name, int namesz, bool& is_dir) const;

// Default config file names for gophermaps
#ifndef FOSSORIAL_GOPHERMAP
#define FOSSORIAL_GOPHERMAP "gophermap"
#endif

#ifndef FOSSORIAL_GOPHER_ROOT
#define FOSSORIAL_GOPHER_ROOT "/gopher"
#endif

// ─── Selector to filesystem path ─────────────────────────────────────────

// Maps a Gopher selector string to a filesystem path.
// Returns length of path (excluding null), or 0 on error.
// The path is written to the output buffer.
static inline size_t selectorToPath(char* path, size_t pathsz,
                                     const char* selector, size_t sel_len)
{
    if (!path || !pathsz) return 0;

    // Start with gopher root
    const char* root = FOSSORIAL_GOPHER_ROOT;
    size_t root_len = 0;
    while (root[root_len]) ++root_len;

    // Copy root
    size_t pos = 0;
    for (size_t i = 0; i < root_len && pos < pathsz - 1; ++i)
        path[pos++] = root[i];

    // Append selector (skip leading slash if root already ends with one)
    if (sel_len > 0 && sel_len < FOSSORIAL_MAX_SELECTOR) {
        bool root_has_slash = (pos > 0 && path[pos-1] == '/');

        if (!root_has_slash) {
            if (pos >= pathsz - 1) return 0;
            path[pos++] = '/';
        }

        // If selector starts with '/', skip it to avoid double slash
        size_t s_start = (sel_len > 0 && selector[0] == '/') ? 1 : 0;

        for (size_t i = s_start; i < sel_len && pos < pathsz - 1; ++i) {
            char c = selector[i];
            if (c == '\t' || c == '\n' || c == '\r') break;
            path[pos++] = c;
        }
    }

    path[pos] = 0;
    return pos;
}

static inline size_t selectorToPath(char* path, size_t pathsz, const char* selector) {
    size_t len = selector ? 0 : 0;
    if (selector) while (selector[len]) ++len;
    return selectorToPath(path, pathsz, selector, len);
}

// ─── Menu generation from directory ──────────────────────────────────────

// Auto-generate a Gopher menu from a directory listing (no gophermap).
// Items are assigned types based on filename extensions.
template <typename FileSystem, MenuFormat Fmt, typename Output>
static inline size_t generateDirectoryMenu(const FileSystem& fs,
                                            const char* dir_path,
                                            const char* selector_prefix,
                                            Output&& output)
{
    char name[128];
    bool is_dir;
    int idx = 0;

    size_t total = 0;
    char line[FOSSORIAL_MAX_SELECTOR + 32];

    while (fs.readDir(dir_path, idx, name, sizeof(name), is_dir)) {
        // Skip hidden files and gophermap itself
        if (name[0] == '.') continue;
        if (strcmp(name, FOSSORIAL_GOPHERMAP) == 0) continue;

        GopherType type = is_dir ? GopherType::Directory
                                 : typeFromFilename(name);

        // Build selector for this item
        char item_selector[FOSSORIAL_MAX_SELECTOR + 1];
        size_t sel_pos = 0;

        if (selector_prefix) {
            for (size_t i = 0; selector_prefix[i] && sel_pos < sizeof(item_selector)-1; ++i)
                item_selector[sel_pos++] = selector_prefix[i];
        }

        // Ensure separator
        if (sel_pos > 0 && item_selector[sel_pos-1] != '/')
            item_selector[sel_pos++] = '/';

        // Append filename
        for (size_t i = 0; name[i] && sel_pos < sizeof(item_selector)-1; ++i)
            item_selector[sel_pos++] = name[i];

        item_selector[sel_pos] = 0;

        size_t n;
        if constexpr (Fmt == MenuFormat::Standard) {
            n = buildMenuLine<MenuFormat::Standard>(line, sizeof(line), type, name, item_selector);
        } else {
            n = buildMenuLine<MenuFormat::LoRa>(line, sizeof(line), type, name, item_selector);
        }

        if (n == 0) break;
        output(line, n);
        total += n;
    }

    return total;
}

// ─── Serving a selector ──────────────────────────────────────────────────

enum class SelectorResult {
    Menu,       // Selector resolved to a directory (serve menu)
    File,       // Selector resolved to a file (serve raw content)
    Gophermap,  // Selector had a gophermap (serve via gophermap content)
    NotFound,   // Selector not found
    Error,      // Internal error
};

struct ResolvedSelector {
    SelectorResult result;
    char path[FOSSORIAL_MAX_SELECTOR + 1];
};

template <typename FileSystem>
static inline ResolvedSelector resolveSelector(const FileSystem& fs,
                                                const char* selector,
                                                size_t sel_len)
{
    ResolvedSelector rs;
    rs.result = SelectorResult::NotFound;
    rs.path[0] = 0;

    if (sel_len == 0 || (sel_len == 1 && selector[0] == '/')) {
        selectorToPath(rs.path, sizeof(rs.path), "");
        if (fs.isDir(rs.path)) {
            size_t plen = strlen(rs.path);
            size_t pos = plen;
            if (pos > 0 && rs.path[pos-1] != '/') rs.path[pos++] = '/';
            rs.path[pos] = 0;
            char full[FOSSORIAL_MAX_SELECTOR + 16];
            size_t fi = 0;
            for (fi = 0; fi < pos && fi < sizeof(full); ++fi) full[fi] = rs.path[fi];
            size_t gi = 0;
            while (FOSSORIAL_GOPHERMAP[gi] && fi < sizeof(full)-1) full[fi++] = FOSSORIAL_GOPHERMAP[gi++];
            full[fi] = 0;
            if (fs.exists(full)) {
                rs.result = SelectorResult::Gophermap;
            } else {
                rs.result = SelectorResult::Menu;
            }
        }
        return rs;
    }

    // Map selector to filesystem path
    selectorToPath(rs.path, sizeof(rs.path), selector, sel_len);

    if (!fs.exists(rs.path)) {
        // Check if it's a path + gophermap
        char gmpath[FOSSORIAL_MAX_SELECTOR + 16];
        size_t plen = strlen(rs.path);
        if (plen + 1 + strlen(FOSSORIAL_GOPHERMAP) + 1 < sizeof(gmpath)) {
            size_t pos = 0;
            for (size_t i = 0; i < plen; ++i) gmpath[pos++] = rs.path[i];
            gmpath[pos++] = '/';
            for (size_t i = 0; FOSSORIAL_GOPHERMAP[i]; ++i) gmpath[pos++] = FOSSORIAL_GOPHERMAP[i];
            gmpath[pos] = 0;

            if (fs.exists(gmpath)) {
                // Path is a directory with gophermap
                size_t ep = plen;
                if (ep > 0 && rs.path[ep-1] != '/') rs.path[ep++] = '/';
                rs.path[ep] = 0;
                rs.result = SelectorResult::Gophermap;
                return rs;
            }
        }
        rs.result = SelectorResult::NotFound;
        return rs;
    }

    if (fs.isDir(rs.path)) {
        char gmpath[FOSSORIAL_MAX_SELECTOR + 16];
        size_t plen = strlen(rs.path);
        if (plen > 0 && rs.path[plen-1] != '/') rs.path[plen++] = '/';
        rs.path[plen] = 0;

        // Check for gophermap inside
        size_t pos = plen;
        for (size_t i = 0; FOSSORIAL_GOPHERMAP[i]; ++i) gmpath[pos++] = FOSSORIAL_GOPHERMAP[i];
        gmpath[pos] = 0;
        for (size_t i = 0; i < plen; ++i) gmpath[i] = rs.path[i];

        if (fs.exists(gmpath)) {
            rs.result = SelectorResult::Gophermap;
        } else {
            rs.result = SelectorResult::Menu;
        }
    } else {
        rs.result = SelectorResult::File;
    }

    return rs;
}

// ─── Serve a gophermap ──────────────────────────────────────────────────

// Reads a gophermap file line by line, parses each line,
// and calls output() with the rendered menu entry.
template <typename FileSystem, MenuFormat Fmt, typename Output>
static inline bool serveGophermap(const FileSystem& fs,
                                   const char* gophermap_path,
                                   const char* dir_path,
                                   Output&& output)
{
    // Read the entire gophermap file
    char buf[4096];
    int nread = fs.readFile(gophermap_path, buf, sizeof(buf) - 1);
    if (nread <= 0) return false;
    buf[nread] = 0;

    const char* p = buf;
    const char* end = buf + nread;
    size_t total = 0;
    char line[FOSSORIAL_MAX_SELECTOR + 32];

    while (p < end) {
        // Find end of line
        const char* nl = p;
        while (nl < end && *nl != '\n') ++nl;

        size_t line_len = nl - p;
        // Strip trailing \r
        if (line_len > 0 && p[line_len-1] == '\r') --line_len;

        if (line_len > 0) {
            // Parse the gophermap line
            GopherType type;
            const char* ds; size_t dl;
            const char* ss; size_t sl;
            const char* hs; size_t hl;
            uint16_t port;

            if (parseGophermapLine(p, line_len, type, ds, dl, ss, sl, hs, hl, port)) {
                // Copy display into local buffer (ds points into file buffer,
                // not null-terminated at line boundary)
                char display_buf[FOSSORIAL_MAX_DISPLAY + 1];
                size_t dlen = dl > FOSSORIAL_MAX_DISPLAY ? FOSSORIAL_MAX_DISPLAY : dl;
                for (size_t i = 0; i < dlen; ++i) display_buf[i] = ds[i];
                display_buf[dlen] = 0;

                if (type == GopherType::Info) {
                    size_t n = buildMenuLine<Fmt>(line, sizeof(line), type,
                                                   display_buf, "", "", 0);
                    if (n > 0) { output(line, n); total += n; }
                } else {
                    char sel_buf[FOSSORIAL_MAX_SELECTOR + 1];
                    size_t slen = sl;
                    if (slen > FOSSORIAL_MAX_SELECTOR) slen = FOSSORIAL_MAX_SELECTOR;
                    for (size_t i = 0; i < slen; ++i) sel_buf[i] = ss[i];
                    sel_buf[slen] = 0;

                    char host_buf[128];
                    size_t hlen = hl > 127 ? 127 : hl;
                    for (size_t i = 0; i < hlen; ++i) host_buf[i] = hs[i];
                    host_buf[hlen] = 0;

                    // If host is empty and dir_path is set, try to make
                    // the selector relative to the gopher root
                    char full_sel[FOSSORIAL_MAX_SELECTOR + 1];
                    const char* final_sel = sel_buf;

                    if (hlen == 0 && sel_buf[0] != '/' && dir_path) {
                        // Relative selector: prepend dir_path
                        size_t dp_len = 0;
                        while (dir_path[dp_len]) ++dp_len;

                        // Strip trailing '/'
                        while (dp_len > 0 && dir_path[dp_len-1] == '/') --dp_len;

                        if (dp_len + 1 + slen <= FOSSORIAL_MAX_SELECTOR) {
                            size_t fi = 0;
                            for (size_t i = 0; i < dp_len; ++i) full_sel[fi++] = dir_path[i];
                            full_sel[fi++] = '/';
                            for (size_t i = 0; i < slen; ++i) full_sel[fi++] = sel_buf[i];
                            full_sel[fi] = 0;
                            final_sel = full_sel;
                        }
                    }

                    size_t n;
                    if constexpr (Fmt == MenuFormat::Standard) {
                        n = buildMenuLine<Fmt>(line, sizeof(line), type,
                                               display_buf, final_sel,
                                               hlen > 0 ? host_buf : "local",
                                               port > 0 ? port : 70);
                    } else {
                        n = buildMenuLine<Fmt>(line, sizeof(line), type,
                                               display_buf, final_sel);
                    }
                    if (n > 0) { output(line, n); total += n; }
                }
            }
        }

        p = nl + 1; // skip past '\n'
    }

    return total > 0;
}

// ─── Serve a selector (high-level dispatch) ─────────────────────────────

enum class ResponseType {
    Menu,     // Callback receives menu lines
    File,     // Callback receives file content (may be chunked)
    NotFound, // Callback receives nothing; caller should send error
};

template <typename FileSystem, MenuFormat Fmt, typename Output>
static inline ResponseType serveSelector(const FileSystem& fs,
                                          const char* selector,
                                          size_t sel_len,
                                          Output&& output)
{
    auto rs = resolveSelector(fs, selector, sel_len);

    switch (rs.result) {
    case SelectorResult::Menu: {
        // Auto-generate directory listing
        const char* prefix = selector;
        if (sel_len == 0 || (sel_len == 1 && selector[0] == '/'))
            prefix = "";
        generateDirectoryMenu<FileSystem, Fmt>(fs, rs.path, prefix, output);
        return ResponseType::Menu;
    }

    case SelectorResult::Gophermap: {
        // Serve via gophermap
        // rs.path is the directory; need to find gophermap inside
        char gmpath[FOSSORIAL_MAX_SELECTOR + 16];
        size_t plen = strlen(rs.path);
        if (plen > FOSSORIAL_MAX_SELECTOR) return ResponseType::NotFound;
        for (size_t i = 0; i < plen; ++i) gmpath[i] = rs.path[i];
        // Ensure trailing slash
        if (plen == 0 || gmpath[plen-1] != '/') gmpath[plen++] = '/';
        // Append gophermap filename
        for (size_t i = 0; FOSSORIAL_GOPHERMAP[i]; ++i) gmpath[plen++] = FOSSORIAL_GOPHERMAP[i];
        gmpath[plen] = 0;

        if (!serveGophermap<FileSystem, Fmt>(fs, gmpath, rs.path, output))
            return ResponseType::NotFound;
        return ResponseType::Menu;
    }

    case SelectorResult::File: {
        // Read file and output raw content
        char buf[512];
        int nread = fs.readFile(rs.path, buf, sizeof(buf));
        if (nread <= 0) return ResponseType::NotFound;
        output(buf, (size_t)nread);
        return ResponseType::File;
    }

    default:
        return ResponseType::NotFound;
    }
}

} // namespace fossorial

#endif // FOSSORIAL_GOPHER_FILESYSTEM_H
