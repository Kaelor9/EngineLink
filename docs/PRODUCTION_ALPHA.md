# EngineLink application v0.8.1

This is a **working application foundation**, not a validated production release. The former Lab firmware is retained under \`experiments/\`. The application is in \`firmware/EngineLink/\`.

## Changes in 0.8.1: boat router

- **The boat router connection comes back by itself, quickly.** After a drop EngineLink tried again after 15 s, then waited longer each time, up to 5 minutes, so one drop could leave it off the router for a long time. Now it tries after 8 s, 16 s and then every 30 s (on the router's own channel, which does not disturb the phones).
- **A router that changed channel is found again.** Routers move channels by themselves; EngineLink only searched for the router again when no phone was on the EngineLink Wi-Fi, so with a phone joined it never found it. It now searches after a few failed tries, at most every 5 minutes while phones are joined.
- **Keep-alive to the router** every 25 seconds, for routers that drop quiet clients.
- **Why it dropped**: Diagnostics → Wi-Fi shows the last router drop in plain words (for example "the router was not heard for a while") and how long it has been off.
- The status light shows "boat router not reached" only after 15 seconds without it, not for every short drop.
- The Range card says why there are no nautical miles: they need the boat speed from GPS (Settings › Speed source).

## Changes in 0.8.0: robustness

- **A power cut loses at most 30 seconds.** While an engine runs, the fuel totals and the trip are saved every 30 seconds (they were saved every 5 minutes, so switching off at the key could lose several litres). The second-by-second trip log is written to the card every 10 seconds.
- **The SD card comes back by itself.** After a write error with the card still in, EngineLink mounts it again (every minute, later every 5 minutes) and carries on with the raw log and the running trip's log.
- **A watchdog for the whole board.** If the main program ever hangs for a minute, the chip restarts by itself (the engine reading has had its own watchdog since 0.7.9). Long downloads and wiping the card are not mistaken for a hang.
- **Restart log** (Diagnostics → Restarts): the last 8 starts with the reason in plain words (power on, update, brownout, crash, watchdog), when, how long EngineLink had been on before, and the version. A brownout comes with advice on the power wiring.
- **App errors are caught.** The app keeps its own errors for Diagnostics and the technical file, shows a short notice for real ones, and ignores the network hiccups that are normal on a boat.

## Changes in 0.7.9: engine watchdog

- **A watchdog for engine reading.** If the engine task ever hangs for 10 seconds (other than waiting its turn), EngineLink saves its totals and starts over instead of going on without data; automatic reading starts again by itself.
- **Diagnostics → Reading speed → Engine reading** shows that reading runs in its own task, and the longest it ever had to wait for the rest of EngineLink, so the split can be checked on the boat.

## Changes in 0.7.8: engine task and data read

- **Reading the engines has its own task.** It runs on its own processor core and is no longer held up by the app, Wi-Fi or a download. The rest of EngineLink lets it in whenever it waits for the network, which is exactly when reading used to stop.
- **Data read, per engine.** Every second an engine runs counts as read when a fresh answer is there. The trip report and the trip timeline show the share for each engine, the number of gaps over 3 seconds and the longest gap; Diagnostics → Reading speed shows it since the last start. A gap only counts when the engine ran on both sides of it, so starting and stopping never count as missing.

## Changes in 0.7.7: steady engine reading

- **Starboard (or port) no longer stops being read for no reason.** When EngineLink was busy for a moment (a download, a report, a page), it judged its request unanswered before it had looked at the answer that was already waiting, and sent the next one. The echo of the earlier request then looked like a request from another device, so reading stopped with "Another device already polls this engine", and a late answer could be stored under the wrong value. Now EngineLink reads what has arrived before it decides, always recognises its own requests, only accepts an answer after its own request was seen on the cable, and only steps back for another device that keeps asking (three requests within 10 seconds).
- Run "Discover more values" again: the earlier list (every number answered) came from this mix-up.

## Changes in 0.7.6: connection

- **Downloads are compressed.** When the browser accepts it (all current ones do), CSV files and raw data are sent deflate-compressed with the compressor built into the chip. CSV shrinks 5–8 times, so a weak link carries that much more and a download takes that much less time. The file you get is the normal CSV. Diagnostics → Wi-Fi → Last download shows how much it was compressed.
- **The engines keep being read during a download.** A download used to stop everything else on EngineLink until it was done, so reading the engines, the trip and the diagnosis paused (and a long one could cut an engine run in two). Between pieces of a download EngineLink now handles the engine messages, sends its read requests, keeps the trip and the diagnosis going and updates the status light.
- **Every download goes out in large pieces**, also the raw log, engine runs and motor test files.
- **20 MHz channels.** The EngineLink Wi-Fi and the connection to the boat router use 20 MHz channels instead of 40 MHz. 40 MHz on 2.4 GHz overlaps the neighbouring networks and is the first thing to fail where it is crowded or the signal is weak.
- **The quietest channel without a router.** Without a boat router EngineLink now listens on channels 1, 6 and 11 at start-up and takes the one with the least other Wi-Fi nearby (a marina can have dozens of networks). With a router it keeps using the router's channel, as it must.
- **The app asks less often on a slow link.** It times EngineLink's answers and spaces its live updates to match (every 0.5 s on a good link, up to every 2 s on a weak one), so requests no longer queue up behind each other.

## Changes in 0.7.5: download speed test

- **Find out what makes a download slow.** Diagnostics → Wi-Fi has a new **Test download speed** button: EngineLink sends 512 kB from memory, without the SD card, so the result is the Wi-Fi alone, and it says whether that went over the EngineLink Wi-Fi or the boat router, with the signal. Below it, **Last download** splits the last raw-data export into finding the trip, reading the SD card and sending, and says which took the time.

## Changes in 0.7.4: fast raw download

- **Raw engine data downloads quickly.** The raw export sent every row as three tiny pieces, each its own network packet, so it ran at about 1 kB a second and browsers gave up on a long trip ("Download failed"). It now reads the SD card in 4 kB blocks and sends 8 kB at a time, a few hundred times fewer packets. While an export runs EngineLink does nothing else, so a fast export also means a shorter pause in reading the engines.

## Changes in 0.7.3: protocol safety

- **No more silent stop when the protocol is wrong.** With the engine protocol set to SAE J1587, EngineLink only listens and never asks the Volvo Penta KAD / EDC I control units, so nothing comes in, even with "Read engines automatically" on. One tap on "Engine protocol" in Diagnostics → Advanced switched it, without asking. Now switching to J1587 asks first and says what it does, and while J1587 is set and no engine is seen, every screen shows "Not reading the engines" with a button that switches back. Automatic reading starts again right after switching.

## Changes in 0.7.2: iPhone menu

- **The menu on iPhone really clears the home bar now.** 0.7.1 only made room when it detected the iOS sizing fault, but on newer iOS the home-screen app also reports no room at the bottom without that fault, so the tabs still sat on the home bar and in the rounded corners. The menu now always keeps Apple's own room for the home bar in the home-screen app: 34 pt on iPhones without a home button (21 pt sideways) and 20 pt on iPads, plus a little room at the sides.

## Changes in 0.7.1: reports, export and the iPhone menu

- **iPhone home-screen app: the menu is clear of the home bar.** When EngineLink stretches the app to the bottom of the screen (the iOS sizing fault fixed in 0.5.4), iOS reports no room for the home bar, so the menu labels sat under it. The menu now keeps the 34 pt the home bar needs on iPhones without a home button (20 pt on iPads).
- **Export, done properly.** One Export sheet for everything: a clear **trip report** or **engine report** at the top, and the spreadsheet files (trip summary, second by second, raw engine messages) below, with their sizes. The report is a light document with the key figures, curves for rpm, fuel, coolant and boost, what EngineLink noticed, port against starboard and each engine's life story. "Save as PDF or print" makes a PDF you can keep or send to a mechanic. It opens in its own window, so the app stays where you were. Open it from History (a trip → Export, or select several trips), the trip timeline, or Diagnostics → Engine report and files.
- Charts no longer blow up the noise at a steady idle: each value has a smallest scale (300 rpm, 6 L/h, 10 °C, 0.2 bar, 1 V).
- **Exporting no longer traps the iPhone app.** A home-screen app on iPhone shows a download full screen with no way back, so you had to close the app. Now the file opens in a Safari window: save it with the share button (Save to Files) or send it, and tap Done to come back where you were. The diagnostics report, which is made in the app, is shown in a sheet with a Copy button. Other phones and browsers download as before.

## Changes in 0.7.0: diagnostics

- **EngineLink watches the engines for you.** Every second it checks both engines against fixed limits and against what is normal for each engine at that rpm. It raises an event for: coolant too hot (warning above 95 °C, alarm above 102 °C), charge air too hot, battery too low or too high while running, overspeed, an engine that stops from cruise or stops answering, rpm that does not reach what the lever asks, rpm that hunts while the lever is still, coolant, boost, charge air or fuel slowly drifting away from this engine's normal (CUSUM, so a small but steady change is caught without false alarms from noise), one engine using more fuel or making less boost than the other at the same rpm, full throttle on a cold engine, an engine that stays cold (thermostat), long idling, and status bits that change. A normal trip raises nothing: this is checked with simulated trips in `tools/test_diagrules.cpp`.
- **Events in the app** (Settings → Events, and a card at the top when something needs attention): what happened, what it usually means and what to check, with the phone vibrating for a new warning.
- **Black box.** Every event saves the two minutes before and after, second by second, on the SD card. Open an event to see rpm, lever, fuel, coolant, boost, charge air and battery of both engines around it.
- **"Something's odd" button** (Engines page and Events): heard or felt something? Tap it and the moment is saved with its black box, so you can look at it later.
- **Trip timeline.** Each trip is recorded once a second on the SD card. History → a trip → Timeline shows the curves, and "Download as spreadsheet" gives one row per second with every value of both engines (opens in Excel or Numbers).
- **Last 10 minutes** on the Engines page, as curves.
- **Port and starboard** compares the two engines at the same rpm, from what both learned over the trips: fuel, boost, coolant and charge air. A big fuel or boost difference is shown in orange.
- **Life story** per engine: hours at idle, part load, cruise and full throttle, starts, cold starts, full throttle while cold, time above 95 °C, long idles, the lowest battery voltage at a start, and the best and latest full-throttle rpm (a falling full-throttle rpm points at the hull, the propeller or the engine).
- **Faster reading.** EngineLink asks as fast as the engines answer (down to every 80 ms, it was every 220 ms) and slows down by itself when answers go missing. Values an engine never answers are skipped, so they no longer cost time. Diagnostics → Reading speed shows requests per second and the share answered.
- **Discover more values** (Diagnostics → Reading speed): asks once for every KAD value number from 256 to 319 and lists the ones the engine answers. It uses the same read request as always: nothing is changed in the engine, and no fault codes are read or cleared.
- **Range with ±**: the range now carries the same uncertainty as the fuel aboard.
- **A status light you can read.** New colours and rhythms that cannot be mixed up:

| Light | Meaning | What to do |
|---|---|---|
| White, fast blink | Updating firmware | Keep the power on |
| Red, fast blink | Engine alarm (for example coolant too hot) | Look at Events in the app now |
| Long red, then short blue | SD card error: nothing is saved | Take out the card and put it back, or replace it |
| Three short red blinks, pause | Engine data fault (J1708 garbled or overflowing) | Check the engine connectors and cables |
| Two short orange blinks, pause | Engine warning (a value outside what is normal for that engine) | Look at Events in the app |
| Short blue, then long orange | No SD card | Everything works, but trips and raw data are not saved |
| Magenta, slow blink (1 s on, 1 s off) | Recording a motor test | – |
| Blue, breathing | Setup not finished | Join the EngineLink Wi-Fi and open the app |
| Long green, then short blue | Boat router not reached | EngineLink's own Wi-Fi still works |
| Green, dark for a moment every 2 s | Reading the engines, all is well | – |
| Green, steady | Ready, engines off or not being read | – |
| White, steady | Starting | – |
| Three quick magenta flashes | "Something's odd" mark was saved | – |
- Trips from before 0.7.0 have no timeline. Settings, trips, the fuel log and fuel totals are kept.

## Changes in 0.6.0: measurements

- **Calibration from fill-ups that knows how exact each one is.** When you log a fill-up you choose "Nozzle clicked off", "Topped up to the filler pipe" or "Not full". Every pair of full tanks becomes one equation, weighted by how exact both fill-ups were. Partial fill-ups in between are added to the pump litres. The calibration is solved again from the whole log after every change, so correcting or deleting an entry also undoes its effect.
- **Correction per engine-speed band.** Fuel is now counted separately at idle (under 1,100 rpm), part load and cruise (2,500 rpm and up). With fill-ups from different kinds of trips EngineLink learns whether the engines are more off at idle than at cruise; with few fill-ups it uses one common factor. Fuel is stored as raw counts, so every trip and the tank level are recalculated whenever the calibration improves.
- **Honest uncertainty.** The app shows how exact the fuel figures are ("within about ±4 %"), and fuel aboard carries its own ± that grows with litres counted and shrinks with good fill-ups.
- **Odd fill-ups are caught.** A fill-up that disagrees with all the others by more than 3× the expected spread is set aside, and the fuel log asks whether the tank was really full. You decide whether it counts.
- **Engine answers are checked.** Each value has physical limits and a largest believable change between two answers. A sudden jump is held back until the next answer confirms it; impossible values are dropped. This way a garbled reply never reaches the fuel totals.
- **Short gaps are bridged.** If fuel answers are lost for up to 2 minutes while the engine keeps answering for rpm, the fuel in between is interpolated and counted, and Diagnostics shows how much.
- **Adaptive reading.** While the throttle moves, fuel is asked for more often (every second request), so the litres follow fast changes closely.
- **Learn the gear** (Diagnostics → Calibration): shift N, F, N, R, N at the dock while EngineLink watches the gear value; it finds which bits mean forward and reverse. Until then the gear shows "Not learned" instead of guessing.
- **"Load" is no longer a headline value.** KAD index 288 reads 80–90 % at idle, so its meaning is not confirmed; it is off by default and labelled as unconfirmed, as is "Fuel rack".
- **Raw engine data gets its own download button** when exporting trips, because phones allow only one download per tap.
- Existing trips, fill-ups and totals are converted. Fill-ups from before 0.6.0 count as "nozzle clicked off" until you change them in the fuel log.

## Changes in 0.5.5: network watchdog

- **The network watchdog no longer drops phones.** The ESP32 also reports "network stopped" (late) when the network only changed a setting such as its channel. The watchdog took that as a failure and restarted the network, and every restart also set the country again, which on its own makes the network start over and drops every phone. Now the watchdog only acts when the network is really gone (stopped and not started again for 3 seconds, or switched off), and the country is only set when it is not already right.
- **Address name in Settings.** EngineLink answers at http://enginelink.local (mDNS) on its own network and on the boat router, and the router also sees it under that name. The name can be changed under Settings → EngineLink Wi-Fi → Address in the browser (letters, digits and hyphens). http://192.168.4.1 always works on the EngineLink network. EngineLink deliberately does not act as the phone's DNS server: it would answer "not found" for every other name and could cut the phone off the internet over mobile data.
- Diagnostics → Wi-Fi also shows phone drops with the reason for the last one (for example "the phone left by itself (it may have switched to another Wi-Fi)"), channel changes and the slowest pass of the main loop. Restart code 11 now reads "USB (flashing or serial monitor)".

## Changes in 0.5.4: iPhone bottom band

- **iPhone home-screen app fills the screen to the bottom.** iOS can move the page below the status bar but still size it as if it began at the top of the screen, which left an unused band the height of the status bar under the menu. The app now detects this (the page is then exactly two status bars shorter than the screen) and reaches down itself; anything iOS shows below the page takes the menu colour. This only applies to iPhone and iPad: Android phones are never changed, because there the same gap is just the status bar plus the Android navigation bar.

## Changes in 0.5.3: iPhone home-screen layout

- **iPhone home-screen app:** the status bar is no longer see-through. New iOS versions put the page below a see-through status bar anyway but sized it as if it were behind it, which left an empty band under the menu. Content now fades out under the top edge instead of being cut off.
- The "Reconnecting" notice is one short line above the menu instead of a large bubble over the title.
- "Which socket is which" asks before swapping port and starboard (one tap used to swap them).
- Firmware page says "Downloading…" while it downloads; "0.0 below average" now reads "On your average".

## Changes in 0.5.2: steady Wi-Fi

- **Web server rebuilt for a steady connection.** The stock ESP32 web server serves one connection at a time, so a connection the iPhone opened ahead of time, or one that stalled during a Wi-Fi blip, held up every other request for up to 5 seconds and the app showed "Reconnecting". EngineLink now accepts all connections at once, answers whichever has a request, and keeps connections open between requests (keep-alive), so the live updates reuse one connection instead of opening a new one twice a second.
- **Connecting to the boat router no longer restarts the EngineLink network.** The router's channel is taken from the scan you picked it from (no second scan that hops channels), and the network only moves channel when the router is on another one; the app says so.
- **The app keeps working when the phone joins the boat router.** EngineLink answers through the router too, and the app finds it there (its router address or enginelink.local) by itself.
- A router that hands out 192.168.4.x addresses (the same as EngineLink) is detected; EngineLink leaves it and the app explains what to change.
- Diagnostics shows a new Wi-Fi section: phones, channel, network restarts, requests, slowest answer, updates missed on this phone, and the reason for the last restart (for example low voltage).

## Changes in 0.5.1: iPhone home-screen app fit

- The app now fills the whole screen when opened from the iPhone home screen. With the see-through status bar iOS measured the page too short and left a gap at the bottom.
- Input fields use 16 px text, so iPhone no longer zooms in when you tap one and stays zoomed.
- Content keeps clear of the notch in landscape, and the page no longer bounces as a whole.

## Changes in 0.5.0: the new phone app

- New app with Trip, Economy, Tank, History and Engines pages, light/dark/night themes, metric or imperial units and your own currency. Add it to the home screen as "Engine Link".
- Trips are recorded automatically and survive restarts. Add notes (people, sea, wind), combine or split trips, export CSV.
- Fuel log: log fill-ups, see fuel aboard and range. A full fill-up fine-tunes the fuel factor by itself.
- Economy: best cruising speed, a trip planner, season statistics per RPM band and an engine health trend.
- Much steadier Wi-Fi: power saving off, full transmit power, the EngineLink network stays on one channel and never restarts because of a router, sleeping phones are kept for 10 minutes, and a watchdog restarts the access point if it ever stops.
- Firmware updates straight from GitHub when EngineLink is on a router with internet. A new version that does not run properly for its first minute is rolled back automatically.
- Diagnostics: setup check, 30-second connection test per engine, calibration and raw values.
- "Read engines automatically" is off by default and must be switched on with a confirmation.
- `/api/cfg` refuses to switch on automatic reading without `confirm=POLL`; a restored backup never switches it on.
- Data that grows (trips, fuel log, health) moved from NVS to LittleFS. 0.4.x totals are migrated.

Not yet verified on the boat: Wi-Fi behaviour over a whole trip, the GitHub update path, gear Forward/Reverse bits, the starboard engine, the fuel scale and the voltage factor.

## Changes in 0.4.0-alpha: Volvo Penta KAD300 / EDC I

Based on the owner's VODIA analysis; details and evidence levels in [KAD300_EDC1.md](KAD300_EDC1.md).

- **Engine protocol setting:** *Volvo Penta KAD / EDC I* (default) or *SAE J1587*, in the setup wizard and under Settings → Engine data decoding. In KAD mode J1587 decoding is off.
- **KAD decoding:** recognises the read request `01 43 21 <index> <cs>` from any device, pairs the next valid frame within 300 ms as the answer, and reads the value (parameter width before the checksum) in both byte orders. Request echoes merged with an answer are split. KAD frames are split on 2 idle symbols.
- **VODIA M_902 scaling** for 16 parameters, including fuel rate 289 × 1/128 (1/256 selectable), RPM 256 × 0.25, voltage 293 × 0.01308 (0.0125 selectable), coolant, boost, air inlet, load, gear bits and engine hours.
- **Opt-in polling per connector** (Data page, typed confirmation). It is off after every restart and attaches the TX pin only while running. It can only send `01 43 21`, waits for 20 ms of bus silence, keeps one request in flight, and sends about 4.5 requests/s. It stops by itself after 40 unanswered requests, when another device polls, on a protocol change, OTA or restart.
- **Verification tools:** a voltage check against a multimeter (picks byte order and voltage factor), a KAD discovery table showing BE and LE values, and decoded answer details.
- **Tank-to-tank fuel calibration:** trip totals at ×1/128 and ×1/256 side by side, refilled litres per tank → scale and per-engine correction. Totals are stored as the raw integral, so a later calibration recalculates all recorded litres. 0.3 totals are migrated.
- **Logging:** session and test CSVs gain `direction` (RX/REQ/TX/ECHO), `kad_index`, `kad_value`. Sent requests are logged too.

## Changes in 0.3.0-alpha

- **Safari stability:** the stock ESP32 WebServer waits up to 5 s on a TCP connection that never sends a request, and serves nobody else in the meantime. Safari opens such speculative connections, which is a likely cause of "network connection was lost". EngineLink now drops idle sockets after 600 ms (`EngineLinkWebServer`).
- **Wi-Fi firmware update** (Settings → Firmware update) using the `enginelink-update.bin` application image published on the download page. The image is checked for the ESP32 app descriptor; the merged USB image is rejected. Files and totals are closed/saved first.
- **Persistent totals** per physical channel in NVS: trip fuel, lifetime fuel, engine hours and trip engine time. Counted only from valid ECU samples ≤5 s apart. Saved at most every 5 minutes, at the end of each engine run and before deliberate restarts, so up to 5 minutes can be lost on sudden power loss.
- **Engine-run segments:** fresh RPM ≥300 starts a run; it ends 10 s after RPM stops or goes stale. Runs are listed in History and appended to `SEGMENTS.CSV`. Runs are never merged into trips.
- **Bus discovery:** every page-0 J1587 PID seen per channel/MID with its latest raw bytes and count (`/api/pids`). The UI shows standard J1587 values labelled *unverified*.
- **Engine details** on the Fuel page (coolant, oil pressure, battery, load, boost, oil temperature, ECU engine hours and total fuel) when the ECU sends them, with standard J1587 scaling labelled *unverified*.
- **One-hour trend** of fuel flow and RPM (10 s samples).
- **mDNS:** `http://enginelink.local`. **Selectable Wi-Fi channel** (1/6/11).
- **Maintenance:** restart, re-read SD card, factory reset (typed confirmation).
- **New UI:** bottom tab bar on phones, health overview with plain-language explanations, guided engine test, decoded message details, prioritized alert bar with one-tap actions, confirmation dialogs.
- `ESP.getSketchSize()` (which verifies the whole image) is cached at boot instead of running on every status poll.
- Version defined once (`EL_VERSION`); the installer, manifest and artifacts read it automatically.

