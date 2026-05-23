# Fossorial Meshtastic Module

## Integration

```bash
# 1. Clone Meshtastic firmware
git clone --depth 1 https://github.com/meshtastic/firmware.git
cd firmware
git submodule update --init

# 2. Copy Fossorial module source
cp FossorialModule.h FossorialModule.cpp src/modules/
cp -r ../../include/fossorial/ src/modules/

# 3. Copy Fossorial core headers to the module include path
#    (add -I src/modules/fossorial/ to build flags, or symlink)

# 4. Register in src/modules/Modules.cpp:

#    Near the top, with other includes:
#    #if !MESHTASTIC_EXCLUDE_FOSSORIAL
#    #include "modules/FossorialModule.h"
#    #endif

#    Inside setupModules():
#    #if !MESHTASTIC_EXCLUDE_FOSSORIAL
#        new FossorialModule();
#    #endif

# 5. Build
pio run -e tbeam -t upload
```

## Gopher content

Place gophermap files and content under `/gopher/` on the device filesystem.
The default root is `/gopher/gophermap`. Subdirectories need their own
`gophermap` or will auto-generate a listing.

## Protocol

### Request (client → node)

Send a `DATA` packet on port `PRIVATE_APP` (256):

```
Byte 0:     'G' (0x47) — Fossorial marker
Byte 1..N:  Gopher selector (e.g. "/docs" or "/welcome.txt")
```

### Response (node → client)

One or more `DATA` packets on port `PRIVATE_APP`:

```
Byte 0:     'G' (0x47) — marker
Byte 1:     flags — 0=Sole, 1=First, 2=Mid, 4=Last
Byte 2:     seq   — sequence number (0–255)
Byte 3..N:  Gopher content (menu text or file data)
```

Reassemble by grouping chunks with the same `from` node, ordering by `seq`.
Strip the 3-byte header from each chunk.

## Gophermap format

Standard Gophermap (RFC 1436), host/port optional:

```
iWelcome to Fossorial
1Documents\t/docs
0README\t/readme.txt
```

## Notes

- The module uses port `PRIVATE_APP` (256). Standard Meshtastic nodes
  ignore packets on this port, so no interference with normal mesh traffic.
- The module does **not** register with `moduleConfig`; it is always active
  when compiled in. Add a runtime toggle if desired.
- Maximum Gopher response is 4096 bytes across all chunks.
- Each chunk carries up to 177 bytes of content (180 minus 3 header bytes).
