# Embedded persistent-installer assets

These files make the v0.8 NRO's one-click save installation reproducible.

- `core.js`, `install.py` and `compress_stage.js` are adapted/copied from
  [ItsNerisma/CivSwitchLDN](https://github.com/ItsNerisma/CivSwitchLDN) commit
  `91668edc8141f6764294fd969bf8373b625626ba`, which preserves their original
  [Gezine/CivSwitch](https://github.com/Gezine/CivSwitch) MIT attribution.
- `../data/save_template.bin` is the byte-identical upstream template. Its
  SHA-256 is
  `32fa8a0d6dd0a480d8e541e63ea44c4619bc6f7be4282cfc3cb6a4f6d305dfae`.
- `generate_assets.py` wraps and compresses `core.js`, inserts the result into
  the upstream installer block and writes `../source/installer_payload.h`.

Regenerate with:

```sh
npm ci --ignore-scripts
python3 generate_assets.py
```

The generated header is checked in so users only need devkitPro to build the
NRO. See `../LICENSE` and `../LICENSES/Gezine-MIT.txt` for license terms.
