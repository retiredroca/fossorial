#include <Arduino.h>
#include <Mesh.h>
#include "GopherMesh.h"

StdRNG fast_rng;
SimpleMeshTables tables;
GopherMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() { while (1); }

static char command[160];

void setup() {
    Serial.begin(115200);
    delay(1000);
    board.begin();

    if (!radio_init()) { halt(); }

    fast_rng.begin(radio_get_rng_seed());

    FILESYSTEM* fs;
#if defined(NRF52_PLATFORM)
    InternalFS.begin();
    fs = &InternalFS;
    IdentityStore store(InternalFS, "");
#elif defined(RP2040_PLATFORM)
    LittleFS.begin();
    fs = &LittleFS;
    IdentityStore store(LittleFS, "/identity");
    store.begin();
#elif defined(ESP32)
    SPIFFS.begin(true);
    fs = &SPIFFS;
    IdentityStore store(SPIFFS, "/identity");
    store.begin();
#else
    #error "need to define filesystem"
#endif

    if (!store.load("_main", the_mesh.self_id)) {
        the_mesh.self_id = radio_new_identity();
        int count = 0;
        while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 ||
                              the_mesh.self_id.pub_key[0] == 0xFF)) {
            the_mesh.self_id = radio_new_identity();
            count++;
        }
        store.save("_main", the_mesh.self_id);
    }

    Serial.print("Gopher node ID: ");
    mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE);
    Serial.println();

    command[0] = 0;
    sensors.begin();
    the_mesh.begin(fs);
}

void loop() {
    int len = strlen(command);
    while (Serial.available() && len < (int)sizeof(command) - 1) {
        char c = Serial.read();
        if (c != '\n') {
            command[len++] = c;
            command[len] = 0;
        }
        Serial.print(c);
    }
    if (len == (int)sizeof(command) - 1) {
        command[sizeof(command) - 1] = '\r';
    }

    if (len > 0 && command[len - 1] == '\r') {
        command[len - 1] = 0;
        char reply[160];
        the_mesh.handleCommand(0, command, reply);
        if (reply[0]) {
            Serial.print("  -> ");
            Serial.println(reply);
        }
        command[0] = 0;
    }

    the_mesh.loop();
    sensors.loop();
    rtc_clock.tick();
}
