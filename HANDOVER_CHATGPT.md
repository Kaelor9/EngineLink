# EngineLink – handover til ChatGPT

Dato: 28. oktober 2026. Status: firmware **0.5.0** er merget til `main` (PR #5).

Kort om ejeren: svar altid på **dansk**, kort og konkret. Ejeren er ikke programmør, men har en klar smag for design. Lav aldrig om på beslutninger nedenfor uden at spørge først.

---

## 1. Hvad projektet er

EngineLink er et ESP32-S3-printkort på en **Fairline Targa 34** med **2× Volvo Penta KAD300 (EDC I)**. Kortet lytter på motorernes J1708-bus. Det viser brændstofforbrug, økonomi, ture og motordata i en telefon-app, som kortet selv serverer over sit eget Wi-Fi. Der er ingen internet- eller cloudafhængighed om bord.

- Repo: https://github.com/Kaelor9/EngineLink (offentligt)
- Installationsside (GitHub Pages): https://kaelor9.github.io/EngineLink/
- Appen på båden: Wi-Fi **EngineLink** (fabrikspassword `12345678`), derefter http://192.168.4.1 eller http://enginelink.local

Hovedformål: pålidelige brændstoftal pr. motor, L/nm, forbrug pr. tur, tank/rækkevidde og det mest økonomiske omdrejningstal. Manglende eller usikre data vises som **—** og må aldrig erstattes af 0 eller opdigtede tal.

## 2. Hardware

| Del | Detaljer |
|---|---|
| MCU | ESP32-S3-WROOM-1-N8R8 (8 MB flash, 8 MB OPI PSRAM) |
| J7 = kanal 1 | RX GPIO21, TX GPIO47, loddebro SJ1 skal lukkes for TX |
| J6 = kanal 2 | RX GPIO17, TX GPIO18, loddebro SJ2 |
| NMEA 2000 (CAN) | TX GPIO15, RX GPIO16, kun lytning (SOG og tid) |
| microSD | CS 10, MOSI 11, SCK 12, MISO 13, kortdetektion 14 (LOW = isat) |
| LED | SK6805 på GPIO48 |
| Isoleret indgang | GPIO38 |

Kanal 1 (J7) hedder ISO2 på diagrammet og kanal 2 hedder ISO1. Det er kun kosmetisk. Se også `docs/HARDWARE.md`.

## 3. KAD300 / EDC I-protokol (vigtigt)

Detaljer og evidensniveau findes i `docs/KAD300_EDC1.md`.

- Læseforespørgsel: `01 43 21 <index> <checksum>`. Svar: `83 C1 <n>1 <værdi…> <checksum>`.
- Skaleringer:
  - 256 omdr. ×0,25
  - 289 brændstof ×1/128 (1/256 kan vælges) × korrektion pr. motor
  - 293 spænding 0,01308 (0,0125 kan vælges)
  - 266 motortimer ×0,0027778
  - 264 gear: bits 0x18 (0 = N, 8 = F, 16 = R). F/R er **ikke bekræftet**.
- **Sikkerhedsregler, som aldrig må brydes:**
  - Der må kun sendes læseforespørgslen `01 43 21`. Ingen andre TX-frames.
  - Polling er opt-in. "Read engines automatically" er **slået fra som standard**. At slå den til kræver `confirm=POLL` i firmwaren og et bekræftelsesark i appen. En gendannet backup må aldrig slå den til.
  - Der må ikke polles, mens VODIA er tilsluttet.
- **Commit aldrig Volvo-proprietært materiale:** password til datasets.zip, CRB-substitutionstabellen og dekodet Volvo-firmware/database. Det må kun ligge lokalt.

## 4. Repo-struktur

```text
firmware/EngineLink/EngineLink.ino   al device-logik (~3100 linjer)
firmware/EngineLink/ui.h             GENERERET: gzip af appen, rediger den ikke i hånden
firmware/EngineLink/icons.h          app-ikon A1 (SVG + PNG 180/192/512)
firmware/EngineLink/rollback.cpp     slår bootloader-rollback til for OTA
firmware/ui/app.html                 KILDEN til telefon-appen (HTML+CSS+vanilla JS, ét dokument)
tools/build_ui.py                    app.html -> ui.h (gzip). --check bruges i CI
site/                                installationsside (ESP Web Tools)
docs/                                KAD300_EDC1, HARDWARE, PRODUCTION_ALPHA, RELEASING, ENGINE_TEST, releases/vX.md
experiments/                         gamle Lab-sketches, ikke appen
.github/workflows/                   validate.yml, build-engine.yml, pages.yml
```

### Firmware-arkitektur (0.5.0)

- **Lagring:**
  - NVS: indstillinger og livstidstotaler (struct `Totals`, magic 0xE1170005, 0.4-data migreres).
  - LittleFS på partitionen `spiffs`: `/trips.bin` (max 60 ture), `/fuel.bin` (max 30 tankninger), `/health.bin`, `/diag.bin` og `/prefs.json`. Alt skrives som tmp-fil + rename.
  - SD: rå J1708-logs `/EL%08lX.CSV`, som slettes efter `rawKeepDays`.
- **Wi-Fi-robusthed** (ejeren hader forbindelsestab, så dette må ikke forringes):
  - AP'et er altid tændt og genstartes aldrig på grund af routeren.
  - `WiFi.setSleep(false)`, `WIFI_PS_NONE`, 19,5 dBm og land EU 1–13.
  - `esp_wifi_set_inactive_time(AP, 600)` og max 6 klienter.
  - Med router startes AP'et på routerens kanal. Genforbindelse sker målrettet på kanal + BSSID med backoff fra 15 s til 300 s. Fuld scanning kun efter 6 fejl **og** 0 AP-klienter.
  - AP-vagthund hvert 3. sekund.
- **OTA:**
  - Fra fil: `/api/update`.
  - Fra GitHub: `/api/fw/check` henter `https://kaelor9.github.io/EngineLink/releases.json`, og `/api/fw/install` accepterer kun URL'er under `https://github.com/Kaelor9/EngineLink/releases/download/`. Det kører i en worker-task på core 0.
  - Ny firmware markerer sig først som gyldig efter 60 s sund drift. Ellers ruller bootloaderen tilbage.
- **App:**
  - `/` serveres gzip med ETag. Appen poller `/api/live` hvert 500 ms.
  - Efter 2 fejl vises en "Reconnecting…"-banner med backoff.
  - Fanerne er Trip, Economy, Tank, History, Engines og Settings.
  - Fuld API-liste findes i `firmware/README.md`.
- **Arduino-faldgruber:**
  - Typer/structs skal defineres før den første funktion, fordi Arduino genererer prototyper.
  - Ingen default-argumenter.
  - `extern "C"`-funktioner skal ligge i en separat `.cpp`.

### Design

Appen blev designet i en prototype sammen med ejeren, og den er **færdig**. Hold dig til den visuelle stil, som er Marine Precision:
- mørk navy, teal accent, rolige tal
- ens afstande top og bund i Settings
- skraldespandsikon og en tydelig liste over, hvad der slettes og beholdes, ved sletning
- farlige handlinger kræver, at man skriver WIPE eller ERASE

Valgmuligheder må ikke mærkes "Recommended". Ingen hyperaktive animationer.

## 5. Arbejdsgang: sådan skrives kode og pushes til git

Det her er præcis det, der er blevet gjort hver gang.

### 5.1 Branch

1. `git fetch origin main && git checkout -B <feature-branch> origin/main`. Branches har heddet `claude/<emne>`. Vælg et tilsvarende navn, fx `chatgpt/<emne>`.
2. Arbejd aldrig direkte på `main`.
3. Er en branchs PR allerede merget, så start en ny branch fra den nyeste `main`. Genbrug ikke den gamle PR.

### 5.2 Ændringer

- **Appen:** rediger kun `firmware/ui/app.html`. Kør derefter `python3 tools/build_ui.py`, som genererer `ui.h`. Commit begge filer. CI fejler, hvis de ikke passer sammen.
- **Version:** står **ét sted**, `#define EL_VERSION "x.y.z"` i `EngineLink.ino`. Pages-workflowet læser den derfra. Opdater også badge og status i `README.md` og `docs/PRODUCTION_ALPHA.md`.
- **Release-noter:** skriv `docs/releases/v<version>.md`. Teksten bliver brugt som GitHub Release-tekst og vises i appen.
- **Ny firmwareversion:** bump `EL_VERSION`. En eksisterende release overskrives aldrig. Bindestreg i versionen (fx `0.5.1-beta`) giver en pre-release, som kun vises under "Test versions".

### 5.3 Byg og test lokalt før push

```bash
# Arduino CLI med ESP32 core 3.3.x og biblioteket "Adafruit NeoPixel"
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc" \
  firmware/EngineLink

python3 tools/build_ui.py --check
# JS-syntakstjek af appen:
python3 -c "h=open('firmware/ui/app.html').read();s=h.index('<script>')+8;open('/tmp/ui.js','w').write(h[s:h.index('</script>',s)])"
node --check /tmp/ui.js
```

- Den sidste build brugte 1.471.551 bytes (44 % af en 3,3 MB OTA-slot), og der skal være plads til OTA.
- Appen blev testet i Chromium/Playwright mod en lille Node-mock-server. Den efterligner `/api/live`, `/api/trips`, `/api/fuel`, `/api/economy`, `/api/diag`, `/api/state`, `/api/fw/*` og `/api/wifi/*` og serverer `app.html`. Testen klikker alle faner og ark igennem, tager skærmbilleder og tjekker for `pageerror`. Mock-serveren ligger ikke i repo'et. Lav en tilsvarende, hvis UI'et ændres meget.
- Tjek før commit, at intet Volvo-proprietært materiale er med: `git diff --cached | grep -i "datasets.zip\|CRB\|substitution"`.

### 5.4 Commit og push

- Skriv commit-beskeden på engelsk: første linje er en kort titel, derefter punktopstilling over ændringerne.
- Sæt ingen AI-modelnavne i commits, PR'er eller kode.
- Push: `git push -u origin <branch>`. Ved netværksfejl prøves igen op til 4 gange med 2, 4, 8 og 16 s pause.

### 5.5 Pull request

- Opret en PR mod `main` med en **Summary**-sektion (hvad og hvorfor), en **Testing**-sektion (hvad der er kompileret og testet) og en linje om, hvad der **ikke er verificeret på hardware**.
- `gh pr create` virkede ikke i det miljø, hvor Claude kørte, fordi GraphQL er blokeret. REST virkede:
  `gh api repos/Kaelor9/EngineLink/pulls -f base=main -f head=<branch> -f title="…" -F body=@body.md`
- **Merg aldrig selv.** Ejeren merger med "Create a merge commit".
- Tjek CI efter push: `gh api repos/Kaelor9/EngineLink/commits/<sha>/check-runs`. Begge checks, `check` (validate.yml) og `compile` (build-engine.yml), skal være grønne.

### 5.6 Efter merge (automatisk)

`pages.yml` gør følgende:
1. Kompilerer firmwaren.
2. Laver `firmware.bin` (merged, flashes på adresse 0 via USB) og `enginelink-update.bin` (kun app, til Wi-Fi-opdatering).
3. Opretter GitHub Release `v<EL_VERSION>`, hvis den ikke findes.
4. Skriver `releases.json` (max 15 releases, noter max 300 tegn, fordi firmwaren afviser over 16 KB).
5. Udgiver alt på GitHub Pages.

Workflowet kræver `contents: write`. Fejler det, skal ejeren slå skriveadgang til under Settings → Actions → Workflow permissions.

Afslut altid med en kort dansk opsummering til ejeren: hvad der er lavet, PR-link, hvad ejeren skal gøre (merge, flashe) og hvad der skal testes på båden.

## 6. Status og hvad der mangler at blive verificeret på båden

- [ ] Første flash af 0.5.0 via USB fra installationssiden. Derefter kan kortet opdatere over Wi-Fi.
- [ ] Wi-Fi-stabilitet over en hel tur, med og uden bådrouter.
- [ ] OTA fra GitHub og at rollback virker.
- [ ] Gear F/R-bits (264).
- [ ] Styrbords motor (J6) læses korrekt.
- [ ] Brændstofskala 1/128 vs. 1/256 og korrektionsfaktor via tank-til-tank / fuld tankning.
- [ ] Spændingsfaktoren (multimetertjek i Diagnostics).
- [ ] "Hold skærmen tændt" og at Android tilbyder installation over http.
- [ ] NMEA 2000-bussen er ikke valideret.

Roadmap derefter: NMEA 2000-output af motor- og brændstofdata, login/autorisation til indstillinger og unikke enhedspasswords, samt flere sprog i appen.
