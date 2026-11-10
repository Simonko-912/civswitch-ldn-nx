# Embedded persistent-installer assets

These files reproduce the one-click save installation used by the current NRO.

- `core.js`, `install.py` and `compress_stage.js` are adapted/copied from
  [ItsNerisma/CivSwitchLDN](https://github.com/ItsNerisma/CivSwitchLDN) commit
  `91668edc8141f6764294fd969bf8373b625626ba`, which preserves their original
  [Gezine/CivSwitch](https://github.com/Gezine/CivSwitch) MIT attribution.
- `../data/save_template.bin` is the byte-identical upstream template. Its
  SHA-256 is
  `32fa8a0d6dd0a480d8e541e63ea44c4619bc6f7be4282cfc3cb6a4f6d305dfae`.
- `generate_assets.py` wraps and compresses `core.js`, inserts the result into
  the upstream installer block and writes `../source/installer_payload.h`.
- v1.2.0 wraps the encoded `XSSP` stage in a hidden HTML span. The loader still
  reads the stage from `document.body.textContent`, while the game renderer no
  longer has to lay out the full hexadecimal payload. This local adaptation is
  based on [Gezine/CivSwitch PR #3](https://github.com/Gezine/CivSwitch/pull/3)
  by ItsNerisma.

Regenerate with:

```sh
npm ci --ignore-scripts
python3 generate_assets.py
```

The generated header is checked in so users only need devkitPro to build the
NRO. See `../LICENSE` and `../LICENSES/Gezine-MIT.txt` for license terms.
