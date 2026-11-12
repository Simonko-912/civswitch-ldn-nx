#!/usr/bin/env node
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Gezine. See LICENSES/Gezine-MIT.txt.
// Generates source/chat_payload.h from installer/core.js for the LDN chat
// transport (CivSwitchLDN protocol, Gezine compress.js lineage).
//
// The payload is wrapped in a tiny preamble that arms LDN-only mode before
// the loader body runs. The loader (installer/core.js) starts its TCP
// listener on port 9026 immediately when window.__ldn_loader is true,
// without waiting for a Wi-Fi address - which is what makes the whole
// one-click install work on an LDN-only console (no WiFi/LAN required).
//
// usage: node generate_chat_payload.js
// needs: terser (npm ci --ignore-scripts)
const fs = require("fs");
const path = require("path");

const HERE = __dirname;
const compress = require(path.join(HERE, "compress_chat.js"));

const CORE = fs.readFileSync(path.join(HERE, "core.js"), "utf8").replace(/\r\n/g, "\n");

// Runs first when the payload is evaluated from LDN chat. The persistent
// trigger path (Load Game) does not set this, so the loader keeps its
// original Wi-Fi behaviour there.
const PREAMBLE =
    "window.__ldn_loader = true;\n";

const SOURCE = PREAMBLE + CORE;

compress(SOURCE).then((lines) => {
    const header =
        "// Generated from installer/core.js (Gezine CivSwitch) via compress_chat.js.\n" +
        "#ifndef CIVSWITCH_CHAT_PAYLOAD_H\n" +
        "#define CIVSWITCH_CHAT_PAYLOAD_H\n" +
        "static const char *const chat_payload_lines[] = {\n" +
        lines.map((l) => "    " + JSON.stringify(l)).join(",\n") +
        ",\n};\n" +
        "static const unsigned chat_payload_count = sizeof(chat_payload_lines) / sizeof(chat_payload_lines[0]);\n" +
        "#endif\n";
    fs.writeFileSync(path.join(HERE, "..", "source", "chat_payload.h"), header);
    console.log(`wrote source/chat_payload.h (${lines.length} chat lines)`);
}).catch((e) => {
    console.error(e);
    process.exit(1);
});
