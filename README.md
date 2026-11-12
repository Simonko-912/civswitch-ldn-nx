# CivSwitch-LDN-NX v1.3.0-LDN

<p align="center">
  <img src="logo.png" alt="CivSwitch-LDN-NX logo" width="220">
</p>

> [!IMPORTANT]
> [CivSwitch](https://github.com/Gezine/CivSwitch) is an exploit by
> [Gezine](https://github.com/Gezine). This program delivers it from a
> Switch 1 over local wireless (LDN), instead of from a Linux PC like
> [CivSwitchLDN](https://github.com/ItsNerisma/CivSwitchLDN).

> [!WARNING]
> Remove **ldn_mitm** (and any other sysmodule that MITMs the LDN service)
> before running this app. This program uses real local wireless: an active
> ldn_mitm replaces the `ldn:u` service, scans the LAN instead of nearby
> consoles, and can crash Atmosphere. On startup the app detects a MITM and
> hard-blocks with removal instructions; it does not run until the module is
> gone and the console has been rebooted.

> [!IMPORTANT]
> **Prefer launching from the Album (applet mode).** The app also opens via a
> hold-R title override (application mode), but that path is supported only
> when overriding **Civilization VII itself**. Overriding any other title can
> scan LDN rooms but cannot complete the game-level join, and the launch-mode
> guard then blocks with instructions to relaunch from the Album. Unless you
> are specifically overriding Civilization VII, open the Album and launch
> `civswitch-ldn-nx.nro` from there.

v1.3.0-LDN makes the whole one-click install work on a console with **only**
local wireless (LDN) - no Wi-Fi/LAN connection required on either console.

Previously the final stage needed Wi-Fi: the Remote JS Loader on Switch 2
waited for a Wi-Fi/LAN address before opening its TCP listener, and Switch 1
restored Wi-Fi and scanned the whole subnet for it. On an LDN-only console
that wait never resolves, so the install dead-ends (the loader stalls at the
"network down - waiting for network.." loop, installer/core.js line 1278 in
v1.2.0).

Now the loader starts its `0.0.0.0:9026` listener immediately - the bind is
wildcard, so it is already reachable on the LDN subnet - and the LDN chat
payload arms LDN-only mode so the loader never waits for Wi-Fi. Switch 1 no
longer touches the Wi-Fi stack: it keeps the LDN room and the lobby socket
open and talks to the loader on the host's own LDN IP, which the lobby
handshake already provides. The persistent trigger save, save template and
upload protocol are unchanged; the R (loader-only) and Y (chat test) actions
are unchanged.

v1.2.0 speeds up launching the persistent trigger save. The encoded loader
stage is now kept in a hidden HTML span: the bootstrap can still read it from
the document text, while the game's renderer no longer lays out the large
hexadecimal payload. Only trigger saves written from now on use the new
layout; a trigger installed by an earlier release keeps working as before.
Normal save loading and the LDN connection flow are unchanged. The hidden-span
technique comes from
[Gezine/CivSwitch PR #3](https://github.com/Gezine/CivSwitch/pull/3) by
ItsNerisma.

v1.1.0 adds a launch-mode guard. When the app is started in application mode
(hold-R title override), it reads the host title through pm:dmnt and continues
only for Civilization VII; any other title, or one that cannot be identified,
shows a blocking screen asking the user to launch from the Album (applet mode).
The Album and Civilization VII override paths are unchanged.

v1.0 adds an ldn_mitm environment guard. At startup the app checks whether the
`ldn:u` service has a MITM. If it does, the app shows a blocking screen with
steps to remove the module and reboot, and never initialises LDN or scans. The
empty-scan screen also lists troubleshooting steps.

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
- ldn_mitm (or any sysmodule that MITMs LDN) must be removed and the console
  rebooted; the app hard-blocks while such a module is resident.
- No Wi-Fi/LAN connection is required; the whole flow runs over local
  wireless. (If both consoles *do* share a Wi-Fi network it simply keeps
  working.)
- Launch the app from the **Album** on Switch 1. Alternatively, a Civilization
  VII title override can be used. Do not use another game's title override: it
  can scan nearby LDN rooms, but it cannot complete the Civilization VII
  game-level join.

The cartridge stays in Switch 2 throughout the process. `prod.keys`, Linux,
Python and Node.js are not required when using the built NRO.

## One-click persistent install (LDN only - no Wi-Fi needed)

1. Back up any Civilization VII single-player saves you want to keep.
2. On Switch 2, open **MULTIPLAYER > WIRELESS > CREATE GAME > HOST LOBBY**.
3. On Switch 1, open the Album and launch `civswitch-ldn-nx.nro`. Alternatively,
   use Civilization VII as the title override. Press **X** to rescan if needed,
   choose the lobby with Up/Down, then press **A**.
4. Wait for `Game lobby: JOINED` and confirm the extra player appears on
   Switch 2.
5. Press **ZR once**. Do not close the game or either console while the chat
   lines, template upload and save writing are in progress.
6. Wait for `Persistent trigger save installed` and `DONE` on Switch 1.
7. Restart Civilization VII on Switch 2 and choose **Load Game**. Building the
   list triggers the persistent Remote JS Loader.

If you are upgrading from an earlier release, repeat this ZR installation once
so the newer trigger save replaces the old one. Remember that the ZR operation
clears every single-player save, so back up first.

## Troubleshooting

The whole ZR flow stays inside the LDN room: Switch 2's loader listens on the
host's own LDN IP and Switch 1 connects to it directly while the lobby is
still up. If it stalls at `Waiting for loader on LDN`:

- Keep both consoles awake and in the same room; the lobby must not drop.
- Keep the consoles close (normal LDN range).
- Confirm Switch 2 actually received the loader chat lines (the game log on
  Switch 2 shows `LDN-only mode: no Wi-Fi required` and
  `listener up on LDN subnet :9026`).
- Press **X** to return to LDN scanning, rejoin and try again.

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
