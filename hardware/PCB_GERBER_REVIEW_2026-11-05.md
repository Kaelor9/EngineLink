# EngineLink — PCB Gerber and schematic audit (2026-11-05)

**Inputs:** `EngineLink_schematic_Diagram.pdf` (one-page KiCad schematic print) and uploaded ZIP `ESP32-S3-WROOM-1-N8R8.kicad_pcb(1).zip`. The ZIP contains manufacturing exports dated **2026-10-12**: four copper-layer Gerbers, soldermask/paste/silkscreen/edge cuts, plated and non-plated drill files, IPC-356 netlist, PCBWay BOM, and position data. **It does not contain the editable `.kicad_pcb`, `.kicad_sch`, project or stackup**, so KiCad ERC/DRC and native connectivity/revision comparisons were not run.

Review method: visually inspect high-resolution vector schematic and Gerber layers, inspect BOM and positions, inspect IPC-356 pad/net assignments, measure copper-pour polygon separation geometrically, compare relevant manufacturer specifications. Geometry results below are **not a complete copper-to-copper DRC**.

## Executive outcome

**Do not claim fully validated PCB electrical correctness, 3 kV board isolation, or final RF suitability.** No unambiguous schematic short/miswired MCU, MAX3444E, ISO1042, USB data pair, or RECOM selector/enable connection was identified in the accessible evidence. Several actionable design issues and tests remain.

## P1 — Isolation-zone copper spacing: measured ~0.60–0.65 mm

Gerber copper-fill polygon distance calculation (not whole-board minimum):

| Layer | Copper domains | Minimum fill-to-fill distance |
|---|---|---:|
| In1.Cu | GND ↔ GND_ISO1 | 0.6003 mm |
| In1.Cu | GND ↔ GND_ISO2 | 0.6003 mm |
| In1.Cu | GND ↔ N2K_NET_C | 0.6003 mm |
| In1.Cu | GND_ISO1 ↔ GND_ISO2 | 0.6000 mm |
| In1.Cu | GND_ISO2 ↔ N2K_NET_C | 0.6500 mm |
| B.Cu | GND ↔ GND_ISO1 / GND_ISO2 / N2K_NET_C | 0.6003 mm |

This is **not a short** and does **not alone establish a violation of a particular insulation standard**: internal copper-to-copper distances, package terminal clearance, PCB surface creepage, dielectric thickness, working voltage, environment and required insulation classification all matter. However, do not infer full-board kilovolt-rated separation from the components' kV test numbers. Check actual PCB stackup and surface/copper gaps against the intended isolation goal (maritime environment) and external transients. If high withstand/creepage is required, enlarge isolation gaps or adjust ground pours in a future revision. Analog Devices ADuM1201 family specifies several-millimeter package external clearance/creepage dimensions (exact grade/package applies).

## P1 — ESP32-S3 module antenna clearance

The module is installed rotated near the left board edge. From the Gerber edge and U1 footprint position, the board starts at about x=3.8 mm adjacent to the module antenna end, and copper pours on more than one layer reach this edge. The ESP32-S3-WROOM-1 antenna spans approximately the first 6 mm of the module; **there appears to be overlap between base-board copper and part of the antenna footprint**. Confirm in KiCad using the official footprint's antenna keepout and the actual board cutout, taking both sides and **all four copper layers** into account. Check metal enclosure and cable proximity. Espressif prefers the entire antenna to overhang the base board or clear underneath, plus recommended surrounding open space, and requires throughput/range testing. This is plausible RF-performance risk, **not proof** of the earlier erratic Wi-Fi.

Manufacturer guidance: https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html

## P1 — 5 V rail setpoint, thermal margin, power budget

**LM5164 feedback divider:** R4=340 kΩ, R6=101 kΩ; TI VFB reference 1.2 V, hence nominal `Vout≈1.2*(1+340/101)=5.24 V` under regulation. The rail is labeled 5 V. This remains inside the RECOM R05C1TF05S 4.5–5.5 V input range but differs from USB-diode-fed rail (~USB VBUS minus two diode/ferrite drops), increasing LDO dissipation. Test output under 12/24 V source, radio bursts, warm conditions and both-power switching. Consider divider values for 5.0 V if desired.

**Regulator capacities:** LM5164 is specified as 1 A max output. Three R05C1TF05S-R isolated modules can output 5 V × 200 mA each, and each draws ~395 mA at 5 V input at full load per manufacturer, so all three maximum loaded simultaneously could exceed the 1 A total 5 V output budget without accounting for 3.3 V LDO, LED and peripherals. This is a **worst-case rating exercise, not an actual measured overload**. Record actual maximum board load including TX, CAN and LED.

**3.3 V LDO:** TLV76733 (1 A) must dissipate `(V_IN - 3.3 V)*I_3V3`, ~0.78 W at 400 mA if VIN=5.24 V, ~1.16 W at 600 mA. Thermal resistance depends on PCB heat spreading. Check its exposed-pad grounding, temperature and regulator transients using an oscilloscope.

**USB path:** F2 specified 0.5 A PTC (MF-NSMF050/16X-2), plus FB2 and D10, followed by D3 into the shared 5 V rail. Check whether USB-only operating current (Wi-Fi peaks and isolated domains) comfortably stays below derated PTC hold and the 3.3 V LDO input margin, and verify USB/external supply back-feed behavior.