## Changes in 0.2.1-alpha

- J1708 frame backlog moved to PSRAM (2048 frames). A CSV download or SD flush blocks the web server loop; the old 72-frame queue could overflow in well under a second of bus traffic, dropping frames and triggering the red J1708 LED alarm.
- Frames up to 64 bytes are captured intact. J1708 limits messages to 21 bytes only while the vehicle moves; longer stationary/diagnostic messages were previously marked malformed.
- microSD hot-plug: card removal is detected in every state (not only while recording) and the card is unmounted; a re-inserted card mounts and resumes session logging automatically.
- A motor test that fails to start no longer leaves ordinary SD logging switched off.
- SD write failures at end of row are detected.
- Settings: a failed NMEA2000 start no longer leaves half-applied settings in memory. The Settings form is now filled correctly right after the setup wizard.
- NTP no longer overrides an active GPS time source in Automatic mode.
- The onboard UI is served with `Cache-Control: no-cache`, so Safari does not keep showing an old UI for an hour after a firmware update.
- Device page: reset reason shown as text (e.g. *Brownout*), UART line-error counter and frame backlog; the LED panel shows the real system state even while the LED demo runs.
- No status-poll SD lookups: test-file availability is cached.
- Version metadata aligned across firmware, installer, workflows and docs; RELEASING.md no longer describes the Lab build.

