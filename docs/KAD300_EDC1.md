# Volvo Penta KAD300 / EDC I: protocol, scaling and test procedure

This page summarises the findings from the owner's own VODIA (PentaEDC1) analysis, for a Fairline Targa 34 (2005) with 2 × Volvo Penta **KAD300/DP, EDC I**. It describes how EngineLink 0.4 uses them.

The analysis files themselves are not stored in this repository. That includes the disassembly addresses, the database export and the decoded ECU firmware. Neither are Volvo's credentials, keys or decoding tables. Volvo's PC tool and ECU firmware are copyrighted, and EngineLink does not need them. Only the protocol facts and scalings below are used.

## First wire capture (port engine on J6, 2026-10-27)

EngineLink 0.4.0 was connected to the port KAD300 on J6. It polled for about 6 minutes, with the engine cold-started, at idle and briefly revved to ~1100 rpm. The capture contained 1,431 requests, **1,391 answers (97 %)** and **0 checksum errors**.

| Finding | Evidence | Status |
|---|---|---|
| Request `01 43 21 <hi> <lo> <cs>` works | ECU answers ~immediately; request echo and answer arrive in one burst | **CONFIRMED** |
| Answer format `83 C1 <n>1 <value> <cs>` | 1,353 × `83 C1 21` + 2 bytes; 38 × `83 C1 31` + 3 bytes (index 266). High nibble = value width | **CONFIRMED** |
| Byte order big-endian | RPM `09 A8` = 2472 → 618 rpm. Little-endian would give 10,754 rpm | **CONFIRMED** |
| 256 engine speed × 0.25 | Idle 600–618 rpm. Index 291 (speed reference) reads exactly 600 rpm at idle. The analog tachometer showed 565 (about 7 % low) | **CONFIRMED** |
| 266 engine hours × 0.0027778 | Counter rose 0.0917 h over 330 s of running (= 10 s per count). Total 772.1 h | **CONFIRMED** |
| 259 coolant × 0.25 °C | Rose steadily from 19 to 35 °C after a cold start | **CONFIRMED** (plausible and consistent) |
| 260 / 261 fuel / boost temperature × 0.25 °C | 12–14 °C / 13–17 °C | Plausible |
| 258 / 265 pressure × 0.03125 kPa | 258 − 265 ≈ 102 kPa = atmospheric throughout, so the abs/rel pair is consistent. About 45 kPa relative at idle | Consistent, meaning to confirm |
| 264 gear bits | `0x4965` → bits 0x18 = 0 → neutral | Plausible (only neutral seen) |
| 293 voltage | 1033 raw → **13.51 V** (VODIA) or 12.91 V (Yacht Devices), with the engine running | Needs a multimeter reading |
| 289 fuel rate | About 407 raw at idle → **3.2 L/h** (× 1/128) or 1.6 L/h (× 1/256) | Tank to tank |
| 288 relative load / 295 rod position | 87–91 % / 52–55 % at idle | Meaning unclear; not used |

## Evidence levels

| Level | Meaning |
|---|---|
| **CONFIRMED** | From several independent sources, or verified (e.g. checksums) |
| **RECONSTRUCTED** | Derived from static analysis of the PC tool; not measured on the bus |
| **HYPOTHESIS** | Reasonable, but needs a measurement on the engine |

## 1. Protocol ("NIRA EDC1"), reconstructed and now confirmed on the wire (see above)

- The physical layer is J1708-style: 9600 bit/s 8N1, differential A/B.
- **KAD is request/response.** The ECU does not broadcast by itself. A quiet bus is normal unless a Volvo EDC display (or EngineLink) asks.
- Read one live parameter by its VODIA **DataINDEX**:

  ```
  01 43 21 <indexHi> <indexLo> <checksum>
  ```

  The index is big-endian. Example for fuel rate, index 289 = 0x0121: `01 43 21 01 21 79`.
- **Checksum:** J1708 rule. All bytes including the checksum sum to 0 mod 256.
- **Reply:** `83 C1 <n>1 <value…> <checksum>`. The high nibble of the third byte is the value width (`21` = 2 bytes, `31` = 3 bytes). The value is big-endian. This was confirmed on a KAD300 in 2026-10, and EngineLink uses the header width when present.
- Other commands exist (`01 42 …`: info, calibration, memory read/write, timer). **EngineLink can only build `01 43 21`. It has no code path for any other command.**

## 2. Parameter scaling, CONFIRMED (VODIA ParameterList, program type M_902)

`physical = raw × factor` (offset 0 for live parameters).

| Index | Parameter | Type / bytes | Factor | Unit |
|---:|---|:-:|---:|---|
| 256 | Engine speed | U / 2 | 0.25 | rpm |
| 258 | Air inlet pressure | U / 2 | 0.03125 | kPa (abs) |
| 259 | Coolant temperature | S / 2 | 0.25 | °C |
| 260 | Fuel temperature | S / 2 | 0.25 | °C |
| 261 | Boost temperature | S / 2 | 0.25 | °C |
| 263 | Panel buttons / start input | bits / 2 | n/a | n/a |
| 264 | Gear (bits 0x18: 0 N, 8 F, 16 R), relays, indicators | bits / 2 | n/a | n/a |
| 265 | Boost pressure | S / 2 | 0.03125 | kPa (rel) |
| 266 | Total engine hours | U / 3 | 0.0027778 | h |
| 288 | Relative load | U / 2 | 0.012207 | % |
| **289** | **Fuel rate** | U / 2 | **0.0078125 = 1/128** | L/h |
| 291 | Engine speed reference | U / 2 | 0.25 | rpm |
| 293 | System voltage | U / 2 | 0.01308 (see conflict) | V |
| 295 | Rod position | S / 2 | 0.0061035 | % |
| 296 | Injection angle (alpha) | S / 2 | 0.015625 | ° |

