# CivSwitch-LDN-NX

Native Switch 1 homebrew that delivers the CivSwitch JavaScript loader over
local wireless (LDN). No PC, no `prod.keys` — just a Switch 1 running
homebrew and a Switch 2 hosting a Civilization VII lobby.

## Lineage

This is an independent native port rather than a GitHub fork:

- **[ItsNerisma/CivSwitchLDN](https://github.com/ItsNerisma/CivSwitchLDN)**
  (GPL-3.0-only) — the direct baseline; it delivers the same loader from a
  Linux PC over LDN.
- **[Gezine/CivSwitch](https://github.com/Gezine/CivSwitch)** (MIT) — the
  original Civilization VII userland exploit, Remote JS Loader and persistent
  installer.
- **[kinnay/LDN](https://github.com/kinnay/LDN)** (GPL-3.0) — the LDN protocol
  reference used by CivSwitchLDN.

All upstream copyright and license notices are preserved in `LICENSE` and
`LICENSES/`.

## How it works

1. Switch 2, with the physical Switch 1 edition of Civilization VII
   (game version 1.0.0), hosts a wireless lobby.
2. Switch 1 runs this `.nro`, scans LDN networks and joins the lobby through
   the native `ldn:u` system service.
3. The app sends the 19 compressed JS-loader chat messages, then disconnects
   so Switch 2 can restore normal Wi-Fi.
4. Switch 2 shows the Remote JS Loader (`CivSwitch 1.0 by Gezine`, awaiting a
   payload on port 9026). The standard CivSwitch workflow applies from there.

The game cartridge remains in Switch 2 the whole time.

## Requirements

- A Switch 1 able to run `.nro` homebrew.
- A Switch 2 with the physical Switch 1 edition of **Civilization VII
  version 1.0.0** inserted.
- Both consoles within local wireless range.

## Usage

1. On Switch 2, open Civilization VII and go to
   **MULTIPLAYER > WIRELESS > CREATE GAME > HOST LOBBY**.
2. Copy `civswitch-ldn-nx.nro` to `sd:/switch/civswitch-ldn-nx.nro` on
   Switch 1 and launch it from Homebrew Menu. An application title override
   is recommended to provide an LDN app context.
3. Press `X` to rescan if needed, select the lobby with Up/Down, then press
   `A` to join. Wait for `Game lobby: JOINED` and confirm the new player
   appears in the Switch 2 player list.
4. Press `R` once to send the loader. Progress counts 1/19 through 19/19,
   after which the app disconnects automatically.
5. On Switch 2, verify the loader screen shows `CivSwitch 1.0 by Gezine` and
   `Awaiting payload at <Wi-Fi IP>:9026`.

`Y` sends an optional plain-chat test message (`Civ7-LDN-Hello`); it is not
needed to join or to send the loader.

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

## Verification status

v0.6 was tested twice on real hardware: LDN scanning, lobby registration
visible on Switch 2, optional test chat while the connection stayed open, and
the loader display on Switch 2 after all 19 messages were sent.

Interpreting the on-screen results:

- `Game lobby: JOINED` means the game welcome was received and registration
  frames were sent; always verify the player list on Switch 2 as the ground
  truth.
- `Loader lines sent` only means the local sends completed. The loader display
  on Switch 2 is the proof that the JavaScript actually ran.

This release does **not** install a persistent save trigger and does not
modify game saves. Once the loader is running, use the standard upstream
CivSwitch tooling if you want persistence.

## Implementation notes

- The passphrase matches upstream `ldn/connection.py`. Because all LDN
  cryptography runs inside the Switch OS via native libnx `ldn:u`, this
  program never needs `prod.keys`.
- The sender uses nonblocking UDP reads and paces chat lines at 10 ms each,
  matching CivSwitchLDN's default. The game handshake is scheduled
  100/200/400 ms after the welcome packet, with chat eligible at 500 ms.
- The chat payload is generated from upstream `compress.js` and
  `payloads/core.js`; the generated lines are embedded in
  `source/chat_payload.h` and kept in `payload-lines.json`.
- Source baseline: `safuentees/CivSwitchLDN` commit
  `9537a9e784dfaa2ed7271dde3ce5148ef59460bb`.

## Building from source

Install devkitPro with the `switch-dev` group (devkitA64 and libnx), then run
`make` in this directory. The output is `civswitch-ldn-nx.nro`.

## License

This project as a combined work, and all native port code, are licensed under
**GPL-3.0-only**, consistent with the CivSwitchLDN license. The reused
CivSwitch exploit components retain Gezine's MIT license; see
`LICENSES/Gezine-MIT.txt`. The full GPL-3.0 text is in `LICENSE`, and the
complete source corresponding to every released binary is available in this
repository.
