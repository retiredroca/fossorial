# Fossorial

Gopher protocol serving over LoRa mesh networks.

Fossorial serves Gopher content (menus, text files, directories) on
[MeshCore](https://github.com/iamromand/MeshCore) LoRa nodes, using the same
`gophermap` files that traditional Gopher servers use. Content is authored on
a desktop and uploaded to the node's filesystem, then served via LoRa packets
to mesh clients.

## Components

```
fossorial/
├── include/fossorial/          # Core library (header-only C++17)
│   ├── GopherCore.h            #   types, menu builder, chunker, selectors
│   └── GopherFileSystem.h      #   gophermap parser, selector resolution
├── src/
│   └── fossorial_server.cpp    # Standalone TCP Gopher daemon (for authoring)
├── test/
│   ├── test_gophercore.cpp
│   └── test_gopherfilesystem.cpp
├── examples/
│   └── basic_usage.cpp
├── firmware/
│   └── meshcore/               # MeshCore LoRa node firmware
│       ├── GopherMesh.h        #   Gopher server class + FS adapter
│       ├── GopherMesh.cpp      #   LoRa packet handlers, chunked responses
│       ├── main.cpp            #   Platform entry (RAK4631 / RP2040 / ESP32)
│       └── platformio.ini      #   Build config
└── cli/
    └── fossorial-cli.py        # Content generation & upload tool
```

## Quick start (TCP server)

```bash
cmake -B build && cmake --build build
./build/fossorial-server 7070 ./gopher-site
# Connects on gopher://localhost:7070
```

Creates a default Gopher site in `./gopher-site/` on first run.

## Quick start (LoRa node)

Copy `firmware/meshcore/` into a MeshCore checkout:

```bash
cp firmware/meshcore/*.{h,cpp} firmware/meshcore/main.cpp <MESHROOT>/examples/fossorial/
cp -r firmware/meshcore/include/ <MESHROOT>/examples/fossorial/
```

Then build with PlatformIO targeting a RAK4631, RP2040, or ESP32 board.

## Gophermap format

Files are standard Gophermaps:

```
iWelcome to Fossorial
1Documents\t/docs
0README\t/readme.txt
```

Place them at `/gopher/gophermap`, `/gopher/docs/gophermap`, etc. on the
node's filesystem. Directories without a `gophermap` auto-generate a listing.

## Meshtastic

[Meshtastic](https://meshtastic.org) is an open-source LoRa mesh platform with
its own radio protocol and API (GPL v3). Fossorial has a [Meshtastic
module](firmware/meshtastic/FossorialModule.h) that serves Gopher content on
the `PRIVATE_APP` port — see `firmware/meshtastic/` for integration steps.

## MeshOS

[MeshOS](https://meshcore.co.uk/meshos.html) is a standalone UI firmware for
LilyGo T-Deck / T-Pager / T-Display P4 devices that provides chat, maps, and
encrypted messaging over MeshCore. It is **not an operating system with an
app-loading mechanism** — there is no SDK, no plugin system, and no way to
install third-party apps on it.

Fossorial works alongside MeshOS in several ways:

| Approach | Description |
|----------|-------------|
| **Infrastructure node** | Flash Fossorial on a RAK4631 (headless). It serves Gopher content on the mesh like a Room Server. Any MeshOS or MeshCore device can request content by sending a selector as a text message. The server is usable today without any MeshOS changes. |
| **Standalone client** | Build a separate firmware (same pattern) that receives Gopher content and displays it — on a serial terminal, or with a display for a T-Deck-style experience. |
| **Companion app client** | The MeshOS mobile/desktop apps (Android/iOS/web) connect to a Companion Radio node via BLE/USB. A Gopher client could be built into those apps, talking to the Fossorial server over the mesh via the companion protocol. |

## LoRa protocol

Clients send selectors as `PAYLOAD_TYPE_TXT_MSG` starting with `/`. The server
responds with chunked `PAYLOAD_TYPE_TXT_MSG` packets (3-byte header, ~175
bytes of Gopher text per chunk). See `firmware/meshcore/build/README.md` for
wire format details.

## License

Fossorial is **MIT** — see [LICENSE](LICENSE).

| Platform | Dependency license | Fossorial module license |
|----------|-------------------|--------------------------|
| Core library | — | MIT |
| TCP server | — | MIT |
| MeshCore firmware | MIT | MIT (compatible) |
| Meshtastic module | GPL v3 | MIT (GPL-compatible) |
