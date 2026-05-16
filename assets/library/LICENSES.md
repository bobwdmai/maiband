# BandForge Starter Library Licenses

This repository ships an original starter loop pack generated programmatically by BandForge tooling:

- `assets/library/loops/*.wav`: original synthesized WAV loops created by `tools/generate_loop_pack.py`.
- MIDI loop patterns embedded in `assets/library/manifest.json`: original note patterns created by `tools/generate_loop_pack.py`.
- Preset metadata in `assets/library/manifest.json`: original BandForge preset definitions.

The bundled generated assets are marked in the manifest as `Original BandForge content; generated programmatically for this repository` with attribution `BandForge`.

Do not distribute third-party loop or sample files in `assets/library/loops` or `assets/library/samples` unless each file has a compatible license and attribution entry in `manifest.json`.

Recommended content sources for future packs:

- Original recordings created for BandForge.
- CC0 samples and loops.
- Permissively licensed SFZ/SF2 instruments whose redistribution terms are documented beside the asset.

Every shipped asset should include:

- SPDX-compatible license name where possible.
- Attribution text when required.
- Source URL or internal creation note.
- Proof that commercial redistribution is allowed.
