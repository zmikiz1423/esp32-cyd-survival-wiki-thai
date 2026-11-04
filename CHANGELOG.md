# Changelog

All notable changes to the **ESP32 CYD Survival Wiki** project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Multiple map sets on one SD card - 1.6.0-th4 (2026-11-04)

- Add a paginated CYD province selector for legacy `/maps` and separate `/maps-<slug>` folders, with no fixed application-level map-set count. SD capacity and filesystem limits still apply.
- Persist the active map set across restarts and keep map tiles, searchable POIs, GPX selection, waypoints and recorded tracks separate by set. GPS follow remains manual and does not automatically switch provinces.
- Route HTTP/HTTPS map requests to the selected set and refresh phone map data when the board selection changes.
- Preserve existing th3 cards without copying the full SD package again, add Thai `name.txt` labels automatically when province builds complete while preserving custom labels on resume, and document multi-province installation in `MULTI_MAPS_TH.md`.
- Software checks and ESP32 compilation do not replace testing this update on a physical CYD/GPS module.

### Offline province maps and live GPS - 1.6.0-th3 (2026-11-04)

- Add one-click, resumable whole-province map authoring from a checksum-verified Geofabrik Thailand OSM extract, covering province polygons and islands with a boundary margin. Local image batches checkpoint automatically at 3,000 images / 50 MB; this does not split remote tile-service requests.
- Bundle a pinned Windows x64 Python runtime and native OSM/geometry/Thai shaping libraries, plus a token-protected local browser UI with 77 provinces, coverage outline, zoom/size estimates, cancel/resume and complete-only activation.
- Render real OSM geometry through zoom 16, export a searchable Thai POI index, validate every baseline JPEG, preserve tracks/waypoints and reject mixed dataset/provider/province folders.
- Add live GPS map refresh/follow, north/scale/fix/speed indicators, missing-tile notices, standalone CYD Thai POI search and straight-line destination distance/bearing. Keep original mesh packets and existing app tools.
- Add bounded GPX route overlays on CYD and phone, offline phone POI search, board-GPS follow, and NMEA RMC/course/HDOP handling. Routes are sampled for display; this release does not compute road routes or turn instructions.
- Add `OFFLINE_MAPS_TH.md`, portable province helper packaging and software verification reports. Hardware testing of th3 remains outstanding.

### Thailand maps - 1.6.0-th2 (2026-11-02)

- Replace the default US-only map downloader with a local Windows/browser assistant using OpenFreeMap data, Thai Canvas labels, all 77 provincial capitals, place search and direct SD-folder selection. No end-user Python, Node.js or API key is required.
- Generate baseline 256px JPEGs compatible with the existing board and phone map viewers, with resume/cancel, validated atomic writes, source metadata, complete-only map activation and bounded service usage.
- Add visible source attribution to both map viewers, retain legacy USGS map support, and document setup/download/GPS use in `MAPS_TH.md`.

### Thai CYD 2.8 edition - 1.6.0-th1 (2026-11-02)

- Add embedded Sarabun glyphs shaped with HarfBuzz/FreeType, UTF-8 cluster-safe clipping and Thai word wrapping. Keep TFT_eSPI hardware drawing and all existing subsystems.
- Adjust small-screen spacing and book/nearby paging, add a Thai/English keyboard, and preserve the ESP-NOW packet format with valid UTF-8 byte limits.
- Translate and edit all 118 wiki articles, 62 FM 21-76 text parts, phone pages and 22 illustrated diagrams. Regenerate Thai search, 22 combined-guide parts, compressed assets and PDFs with searchable Unicode text.
- Preserve the complete 233-page original FM manual as an explicitly labelled reference appendix; mark its historical medical guidance.
- Add the merged `firmware-CYD28.bin` (flash offset `0x0`), `sdcard-TH.zip`, Thai installation/build instructions and validation reports. Physical board testing remains outstanding.