## What is implemented

- Native ESP32-S3 web app served **offline** at \`http://192.168.4.1\` on the EngineLink SoftAP.
- First-time wizard: direct EngineLink Wi-Fi vs. router, J7/J6 → port assignment, time-source preference. Both connection choices are presented neutrally.
- AP SSID \`EngineLink\`, initial development password \`12345678\`. A new password can be set in the wizard. **These shared default credentials are not suitable for deployment without changing them.**
- Router credentials saved using ESP32 Preferences; SoftAP remains on as a fallback. ESP32 AP+STA uses a shared 2.4 GHz radio and router connection may briefly change SoftAP channel.
- Collection from J7 GPIO21 and J6 GPIO17, 9600 8N1. **Listen-only at every boot.** J7 TX GPIO47 / J6 TX GPIO18 are attached only while the user runs KAD polling on that connector, and then only the read request `01 43 21` can be sent. There is no free-form transmit endpoint.
- UART RX timeout frame separation and 8-bit J1708 additive checksum checking. Physical framing and actual engine compatibility still need testing.
- SAE J1587 PID 190 (engine RPM, 0.25 rpm/bit), PID 183 (instantaneous fuel rate, 1/64 US gallon/hour per bit), only on valid full frames with supported page-0 PID layout. Unrecognized proprietary traffic remains raw. **Do not assume Volvo Penta ECUs send PID 183.**
- Display values as **unknown** (—) when missing or stale, not fake zeroes. Combined L/h requires fresh fuel values from both assigned channels.
- Fuel volume since current boot is integrated only over consecutive valid samples (gaps >5 seconds excluded); incomplete estimates are explicitly identified. It is **not** a permanent all-trip fuel total yet.
- microSD session CSV logging of raw J1708 bytes, physical channel, checksum result, MID, uptime and UTC if time is known. Auto-generated per-boot file IDs. Logging stops on card removal. Writes are periodically flushed; sudden power loss can lose unflushed records.
- Browser/phone UTC synchronization and optional NTP after connecting a router.
- Optional NMEA 2000 listen-only TWAI input at 250 kbit/s: PGN 126992 UTC and PGN 129026 speed over ground. **Physical CAN interface has not been validated.** Do not enable until verified. No NMEA transmission/ACK.
- Fixed LED GPIO48 patterns reflect actual status (setup, normal, Wi-Fi/router reconnect, missing SD, write fault, J1708 RX fault, passive motor test). The user color selector was intentionally removed. GPIO38, PSRAM/heap/network diagnostics, raw J1708 Packet Inspector, read-only advanced terminal with ℹ️ help.
- **Engine Test & Calibration**: passive J1708 capture with stable-RPM phase markers, optional independent reference RPM and fuel flow, SD CSV, download to phone or computer. [Test procedure](ENGINE_TEST.md).
- SoftAP stability changes: avoid AP-mode Wi-Fi.disconnect(), avoid frequent router reconnection scans with an AP client present, and serialize Safari polling to prevent overlapping status requests.
- Arduino build and embedded browser JavaScript syntax validation in GitHub Actions.

## Known gaps that must be resolved before any "production" claim

1. **Confirm both isolated 5 V power domains with a multimeter** against their respective isolated grounds. Earlier testing recorded ~0.98 V in one domain; resolution has not been confirmed.
2. Bench-validate UART framing across real J1708 traffic; J1708 protocol has inter-character and inter-message timing requirements. Receiving UART bytes does not prove full protocol conformance.
3. Verify actual Volvo Penta engine MID/PID identities and fuel-rate scaling; proprietary or extended formats must be decoded from real captures and validated independently.
4. Reliable boot-to-boot journey records, configurable grouping and preserved trip fuel accounting are not implemented. SD sessions are separate by design; no auto-merge.
5. Implement robust queue overflow/SD loss reporting under high-rate traffic; verify actual SD write performance and power-loss recovery.
6. Current advanced terminal is **read-only**. Bench-only transmit requires a separate electrical and SAE timing/arbitration safety review.
7. User/admin authorization has not been implemented. Anyone with the Wi-Fi password can currently change settings. Change the AP password from the shared factory value.
8. Test actual iPhone Safari connectivity, AP-only mode, AP+STA fallback and memory stability on the assembled PCB. An automated build cannot prove RF stability; review AP disconnect counters and reset reason under Device.
9. The optional NMEA2000 listener requires verifying ISO1042 power isolation, 250 kbit/s traffic, bus termination, and source correctness before connection.
10. A battery RTC is not fitted. The clock cannot know elapsed power-off time without a new time source; logs store uptime and boot ID when UTC is unavailable.

## Hardware connections

| Connector | Signal | Pin |
|---|---|---|
| J7 | J1708 RX | GPIO21 |
| J7 | J1708 TX | GPIO47 **not used** |
| J6 | J1708 RX | GPIO17 |
| J6 | J1708 TX | GPIO18 **not used** |
| CAN | CAN RX/TX | GPIO16/GPIO15; optional **listen-only** |
| microSD | CS/MOSI/SCK/MISO/Detect | GPIO10/11/12/13/14 |
| LED | SK6805 | GPIO48 |
| Optoisolated input | Digital | GPIO38 |

Set Arduino ESP32-S3 board to **8 MB flash, OPI PSRAM, USB CDC On Boot enabled**. Requires **Adafruit NeoPixel** library. The repo's GitHub Actions build exports a merged ESP32-S3 image for test flashing.

## How to try the application

**Do not connect to the real engine network until the physical power domains and passive electrical inputs are verified.** Test the AP, first-time wizard, LED and SD with the engine connections removed. Confirm firmware boot with USB Serial Monitor at 115200 baud and check \`http://192.168.4.1/api/health\`.

There are no synthetic motor values: Fuel Overview deliberately displays dashes until the UARTs receive valid compatible engine messages. That is expected, not a software error.

No manual trip start/finish is required to create raw sessions, but boat-journey grouping and durable fleet accounting are still pending.
