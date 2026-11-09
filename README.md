# CivSwitch-LDN-NX v1.1.0

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
- Both consoles must already have saved connection profiles for the same
  Wi-Fi/LAN network.
- For the **ZR one-click persistent-save installation**, both consoles must
  have **"Join this network automatically"** enabled for that Wi-Fi/LAN
  network.
- Launch the app from the **Album** on Switch 1. Alternatively, a Civilization
  VII title override can be used. Do not use another game's title override: it
  can scan nearby LDN rooms, but it cannot complete the Civilization VII
  game-level join.

The cartridge stays in Switch 2 throughout the process. `prod.keys`, Linux,
Python and Node.js are not required when using the built NRO.

## One-click persistent install

1. Back up any Civilization VII single-player saves you want to keep.
2. On both consoles, confirm that the same Wi-Fi/LAN network is already saved
   and has **"Join this network automatically"** enabled.
3. On Switch 2, open **MULTIPLAYER > WIRELESS > CREATE GAME > HOST LOBBY**.
4. On Switch 1, open the Album and launch `civswitch-ldn-nx.nro`. Alternatively,
   use Civilization VII as the title override. Press **X** to rescan if needed,
   choose the lobby with Up/Down, then press **A**.
5. Wait for `Game lobby: JOINED` and confirm the extra player appears on
   Switch 2.
6. Press **ZR once**. Do not close the game or either console while chat lines,
   Wi-Fi restoration, template upload and save writing are in progress.
7. Wait for `Persistent trigger save installed` and `DONE` on Switch 1.
8. Restart Civilization VII on Switch 2 and choose **Load Game**. Building the
   list triggers the persistent Remote JS Loader.

## Troubleshooting

For the **ZR** installation flow, Switch 2 leaves the temporary LDN connection
after receiving the loader and must reconnect to the Wi-Fi/LAN network.
Switch 1 then reconnects to the same network, finds Switch 2, and uploads the
save file.

If automatic discovery times out:

- Confirm that both consoles have reconnected to the same Wi-Fi/LAN network.
- On both consoles, confirm that **"Join this network automatically"** is
  enabled for that network.
- Make sure client isolation is disabled on the access point.
- Press **X** to return to LDN scanning and try again.

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