**KAD300 = program type M_902 → fuel rate index 289 × 1/128.** This was confirmed three independent ways: the VODIA database (KA(M)D 300 → Marine + hardware 873902), the program-type logic in the PC tool (fallback "default to M_902"), and a byte-for-byte firmware family comparison. The only alternative in the list is × 1/256, exactly half.

Notes:
- Index 262 is a different fuel-rate field for another program group. It is **not** used on KAD300.
- Gear on 264 may not distinguish forward and reverse. Test N/F/R at the dock and watch the raw bits.
- Index 291 is "engine speed reference" in VODIA. Yacht Devices may map it to throttle.

## 3. Voltage conflict, open question

| Source | Index 293 factor |
|---|---|
| VODIA ParameterList | 0.01308 V |
| Yacht Devices (KAD gateway) | 0.0125 V |

The two differ by about 4.6 %. **Measure it**: ignition on, engine off, compare against a multimeter on the battery. EngineLink's voltage check (Data page) compares all four combinations of byte order × factor and saves the best match.

## 4. Fuel factor and calibration, HYPOTHESIS until a tank-to-tank

- Choosing between × 1/128 and × 1/256 is an exact factor of 2. One normal trip settles it; **no full throttle is needed**.
- The ECU *calculates* fuel from rod position and RPM; it does not measure it. A per-engine correction (`refilled ÷ integrated`) covers wear such as a replaced or worn turbo.
- EngineLink stores the fuel totals as the **raw count integrated over time**. Changing the scale or a correction later recalculates every litre already recorded.

## How EngineLink 0.4 implements this

| Topic | Behaviour |
|---|---|
| Protocol setting | Settings → Engine data decoding → *Volvo Penta KAD / EDC I* (default) or *SAE J1587*. In KAD mode J1587 decoding is off, so no phantom values appear. |
| Listening | Requests from any device are recognised (`01 43 21 …`, checksum valid). The next valid frame within 300 ms is paired as the answer. If a Volvo EDC display polls, EngineLink decodes its answers without sending anything. |
| Value position | The parameter's width in bytes, immediately before the checksum. Both byte orders are kept; the chosen one (default big-endian) is used. |
| Echo and merged frames | A request echo followed by the answer in one burst is split. Our own echoed requests are marked `ECHO`, sent requests `TX`. |
| Frame gap | 2 idle symbols (~2 ms) in KAD mode, 1 in J1587 mode. |
| **Polling** | Off at every boot. Switched on per connector on the Data page, with a typed confirmation. Attaches the TX pin only then; releases it when stopped. |
| Polling limits | About 4.5 requests/s per connector: RPM and fuel every 3rd slot each, the rest round-robin. Waits for 20 ms of bus silence. One request at a time, 300 ms answer window. |
| Automatic stop | After 40 unanswered requests in a row, if another device starts polling, on a protocol change, firmware update or restart. |
| Logging | The session CSV and the motor-test CSV get `direction` (RX/REQ/TX/ECHO), `kad_index` and `kad_value` columns. Raw HEX is always kept. |
| Totals | Trip and lifetime per physical connector, from the raw integral × scale × correction. Uncorrected ×1/128 and ×1/256 trip candidates are shown side by side. |

## Test procedure on the boat

Do these in order. Stop at any step that does not look right, and keep the CSV.

1. **Bench first.** Both isolated 5 V rails measured against *their own* isolated ground (4.75–5.25 V). SJ1/SJ2 open.
2. **Connect, listen only.** Ignition on, engine off. Open **Data**. If a Volvo EDC display is on the bus you will see `REQUEST`/`ANSWER` pairs straight away and the card says *Display is polling*. Then nothing needs to be sent at all.
3. **Close the TX jumper for one connector only** (SJ1 for J7, SJ2 for J6). Make sure VODIA/Vocom is **not** connected.
4. **Start polling** that connector (Data → Engine connection → *Start polling*, type `POLL`). Within seconds the answer counter should rise.
5. **Voltage check** (Data → Verify the decoding). Enter the multimeter reading and press *Compare and choose*. This confirms frame format, byte order and voltage factor together.
6. **RPM check.** Start the engine. The idle RPM shown should match the tachometer (≈ 600–750 rpm). Optionally record an *Engine Test* with a few steady RPM stages.
7. **Repeat 3–6 for the other connector.**
8. **Fuel, tank to tank.** Fill both tanks, *Reset trip*, make a normal trip with polling on, refill, and enter the litres per tank under Fuel → *Fuel calibration*. EngineLink picks 1/128 or 1/256 and saves a correction per engine.

If no combination in step 5 matches within 8 %, the reply layout differs from the reconstruction. Download the session CSV (History). It contains every request and answer byte for analysis.

## Still to be decided on the engine

- The reply layout on the wire: header length, value position and byte order (step 5).
- The fuel factor, 1/128 or 1/256 (step 8).
- The voltage factor, 0.01308 or 0.0125 (step 5).
- Whether index 264 distinguishes forward from reverse.