Manufacturer references: https://www.ti.com/lit/ds/symlink/lm5164.pdf ; https://recom-power.com/pdf/Econoline/RxxC1TFxxS.pdf ; https://www.ti.com/product/TLV767-Q1/part-details/TLV76733QWDRBRQ1

## P2 — Component, connector and circuit audit

- **J3 BOM text discrepancy:** PCBWay BOM value `Conn_01x13_Socket`, actual footprint `PinHeader_1x08_P2.54mm_Vertical`, and the IPC netlist identifies **8** J3 contacts. Manufacturing must use an 8-pin mating part, not infer 13 from the value; correct BOM value/description for the next release. Otherwise BOM and positions match: 141 designators in each; 113 top/28 bottom positions, 133 SMT and 8 THT entries.
- **MAX3444E U5/U8 with ADuM1201 U6/U7:** Data directions and logic match the basic intended J1708 transmit/receive path; MAX3444E DE is active-low, R22/R32 pull the respective DE high (TX disabled) and SJ1/SJ2 can pull it low for testing. /RE held low. Confirm jumper positions/polarity, isolated power values and the timing/protocol before enabling live engine-bus TX. Inspect protected bus A/B labeling and cable polarity.
- **RECOM PS1/PS2/PS3:** CTRL pin 6 tied VIN+; VSEL pin 7 tied VOUT+ to select 5 V. Both match the **R05C1TF05S** datasheet, including official direct-VIN CTRL enable recommendation; do not flag these as errors.
- **NMEA2000:** ISO1042 supply and logic sides are split, FL1 common mode filter and D16 CAN TVS included; J9 allows optional 120 Ω R37 between CAN H/L. For a normal drop on a backbone already terminated at both ends, **leave J9 termination open**. Confirm J5 CAN H, CAN L, NET_C and NET_S lead colors with the actual NMEA/SeaTalk NG cable before connecting. `SJ_GND` in IPC shows adjacent pads for **EXT_GND, main GND and N2K_NET_C** — verify how/if this solder jumper is populated: tying N2K_NET_C to main GND defeats the intended CAN galvanic isolation.
- **USB-C:** Two 5.1 kΩ Rd CC resistors, USBLC6-2SC6 on the D+/D− pair, plus USB polarity/feed arrangement. No obvious schematic reversal found. Check actual 90 Ω differential impedance, routing and ESD placement in KiCad/stackup.
- **GPIO0 / GPIO3:** GPIO0 has a BOOT switch to GND and the ESP32-S3 internal pull-up; missing external pull-up is not by itself a functional error. R1 10 kΩ is actually on GPIO3, a JTAG-source strapping pin; this pull-up defaults it high and is generally compatible with built-in USB JTAG. Keep this distinction in hardware docs; do not call R1 a GPIO0 pull-up.
- **microSD:** 3.3 V SPI and card detection wiring/decoupling appear coherent, but socket physical pin orientation and hot-insertion behavior require board test.
- **Reverse protection and transients:** Q1 PMV100EPA is a 60 V P-MOSFET, D1 is a gate clamp, and D2 is SMCJ33CA TVS. Validate absolute and pulse limits against the actual boat's surge exposure, especially TVS peak clamp versus MOSFET voltage rating and F1's 40 V PPTC maximum. This is not a marine EMC certification.

## Pre-boat bench checks (do not use live engine transmission yet)

1. Check power off for unexpected continuity between GND, GND_ISO1, GND_ISO2 and N2K_NET_C; avoid an installed short across `SJ_GND`.
2. Apply USB alone: measure +5 V, +3.3 V, +5V_ISO1, +5V_ISO2 and +5V_N2K_ISO relative to their **own** ground reference; measure input current and thermal rise under sustained Wi-Fi traffic.
3. Apply externally current-limited 12 V and 24 V separately: measure 5 V buck rail (~5.24 V target), 3.3 V regulator, rail ripple/load-step droop and regulator temperatures. Oscilloscope preferred for short spikes.
4. Test USB + external supply connected and switch between sources; confirm no unwanted back-feed into USB connector, rail interruptions or resets.
5. Test Wi-Fi SoftAP and router using a fixed phone placement/orientation (RSSI, association, sustained transfer, reconnect), and repeat with enclosure/cabling to quantify antenna effects. Compare USB and external power.
6. Confirm both J1708 isolated 5 V rails at load, verify DE disable states with SJ1/SJ2 open and do controlled isolated loopback before a real engine bus.
7. Confirm NMEA J5 pinout, shield/common handling and termination (backbone typically two endpoints); receive-only initial boat test.
8. Check manufacturing assembly against J3 8-pin footprint, as well as solder bridges, exposed pad, mounting and moisture protection.

## Limitations

- No editable KiCad board/project in ZIP → no native ERC/DRC, board stackup, track impedance calculation, or footprint/3D source comparison.
- Geometric 0.60 mm measurements use **zone polygons** only, not all tracks/pads/holes and not a definitive all-copper isolation distance.
- A board's function in one hardware test does not validate all worst-case conditions, EMC or certified insulation rating.
- Source Gerbers and BOM are dated 2026-10-12; do not assume they are the newest hardware revision.
- **No firmware or board design was modified during this audit.**