### Fixed
- **Prebuilt 2.8" CYD Web Flasher Image**: Built and deployed merged single-binary firmware image `survival-wiki-28-full.bin` (bootloader, partitions, boot_app0, and application image at offset `0x0`) across `docs/firmware/`, `web-flasher/firmware/`, and `firmware/bin/`, resolving 404 download errors when flashing 2.8" displays (Fixes #5).
- **Gitignore Rule Correction**: Narrowed case-insensitive `*MW*` pattern to `*MakerWorld*` and `MW_*` to prevent Windows filesystems from unintentionally matching the `mw` substring inside `firmware/` directories (Fixes #5).

### Added
- **Dedicated 2.8" & 3.5" Enclosure Directories**: Reorganized `case/` into dedicated `case/2.8inch/` and `case/3.5inch/` subdirectories with localized STLs, CAD source files, renders, and detailed hardware guides for each module form factor.
- **2.8" Snap-Fit Case 3MF Package**: Generated print-ready Bambu Studio / OrcaSlicer project `ESP32_28_Touch_Case_rev1.3mf` (1-plate, 100% support-free flat on Z=0, 0.20 mm standard layer height, 3 walls, 15% gyroid infill) for Sunton ESP32-2432S028R modules.
- **2.8" CYD Hardware Profile & PlatformIO Target**: Added dedicated `survival-wiki-28` build environment in `platformio.ini` for Sunton 2.8" ESP32-2432S028R Cheap Yellow Display modules with ILI9341 driver and XPT2046 touch configuration (Fixes #4).
- **Responsive 240x320 UI Engine**: Scaled touch UI across all screens (Header, Footer, 2x4 Emergency Home Grid with horizontal icon/label split, Category article lists with 6 rows/page, Settings, and Phone Access Wi-Fi QR view) to fit compact $240 \times 320$ displays natively (Fixes #4).
- **Dynamic Multi-Target Web Flasher**: Added model selector in `docs/index.html` and `web-flasher/index.html` allowing makers to choose between 3.5" (`E32R35T`) and 2.8" (`ESP32-2432S028R`) targets before flashing in 1 click over Web Serial (Fixes #4).
- **SPI Bus Decoupling**: Explicitly deselects `PIN_SD_CS` (GPIO 5) and `TOUCH_CS` (GPIO 33) high during early setup to prevent bus contention across shared HSPI/VSPI operations (Fixes #4).

---

## [1.0.0] - 2026-10-18

### Fixed
- **Prebuilt 2.8" CYD Web Flasher Image**: Built and deployed merged single-binary firmware image `survival-wiki-28-full.bin` (bootloader, partitions, boot_app0, and application image at offset `0x0`) across `docs/firmware/`, `web-flasher/firmware/`, and `firmware/bin/`, resolving 404 download errors when flashing 2.8" displays (Fixes #5).
- **Gitignore Rule Correction**: Narrowed case-insensitive `*MW*` pattern to `*MakerWorld*` and `MW_*` to prevent Windows filesystems from unintentionally matching the `mw` substring inside `firmware/` directories (Fixes #5).

### Added
- **Target Board Calibration**: Explicit support and documentation for the Hosyond 3.5" ESP32-32E Touchscreen Display ([Amazon ASIN B0D93MBWC2](https://www.amazon.com/dp/B0D93MBWC2)), QDtech / LCDWiki `E32R35T` (Resistive Touch) / `E32N35T` (CYD).
- **Consolidated Firmware**: Pre-built single-binary firmware (`survival-wiki-full.bin`) mapped at offset `0x0` with bootloader, partition table, and application flash image.
- **Hardware Test Utility**: Factory board test firmware (`board-test-full.bin`) for diagnosing display, backlight, and touch hardware.
- **1-Click Web Flasher**: Zero-install browser flashing utility using ESP Web Tools (Web Serial) live on GitHub Pages (`https://everyday3d.github.io/esp32-cyd-survival-wiki/`).
- **US Army Survival Manual (FM 21-76)**: Complete 233-page public-domain manual extracted into 62 paged chapters (`0001.txt` .. `0062.txt`) in `sdcard/library/fm21-76/`, plus uncompressed full PDF download over Wi-Fi.
- **Pre-Indexed MicroSD Content**: 61 full-text emergency guides across 8 categories, compressed search database (`search.json.gz`), and 10-part compiled `field-guide/`.
- **Standalone Wi-Fi Captive Portal**: Built-in AP (`SurvivalWiki`) serving an offline mobile web app with instant keyword search and diagram viewing.
- **3D Printable Snap-Fit Case (`rev2`)**: Modern 2-piece enclosure engineered in Blender 5.2 and Bambu Studio `.3mf` with centered USB-C cutout, M3 alignment pins, microSD scoop, cooling louvers, and debossed `BOXERDAWG3D` branding. 100% support-free flat printing.
