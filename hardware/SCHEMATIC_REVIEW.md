# EngineLink schematic review — 2026-11-05

Source reviewed: user-supplied one-page `EngineLink_schematic_Diagram.pdf` (KiCad 8 schematic print). **The original PDF is not stored in this repository yet.** Suggested repository destination: `hardware/EngineLink_schematic_Diagram.pdf`. This review is based on the rendered sheet, not editable KiCad netlists or an ERC run.

## Architecture visible in the schematic

- ESP32-S3-WROOM-1-N8R8 with native USB-C (GPIO19/GPIO20), 3.3 V LDO and 5 V buck power path.
- Two J1708 ports: ADuM1201 digital isolation, MAX3444E transceivers and separate isolated R05C1TF05S-R 5 V modules.
- Isolated NMEA2000 CAN: ISO1042 + isolated 5 V DC/DC, selectable network/external supply jumpers, selectable 120-ohm termination (R37 / J9).
- microSD SPI interface, 5 V addressable SK6805 LED with AHCT level shifter and optically isolated input.

## Review items (requires bench / datasheet confirmation)

1. **GPIO0 BOOT bias:** BOOT1 appears to pull GPIO0 to GND with no *external* pull-up. R1 (10 kΩ) visibly goes to GPIO3, not GPIO0. Confirm module internal pull-up is sufficiently reliable in the actual layout and boot conditions; consider explicit pull-up on GPIO0 if needed.
2. **J1708 TX interlock:** SJ1 / SJ2 bridge DE/RE control to the isolated ground for receive-only operation. Verify the exact MAX3444E DE and /RE truth table, jumper polarity and intended transmit-enable procedure before attaching engine buses. Verify bus protection and fail-safe bias against J1708 electrical requirements.
3. **Isolated rails:** Measure +5V_ISO1 and +5V_ISO2 against GND_ISO1/GND_ISO2 (not main GND), both under load. Historical reports include an unresolved ~0.98 V reading. Inspect module pinout and loading if observed.
4. **NMEA2000 network:** Confirm J5 pinout (CAN H, CAN L, NET_C, NET_S) against actual cable colors and the network's terminators. R37/J9 is optional on-board termination; leave out for a normal drop to an already terminated backbone. Avoid unintended NET_C-to-main-GND bonds. Never close both positions of a power-selection jumper.
5. **Power path:** Check external supply input ranges and transients, TVS D2, reverse-polarity MOSFET Q1, LM5164 buck values and switch-node layout versus datasheets. Validate 5 V / 3.3 V startup and USB/external power source transitions under load.
6. **USB-C:** CC1/CC2 each have 5.1 kΩ Rd pulldowns and D+/D− are connected through USBLC6-2SC6. Confirm ESD placement and that no external power path back-feeds USB VBUS.
7. **Isolation / PCB layout:** Confirm creepage, clearance, isolated-domain separation, ground planes and surge paths in the actual PCB. A schematic cannot establish isolation performance or marine EMC compliance.
8. **microSD / LED:** SPI pull-ups, SD detect, AHCT level conversion and SK6805 decoupling look conceptually consistent. Confirm slot pinout / insertion logic and 5 V LED supply during power switching.

## Scope and confidence

This is a design-review checklist, **not** proof of wiring correctness or an electrical safety certification. It does not replace the KiCad source files, ERC/DRC, the PCB design, component datasheets, or measurements on hardware. No firmware was modified for this review.
