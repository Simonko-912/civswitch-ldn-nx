# CivSwitch-LDN-NX v0.9.1

<p align="center">
  <img src="logo.png" alt="CivSwitch-LDN-NX logo" width="220">
</p>

> [!IMPORTANT]
> [CivSwitch](https://github.com/Gezine/CivSwitch) is an exploit by
> [Gezine](https://github.com/Gezine). This program delivers it from a
> Switch 1 over local wireless (LDN), instead of from a Linux PC like
> [CivSwitchLDN](https://github.com/ItsNerisma/CivSwitchLDN).

v0.9.1 fixes the in-app version label: the running program now reports v0.9.1
instead of v0.8. There are no functional changes.

v0.9 adds the project logo: it is now embedded as the homebrew application
icon and shown at the top of this README. The homebrew functionality itself is
unchanged from v0.8.

v0.8 adds a one-button persistent-save installer. After the Switch 1 joins the
Civilization VII wireless lobby, pressing **ZR** sends the Remote JS Loader,
restores normal Wi-Fi, finds the Switch 2 on the LAN, uploads the bundled save
template and writes `XSTRIG.Civ7Save` automatically.

## Demo

https://github.com/user-attachments/assets/9b8acf39-8e5a-4d36-a307-a0bdff474577

## Critical save warning

**The ZR operation deletes every Civilization VII single-player save and
replaces them with the persistent trigger save. Back up wanted saves first.**

The original **R** action remains available and only sends the Remote JS Loader;
it does not modify savedata.

## Requirements

- A Switch 1 able to run `.nro` homebrew.
- A Switch 2 with the physical Switch 1 edition of Civilization VII 1.0.0.
- Both consoles must already have a connection profile for the same Wi-Fi/LAN.
- Launching through an application title override is recommended for the LDN and
  network service context.

The cartridge stays in Switch 2 throughout the process. `prod.keys`, Linux,
Python and Node.js are not required when using the built NRO.

## One-click persistent install

1. Back up any Civilization VII single-player saves you want to keep.
2. On Switch 2, open **MULTIPLAYER > WIRELESS > CREATE GAME > HOST LOBBY**.
3. On Switch 1, launch `civswitch-ldn-nx.nro`. Press **X** to rescan if needed,
   choose the lobby with Up/Down, then press **A**.
4. Wait for `Game lobby: JOINED` and confirm the extra player appears on
   Switch 2.
5. Press **ZR once**. Do not close the game or either console while chat lines,
   Wi-Fi restoration, template upload and save writing are in progress.
6. Wait for `Persistent trigger save installed` and `DONE` on Switch 1.
7. Restart Civilization VII on Switch 2 and choose **Load Game**. Building the
   list triggers the persistent Remote JS Loader on port 9026.

If automatic discovery times out, verify that both consoles reconnect to the
same LAN and that client isolation is disabled on the access point. Press X to
return to LDN scanning and try again.

## Controls

| Button | Action |
|---|---|
| Up / Down | Select a scanned LDN network |
| A | Join the selected lobby and register as a player |
| ZR | One-click loader delivery and persistent save installation |
| R | Send the Remote JS Loader only; do not modify saves |
| Y | Send one optional chat test |
| X | Rescan or return to LDN mode after an installer attempt |
| B | Leave the lobby |
| + | Exit |

## v0.8 implementation

- Carries forward the v0.7 transport hardening: ordered receive buffering,
  missing-packet requests, a rolling retransmission cache, quiet-period delivery
  confirmation, packet-size checks and explicit refused/closed/kicked errors.
- Embeds Gezine's `save_template.bin` and the persistent installer logic used by
  CivSwitch and CivSwitchLDN.
- Restores Wi-Fi through `nifm:u`, scans a local subnet of at most 512 addresses
  for a verified Remote JS Loader on TCP 9026, and does not rely on a fixed IP.
- Base64-encodes the template on the Switch 1 and sends it in four chunks under
  the loader's 500 KiB per-connection limit.
- Verifies every loader response and reports success only after the Switch 2
  emits `INSTALL DONE` following its savedata commit.

The v0.8 NRO builds successfully and its generated payload passes upstream's
offline decompress/round-trip check. The full loader-to-save path was verified
on real hardware in a two-console test.

## Building

Install devkitPro with the `switch-dev` group, then run:

```sh
DEVKITPRO=/opt/devkitpro make
```

The generated `source/installer_payload.h` and embedded save template are
included, so an ordinary build does not need Node.js. To regenerate the
installer payload from the included upstream source:

```sh
cd installer
npm ci --ignore-scripts
python3 generate_assets.py
cd ..
DEVKITPRO=/opt/devkitpro make clean all
```

The generator performs an offline exact decode check before updating the C
header.

## Lineage and licenses

- [ItsNerisma/CivSwitchLDN](https://github.com/ItsNerisma/CivSwitchLDN)
  (GPL-3.0-only) is the direct LDN baseline.
- [Gezine/CivSwitch](https://github.com/Gezine/CivSwitch) (MIT) supplies the
  exploit, Remote JS Loader, persistent installer and save template.
- [kinnay/LDN](https://github.com/kinnay/LDN) (GPL-3.0) is the protocol reference
  used by CivSwitchLDN.

This combined application and the original code in this port are licensed under
GNU GPL version 3 only. Reused CivSwitch files retain Gezine's MIT license; see
`LICENSES/Gezine-MIT.txt`. Complete corresponding source and generation files
are included in this repository.
