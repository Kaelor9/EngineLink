# EngineLink — editable hardware design

Source: owner-supplied `ESP32-S3-WROOM-1-N8R8(9).zip`, reviewed 2026-11-10. The editable design was saved by KiCad 8.

## Files

- `ESP32-S3-WROOM-1-N8R8.kicad_pro` — KiCad project settings.
- `ESP32-S3-WROOM-1-N8R8.kicad_sch` — editable electrical schematic.
- `ESP32-S3-WROOM-1-N8R8.kicad_pcb` — editable PCB design (4 copper layers, KiCad 8 format).
- `ESP32-S3-WROOM-1-N8R8.kicad_dru` — original KiCad design rules; check them against the intended insulation specification.
- `bom/ibom.html` — standalone interactive BOM/placement viewer (open locally in a browser).

The source archive also contained 28 historical backup ZIPs, a footprint cache, `.kicad_prl` per-user editor preferences, and `.lck` locks; these are omitted. The newest backup (`2026-11-07_123455`) matches the four editable files byte-for-byte.

This import does **not** include current manufacturing Gerber/drill exports, a validated purchase-ready BOM, or a complete ERC/DRC report. Earlier Gerber reviews in this repo are historical evidence, not manufacturing approval. These original KiCad files have been preserved unchanged and have not yet been independently validated.

## Opening

Open `ESP32-S3-WROOM-1-N8R8.kicad_pro` in KiCad 8 or newer. Some 3D model paths may require installed KiCad or vendor libraries; check visually. Export Gerber/drill/BOM from the verified board revision rather than using old production archives.

See `SHA256SUMS.txt` for checksums of the unmodified files extracted from the owner's ZIP.
