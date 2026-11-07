# CivSwitch-LDN-NX

> [!IMPORTANT]
> **[CivSwitch](https://github.com/Gezine/CivSwitch) is an exploit by [Gezine](https://github.com/Gezine).** This program delivers it from a Switch 1 over local wireless (LDN), instead of from a Linux PC like [CivSwitchLDN](https://github.com/ItsNerisma/CivSwitchLDN). It is a Switch homebrew program in the nro format and does not need a prod.keys file.

## Demo

https://github.com/user-attachments/assets/82cc269c-7c35-4192-8637-be4c090ea89f

## Lineage

- **[ItsNerisma/CivSwitchLDN](https://github.com/ItsNerisma/CivSwitchLDN)** (GPL-3.0-only) — direct baseline; it delivers the same loader from a Linux PC over LDN.
- **[Gezine/CivSwitch](https://github.com/Gezine/CivSwitch)** (MIT) — the original exploit, Remote JS Loader and persistent installer.
- **[kinnay/LDN](https://github.com/kinnay/LDN)** (GPL-3.0) — LDN protocol reference used by CivSwitchLDN.

Upstream copyright and license notices are kept in [LICENSE](LICENSE) and the LICENSES directory.

## How it works

1. Switch 2 hosts a Civilization VII wireless lobby with the physical cartridge (game version 1.0.0).
2. Switch 1 scans LDN and joins the lobby through the native ldn:u system service.
3. The app sends the 19 compressed JS-loader chat messages, then disconnects so Switch 2 can restore Wi-Fi.
4. Switch 2 shows the Remote JS Loader, awaiting a payload on port 9026. The standard CivSwitch workflow applies from there.

The cartridge stays in Switch 2 the whole time.

## Requirements

- A Switch 1 able to run nro homebrew.
- A Switch 2 with the **physical Switch 1 edition of Civilization VII, version 1.0.0** inserted.
- Both consoles within local wireless range.

## Usage

### 1. Setup the game

On Switch 2, open Civilization VII and go to **MULTIPLAYER > WIRELESS > CREATE GAME > HOST LOBBY**.

### 2. Launch the app

Copy civswitch-ldn-nx.nro to sd:/switch/civswitch-ldn-nx.nro on Switch 1 and launch it from Homebrew Menu. An application title override is recommended to provide an LDN app context.

### 3. Join the lobby

Press X to rescan if needed, select the lobby with Up/Down, then press A. Wait for Game lobby: JOINED and check the new player in the Switch 2 player list.

### 4. Send the loader

Press R once. Progress counts 1/19 to 19/19, then the app disconnects automatically. On Switch 2, the loader screen should show CivSwitch 1.0 by Gezine and Awaiting payload at the Wi-Fi IP on port 9026.

Y sends one optional Civ7-LDN-Hello chat message; it isn't needed to join or send the loader.

### Controls

| Button | Action |
|---|---|
| Up / Down | Select a scanned network |
| A | Join the selected lobby and register as a player |
| Y | Send one optional test chat message |
| R | Send the 19 JS-loader chat messages |
| X | Rescan LDN networks |
| B | Leave the lobby |
| + | Exit the application |

## Status

v0.7 was verified on real hardware in a two-console test. It keeps the v0.6
timing and adds stricter packet ordering, retransmission handling,
session-close detection and packet-size checks.

v0.6 was tested twice on real hardware: LDN scanning, lobby registration on Switch 2, the optional test chat with the connection open, and the loader display after all 19 messages.

- Game lobby: JOINED means the welcome was received and registration frames were sent; verify the player list on Switch 2.
- Loader lines sent only means the local sends completed. The loader display on Switch 2 is the proof that the JavaScript ran.

This version does not install a persistent save trigger or modify game saves. For persistence, use the upstream CivSwitch tooling once the loader is running.

## Implementation notes

- The passphrase follows upstream ldn/connection.py. LDN cryptography runs inside the Switch OS through libnx ldn:u, so prod.keys is not needed.
- Nonblocking UDP reads; chat lines are paced at 10 ms, matching CivSwitchLDN. The handshake runs 100/200/400 ms after the welcome packet, with chat eligible at 500 ms.
- Incoming game packets are ordered by sequence number. Future packets are
  buffered, missing packets are requested every 500 ms, and sent packets are
  retained in a 64-entry rolling retransmission cache.
- After the last chat line, the connection closes after one quiet second. A
  retransmission restarts that quiet period, with a three-second hard limit.
- Refused, closed and kicked sessions are reported instead of being shown as a
  successful loader send.
- The chat payload comes from upstream compress.js and payloads/core.js; generated lines are embedded in source/chat_payload.h and kept in payload-lines.json.
- Source baseline: safuentees/CivSwitchLDN at commit 9537a9e784dfaa2ed7271dde3ce5148ef59460bb.

## Building from source

Install devkitPro with the switch-dev group (devkitA64 and libnx), then run make. The output is civswitch-ldn-nx.nro.

## License

This program as a combined application, and the original code in this port, are licensed under the **GNU GPL version 3 only (GPL-3.0-only)**. See [LICENSE](LICENSE).

The reused CivSwitch files retain **Gezine's MIT license and copyright**; see [LICENSES/Gezine-MIT.txt](LICENSES/Gezine-MIT.txt). The complete source corresponding to every released binary is available in this repository. The software is provided without warranty; see the license texts for the full terms.
