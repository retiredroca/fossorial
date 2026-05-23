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

## LoRa protocol

Clients send selectors as `PAYLOAD_TYPE_TXT_MSG` starting with `/`. The server
responds with chunked `PAYLOAD_TYPE_TXT_MSG` packets (3-byte header, ~175
bytes of Gopher text per chunk). See `firmware/meshcore/build/README.md` for
wire format details.

## License

MIT License

Copyright (c) 2026

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
