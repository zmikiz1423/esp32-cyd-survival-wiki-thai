# ESP32 CYD Survival Wiki

**ฉบับภาษาไทยสำหรับ CYD 2.8 นิ้ว:** ดู [คำแนะนำภาษาไทย](README_TH.md) และใช้ `firmware-CYD28.bin` ที่โฟลเดอร์หลัก (แฟลชที่ `0x0`).

[![ESP Web Flasher](https://img.shields.io/badge/Web%20Flasher-1--Click%20Install-orange?style=for-the-badge&logo=espressif)](https://everyday3d.github.io/esp32-cyd-survival-wiki/)
[![3D Printable Case](https://img.shields.io/badge/3D%20Case-Snap--Fit%20(No%20Supports)-blue?style=for-the-badge&logo=bambulab)](case/README.md)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge)](LICENSE)

<p align="center">
  <img src="docs/preview_real_hardware_in_hand.jpg" alt="The Survival Wiki running on a 3.5 inch touchscreen" width="520">
</p>

<h3 align="center">When the grid goes down, the answers don't have to.</h3>

<p align="center">
A survival library, a text mesh and a Wi-Fi hotspot, all on an inexpensive touchscreen board you can toss in a pack.<br>
No internet. No cell service. No subscription. Power it up and tap.
</p>

## What you get

| | |
| :--- | :--- |
| 📖 **118 field guides** | Water, fire, shelter, first aid, weather, vehicles, home, radio, kids and family, hygiene and more, in 15 categories. Plain language, big type. |
| 📚 **FM 21-76** | The complete 233-page US Army Survival Manual, one tap away. |
| 🩺 **Triage assistant** | Tap what is wrong and two or three taps take you to the right guide. |
| 💬 **Mesh chat** | Text other boards with no router and no towers. Messages hop from board to board, and you see who heard you. |
| 🗺️ **Offline maps** | Your own saved map tiles, pan and zoom on the screen. Mark waypoints and see how far away they are. |
| 📍 **GPS (optional)** | With a GPS module, or just your phone: your position and a breadcrumb track on the map, positions shared over the mesh, and a radar screen. On the phone map page, tap **Share GPS** and the board uses your phone's position. New, still being tested. |
| 🧭 **Phone tools** | A compass, sun finder (sunrise, sunset and where the sun is), level and camera magnifier on your phone, using the phone's own sensors. Open **Tools** on the hotspot page. New, in the next release. |
| 📱 **Phone hotspot** | The board makes its own Wi-Fi network, so anyone nearby can read, search and chat from a phone. Add it to your home screen, or tap **Save to my phone** to keep every guide on the phone with no Wi-Fi. |
| 🔦 **Flashlight and SOS** | White light, red light to keep your night vision, and a Morse SOS strobe. |
| 🖨️ **Printable cases** | Snap-fit cases for both screen sizes, plus a 2.8" GPS case. No supports and no screws. |

**Flash it from your browser in one click:** [everyday3d.github.io/esp32-cyd-survival-wiki](https://everyday3d.github.io/esp32-cyd-survival-wiki/)

Works on the **2.8" Cheap Yellow Display (`ESP32-2432S028R`)** and the **3.5" Hosyond (`E32R35T`)**. Everything lives on a normal FAT32 microSD card and runs off any 5V USB battery bank or phone charger.

---

## 🆕 What's New

### New in 1.6.0: Triage, Map Waypoints and 12 New Guides (2.8" and 3.5" firmware)
- **Triage Assistant**: a red **Triage** tile first on the home screen. Tap what is wrong (someone hurt, a storm, trouble at home or in the car, lost, a child or older adult) and two or three taps take you straight to the right guide. **Back** inside a guide returns to the triage menu.
- **Map Waypoints**: on the **Map** screen, centre the crosshair on a spot and tap **Mark** to save it. **Pts** lists your waypoints with distance and direction, and tapping one jumps the map to it. Saved to `maps/waypoints.txt` on the SD card.
- **GPS Track and Position on the Map** (needs a GPS module, see `case/2.8inch-gps/WIRING.md`): a breadcrumb trail, a green dot for you, and yellow dots for mesh boards that share their position. **Me** centres the map on you. GPS features are new and have not been tested on real hardware yet.
- **GPS Position Sharing and Radar** (2.8" with GPS): boards can share their position over the mesh, and a radar screen shows where they are. Sharing is off until you turn it on, and positions are not encrypted.
- **12 New Guides, 118 in All**: drowning, electric shock, poisoning and overdose, diabetic emergency, stroke and heart attack, spinal injury, snow cave, wet-weather fire, food safety after an outage, the 72-hour kit, plus a new **Hygiene & Sanitation** category with an emergency toilet and handwashing with little water.
- **To get the new guides**, copy the repo's `sdcard/wiki/` folder to your microSD card. Keep your `config.txt` and your `maps` folder.

<details>
<summary><b>Earlier versions (1.5.0 and older)</b></summary>

### New in 1.5.0: Five New Categories and a Paged Home Screen (2.8" and 3.5" firmware)
- **Weather & Storms**: tornado, floods, hurricanes and storm surge, wildfire and smoke, blizzards and ice storms, earthquakes, extreme heat.
- **Vehicles & Roads**: stranded in winter, breakdowns, changing a tire, jump-starting, vehicle fire, a car in water, a car emergency kit.
- **Home & Utilities**: gas leaks, house fires, burst pipes, carbon monoxide, generator safety, utility shutoffs, downed power lines.
- **Radio & Comms**: choosing a radio, FRS/GMRS walkie-talkies, ham radio, making a Mayday call, NOAA weather radio, using the mesh, getting more range.
- **Kids & Family**: child first aid, fever, caring for a baby without power, helping kids cope, a family plan, lost children, older adults and special needs.
- **Paged Home Screen**: when there are more than 8 categories, arrows and a page number appear under the tiles.
- **Map Downloader App**: `scripts/map-downloader.bat` on Windows.
- **To get the new guides**, copy the contents of the repo's `sdcard/` folder to your microSD card. Keep your `config.txt` and your `maps` folder.

### New in 1.4.0: Phone Chat, Offline Maps and "Heard by" (2.8" CYD firmware)
- **Chat From Your Phone**: Join the `SurvivalWiki` hotspot and tap **Mesh chat** on the main page (or open `http://192.168.4.1/mesh`). Pick a name, tap a quick message or type your own, and the board sends it out to the other boards. You see messages as they arrive and who is nearby.
- **"Heard by" Status**: Like "Read" on a phone. Under each message you send you see **Sent**, then **Heard by Sam, Alex** as other boards confirm they got it. The board's own Mesh screen shows "heard 2" beside your messages. Boards on older firmware can't confirm, so update every unit.
- **Offline Maps**: USGS topo maps from the SD card. On a Windows PC, double-click `scripts/map-downloader.bat`: search for a place, pick a radius and zoom, check the size, and save straight to your SD card (US only, zoom 10 to 16). Prefer the command line? `scripts/get-map-tiles.ps1` does the same. Either way the tiles end up in a `maps` folder at the root of the card. Tap **Map** on the board to drag and zoom, or open `http://192.168.4.1/map` on your phone for pinch-zoom on a bigger screen.
- **Your Position on the Phone Map**: Tap **My GPS**. Phones only share their location on secure pages, so the page switches to `https://192.168.4.1/map`. Your phone shows a one-time certificate warning, because the board makes its own certificate: choose **Advanced**, then **Continue**. You can also tap **Set my spot** and tap the map.
- **Nearby and Distance**: **Near** on the Mesh screen lists boards with signal bars and a rough distance. It only works while the hotspot is **off**, because the signal listener stops phones from joining. Treat the distance as a rough guess.
- **Nuclear Emergency Guides**: Three new guides in First Steps: get inside, stay inside, stay tuned; cleaning up after fallout; and how long to stay.

> [!note] Map tiles
> Use the script for map tiles. It uses the USGS service, which allows downloads. Don't bulk-download tiles from OpenStreetMap's own servers: their usage policy forbids it.

---

### New in 1.3.0: Mesh Text Messaging (2.8" CYD firmware)
[![Mesh Messaging Spotlight](docs/screenshots/09_mesh_messaging_spotlight.png)](docs/screenshots/09_mesh_messaging_spotlight.png)
- **Talk Between Boards With No Signal**: Two or more Survival Wiki units can text each other over ESP-NOW. No router, no phone, no internet. Tap **Mesh** on the home bar.
- **One-Tap Quick Messages**: OK, Need help, Heading out, At camp, Water found, Come to me. Or type your own (up to 50 characters) on the on-screen keyboard.
- **Messages Hop**: Every board that hears a message passes it on, up to 4 hops, so a message can reach a unit that's out of direct range. Duplicates are dropped.
- **Who's Nearby**: Boards say hello every 30 seconds, so the Mesh screen shows how many are in range before anyone types.
- **Name Your Board**: Tap **Name** to set the name your messages go out under.
- **Works With the Hotspot**: Mesh shares the radio channel with the `SurvivalWiki` hotspot (channel 6 by default). Every board needs the same channel, so leave `config.txt` alone or change it on all of them.
- **No SD Card Needed**: The flashlight and mesh work even if the card is missing or broken.
- **Heads Up, It's Not Private**: Messages are not encrypted. Anyone in range running this firmware can read them.

### New in 1.2.0 (2.8" CYD firmware)
[![Flashlight and SOS Spotlight](docs/screenshots/07_flashlight_sos_spotlight.png)](docs/screenshots/07_flashlight_sos_spotlight.png)
[![Settings, Backup and Night Dim Spotlight](docs/screenshots/08_settings_backup_spotlight.png)](docs/screenshots/08_settings_backup_spotlight.png)

These landed in the 2.8" build first. The 3.5" build gets them in a later update.
- **Flashlight & SOS Mode**: Tap **Light** on the home bar. Tap the screen to cycle a full-brightness white light, a red light that keeps your night vision, and a flashing SOS signal (the board's RGB LED flashes along with it). One more tap exits. The screen never sleeps while the light is on.
- **Auto Night Dim**: The CYD has a light sensor on the back. Turn on **Auto night dim** in Settings and the screen drops to a low glow in a dark room, then comes back up when the lights do.
- **Settings Backup on the SD Card**: Brightness, sleep timeout, invert colors, Wi-Fi state, auto night dim, and your touch calibration are saved to the SD card automatically. Reflash the board and everything is restored on first boot, with no re-calibrating. **Settings → SD backup** has manual **Save** and **Load** buttons too.
- **Touch That Works on More Boards**: Touch presses are now detected by reading pressure from the touch chip, so it works on clone boards that don't wire up the touch interrupt pin. The calibration screen is easier too: three markers set well in from the screen edges, with a retry if the taps don't register.
- **A Hidden Surprise for Dog People**: There's an easter egg tucked in the home screen. Copy the latest `sdcard/` files to your card, and keep poking at the title.

</details>

## 📟 Supported Screens & Hardware

I made sure both popular display boards work seamlessly with the 1-click web flasher, precompiled binaries, and 3D printable cases:

| Screen & Board | Board Model | Resolution & Driver | Ports & Power | 3D Printable Case | Precompiled Firmware |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **3.5" Touch Display** | Hosyond / QDtech `E32R35T` ([Amazon](https://link.amazon/B0dBAeAXP)) | $320 \times 480$ (ST7796) | Centered USB-C | [`case/3.5inch/`](case/3.5inch/README.md) (`rev2`) | [`survival-wiki-full.bin`](firmware/bin/survival-wiki-full.bin) |
| **2.8" Cheap Yellow Display (CYD)** | Sunton `ESP32-2432S028R` ([Amazon](https://link.amazon/B0fETEIFF)) | $240 \times 320$ (ILI9341) | Micro-USB & Type-C `CYD2USB` | [`case/2.8inch/`](case/2.8inch/README.md) (`rev1`) | [`survival-wiki-28-full.bin`](firmware/bin/survival-wiki-28-full.bin) |

> [!tip] Note on Board Versions
> **Optional GPS module:** Teyleten ATGM336H with ceramic antenna ([Amazon](https://link.amazon/B066Kz9AX)), used by the [2.8" GPS case](case/2.8inch-gps/README.md).
>
> Amazon links on this page are affiliate links: as an Amazon Associate I earn from qualifying purchases, at no extra cost to you.

> The 3.5" case is tailored specifically for the Hosyond `E32R35T` with the centered USB-C port. Older Sunton 3.5" boards (`ESP32-3248S035`) with off-center Micro-USB use different pinouts and hole locations, so stick to the `E32R35T` board for the 3.5" print.

```
web-flasher/     1-click browser web flasher (Chrome / Edge Web Serial) + precompiled binaries
firmware/        PlatformIO source code + ready-to-flash bin/ files
sdcard/          MicroSD card files (copy directly to root of a FAT32 card)
case/            3D printable 2-piece snap-fit cases (.3mf, .stl, .blend, and guides)
docs/            GitHub Pages web flasher and documentation assets
screenshots/     UI preview screenshots
```

---

## 🌟 What's Inside

Here is a quick look at the features and tools packed into the Survival Wiki:

### 1. Touchscreen Emergency Dashboard (3.5" & 2.8")
[![Stay Prepared: the Survival Wiki on a 2.8 inch touchscreen](docs/screenshots/01_touch_dashboard_stay_prepared.png)](docs/screenshots/01_touch_dashboard_stay_prepared.png)
- **Triage Tile and Category Tiles**: a red Triage tile first, then 1-tap access to every category (First Steps, Water, Fire, Shelter, First Aid, Food, Navigation and more) on pages you flip with arrows.
- **Live Status Bar**: Shows Wi-Fi hotspot state, connected devices, battery voltage, and microSD status.
- **Fast Card Reading**: Instant category navigation reads guides directly off your microSD card without lag.
- **Easy Bottom Bar**: Jump between Books, the Wi-Fi hotspot, the flashlight, Mesh messaging, the Map, and Setup with one touch.

### 2. Illustrated Field Guides & Clear Diagrams
[![Field Guide Reader Spotlight](docs/screenshots/02_field_guide_reader_spotlight.png)](docs/screenshots/02_field_guide_reader_spotlight.png)
- **118 Illustrated Guides**: Step-by-step visuals for water filtration, solar stills, traps, splints, and emergency shelters.
- **High-Contrast Dark Theme**: Easy to read in direct sunlight, and preserves your night vision in the dark.
- **Smooth Touch Paging**: Clean touch paging with percentage progress, fast-jump controls, and instant return to the index.
- **Quick Trauma Reference**: Fast-access medical guides for severe bleeding, hypothermia, CPR, and shock.

### 3. Complete US Army Survival Manual (FM 21-76)
[![US Army Survival Manual Spotlight](docs/screenshots/03_us_army_survival_manual_spotlight.png)](docs/screenshots/03_us_army_survival_manual_spotlight.png)
- **Full Field Manual**: Complete 233-page survival manual split into 62 easy-reading chapters on your microSD card.
- **Core Field Skills**: Covers survival psychology, kits, finding water, and building fires in tough conditions.
- **Plants & Wildlife**: Plant identification guides, universal edibility testing steps, and snake bite treatment.
- **100% Offline**: Zero cell service, Wi-Fi, or satellite connection needed.

### 4. Offline Wi-Fi Hotspot for Phones & Tablets
[![Wireless Smartphone Hub Spotlight](docs/screenshots/04_wifi_portal_search_spotlight.png)](docs/screenshots/04_wifi_portal_search_spotlight.png)
- **No Apps Required**: Scan the on-screen QR code with your phone or tablet to join the `SurvivalWiki` hotspot.
- **Fast Offline Search**: Search all 118 guides and FM 21-76 chapters right from your phone's browser.
- **Night-Vision Theme**: Gentle red/dark theme won't ruin night vision during power outages.
- **Share with Friends**: Up to 8 phones or tablets can read different manuals at the same time.
- **Save to My Phone**: tap the button at the bottom of the page to download one file with all the guides, pictures and search. It opens on your phone with no Wi-Fi, even far from the board. Mesh chat and the map still need the board.
- **Add to Home Screen**: the page has an app icon and name, so it sits on your phone like an app.

### 5. 1-Click Browser Web Flasher (Now with Dual 3.5" & 2.8" Support)
[![Web Serial Flasher Spotlight](docs/screenshots/05_web_serial_flasher_spotlight.png)](https://everyday3d.github.io/esp32-cyd-survival-wiki/)
- **Zero Software or Drivers**: Flash the full firmware directly from Chrome or Edge using USB Web Serial in about 40 seconds.
- **Hardware Target Selector**: Pick your board with 1 click before flashing—instantly switches between **3.5" Hosyond (`E32R35T`)** and the newly added **2.8" Cheap Yellow Display (`ESP32-2432S028R`)**.
- **Automatic USB Detection**: Plugs straight into common CH340 and CP2102 USB serial chips without manual baud rate or port setup.
- **Precompiled Unified Binaries**: Automatically flashes the bootloader, partition table, and full Survival Wiki image cleanly at offset `0x0`.
- **Clean Install Option**: Tick **Erase Device** when prompted. It's the safest choice every time, and it fixes most touch and calibration problems after an update.

### 6. Support-Free Snap-Fit 3D Cases
[![Snap-Fit Field Case Spotlight](docs/screenshots/06_snap_fit_case_spotlight.png)](docs/screenshots/06_snap_fit_case_spotlight.png)
- **100% Support-Free**: Both halves print flat on your build plate. The top bezel prints face-down to pick up your textured PEI sheet.
- **Flush USB Cutouts**: Clean cable access for standard USB-C cables, right-angle adapters, and power banks.
- **Quick-Eject MicroSD Scoop**: Built-in thumb scoop makes swapping microSD cards effortless.
- **Built-in Cooling Vents**: Angled rear louvers keep the ESP32 cool during extended Wi-Fi hotspot use.

## ⚡ Quick Start in 3 Steps

### 1. Flash the Firmware (1-Click in Browser)
> [!tip] 1-Click Web Flasher — Zero Tools or Drivers Needed
> 1. Open Chrome or Edge and head over to the **[ESP32 CYD Survival Wiki Web Flasher](https://everyday3d.github.io/esp32-cyd-survival-wiki/)** (or open `web-flasher/index.html` locally).
> 2. Plug your display board into your computer with a good USB data cable.
> 3. Select your hardware target at the top of the flasher:
>    - **3.5" Display (Hosyond E32R35T)**: $320 \times 480$ ST7796 with centered USB-C.
>    - **2.8" Display (Classic CYD)**: $240 \times 320$ ILI9341 (`ESP32-2432S028R`).
> 4. Click **Connect / Install**, pick your serial COM port (`USB-SERIAL CH340` or `CP2102`), tick **Erase Device** (recommended every time), and let Web Serial write the full unified image in ~40 seconds!
>    - *If connection fails:* Hold down the **BOOT** button on the side of the board, click Connect, then release BOOT once connected.

*Alternative manual method:* Flash ready-to-flash unified binaries at offset `0x0` via [esptool-js](https://espressif.github.io/esptool-js/) or CLI `esptool.py`:
- **3.5" Hosyond Board:** [`firmware/bin/survival-wiki-full.bin`](firmware/bin/survival-wiki-full.bin)
- **2.8" Classic CYD Board:** [`firmware/bin/survival-wiki-28-full.bin`](firmware/bin/survival-wiki-28-full.bin)

---

### 2. Prepare the MicroSD Card
1. Format a microSD card as **FAT32** (cards up to 32GB work out-of-the-box; for 64GB+, format with FAT32 via Rufus/GUIFormat).
2. Copy **everything inside** the `sdcard/` folder directly to the **root** of your microSD card:
   ```
   / (microSD root)
   ├── config.txt
   ├── wiki/
   │   ├── categories.tsv
   │   ├── catalog.tsv
   │   ├── search.json.gz
   │   ├── articles/
   │   └── img/
   ├── maps/                  # optional offline map tiles (see scripts/get-map-tiles.ps1)
   ├── library/
   │   ├── catalog.tsv
   │   ├── field-guide/       # All articles compiled into single book
   │   └── fm21-76/           # US Army Survival Manual (FM 21-76, 62 parts + PDF)
   └── web/
       ├── index.html
       └── index.html.gz
   ```
3. Insert the card into the board's TF card slot.

---

### 3. Power On & Calibrate Touch
1. Power the board from any USB-C phone charger or power bank.
2. On first boot, the board prompts for **touchscreen calibration**: tap each marker as it appears (using a fingernail, pen tip, or stylus for best precision). The 2.8" CYD shows 3 markers, set in from the edges; the 3.5" shows 4 corner arrows. To redo it later, hold the screen during the splash or use **Settings → Touch calibration**.
3. The home screen loads! Tap **Wi-Fi** to display the QR code for phones to scan.

---

## 📱 Connecting Mobile Phones (Captive Portal)
- Join the Wi-Fi network **`SurvivalWiki`** (open network, no password required) or scan the on-screen QR code.
- The portal will pop up automatically. If not, open your browser and navigate to **`http://192.168.4.1`**.
- If your phone prompts "Wi-Fi has no internet access", select **Stay Connected** or **Use as-is**.
- Up to 8 devices can browse simultaneously. Includes dark mode and instant offline fuzzy search.
- To configure network SSID or set a Wi-Fi password: edit `config.txt` on the microSD card.

---

## 🖨️ 3D Printable Snap-Fit Cases (`case/`)
The `case/` directory includes official precision snap-fit enclosures designed in Blender 5.2 for both supported screen sizes:
- **Dedicated 2.8" CYD Enclosure (`case/2.8inch/`):** Tailored for Sunton `ESP32-2432S028R` with universal Micro-USB & Type-C clearance, 4 raised corner support bosses, rear stylus pinholes for BOOT/RESET, and debossed BoxerDawg3D base. Download [`case/2.8inch/ESP32_28_Touch_Case_rev1.3mf`](case/2.8inch/ESP32_28_Touch_Case_rev1.3mf).
- **Dedicated 3.5" CYD Enclosure (`case/3.5inch/`):** Tailored for Hosyond `E32R35T` / `E32N35T` with centered USB-C, 4 tapered M3 corner alignment pins, peripheral headers clearance, and passive cooling louvers. Download [`case/3.5inch/ESP32_35_Touch_Case_rev2.3mf`](case/3.5inch/ESP32_35_Touch_Case_rev2.3mf).
- **100% Support-Free Printing:** Both models print flat on `Z = 0` (Base on its back, Top Bezel face-down on textured PEI). Zero screws, brass inserts, or glue required.
- See the [`case/README.md`](case/README.md) hub and [`case/PRINT_INSTRUCTIONS.md`](case/PRINT_INSTRUCTIONS.md) for full hardware comparison and slicer guidelines.

---

## 🔋 Power Consumption
- Typical power draw is ~1.0–1.5 W with screen on and Wi-Fi active.
- A standard 10,000 mAh battery bank provides over 24 hours of continuous operation.
- **Power saving:** Dim screen brightness or configure sleep timeout in Settings. Tap the screen to wake instantly.

---

## 📝 Customizing & Adding Articles
Articles are standard Markdown documents located in `sdcard/wiki/articles/` with YAML metadata frontmatter:

```markdown
---
title: Purifying Water
category: water
order: 2
tags: boil, bleach, filter
summary: One-line summary displayed in article lists.
---
## Boiling
Bring water to a rolling boil for at least 1 full minute (3 minutes at high altitude).

![Water Filter Diagram](img/water_filter.jpg)
```

Each article file added to `sdcard/wiki/articles/` is referenced in `sdcard/wiki/catalog.tsv` (Title, Category, Summary, Filename) and displays in its respective category index immediately upon reboot.

---

## 🛠️ Building Firmware from Source (PlatformIO)
Requirements: VS Code with PlatformIO extension (or `pio` CLI).
```bash
cd firmware

# Build and flash for 3.5" Hosyond / QDtech E32R35T:
pio run -e survival-wiki-35 -t upload

# Build and flash for 2.8" Sunton ESP32-2432S028R CYD:
pio run -e survival-wiki-28 -t upload
```

---

## 📜 Board Pinout Reference

### 1. 3.5" Display Module (`Hosyond E32R35T`)
| Function | ESP32 Pin | Details |
|---|---|---|
| **LCD SPI** | SCK 14, MOSI 13, MISO 12 | ST7796U controller ($320 \times 480$), 40MHz SPI |
| **LCD Control** | CS 15, DC 2, Backlight 27 | PWM backlight control |
| **Touch SPI** | CS 33, IRQ 36 | XPT2046 resistive touch (HSPI) |
| **MicroSD SPI** | CS 5, SCK 18, MISO 19, MOSI 23 | VSPI bus |
| **RGB LED** | Red 22, Green 16, Blue 17 | Active LOW |
| **Audio** | Amp Enable 4, DAC 26 | Internal speaker driver |

### 2. 2.8" Display Module (`Sunton ESP32-2432S028R CYD`)
| Function | ESP32 Pin | Details |
|---|---|---|
| **LCD SPI** | SCK 14, MOSI 13, MISO 12 | ILI9341 controller ($240 \times 320$), 40MHz SPI |
| **LCD Control** | CS 15, DC 2, Backlight 21 | PWM backlight control |
| **Touch SPI** | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 | XPT2046 resistive touch on its **own** pins (separate from the display bus), read in software. Presses are detected by pressure, so the IRQ pin is optional. |
| **MicroSD SPI** | CS 5, SCK 18, MISO 19, MOSI 23 | VSPI bus |
| **RGB LED** | Red 4, Green 16, Blue 17 | Active LOW |
| **Light Sensor** | GPIO 34 (analog) | Reads about 0 in bright light, higher in the dark. Used by Auto night dim. |
| **Audio** | DAC 26 | Speaker output |

---

## ⚖️ Safety & License
- **Field Reference Notice:** This software is an informational reference guide, not a substitute for professional medical advice, certified wilderness survival training, or emergency services.
- **License:** Open source under the [MIT License](LICENSE).
- **Credits:**
  - Case design by [BoxerDawg 3D](https://boxerdawg3d.com).
  - TFT_eSPI & TJpg_Decoder by Bodmer.
  - QR Code generator by Project Nayuki.
# Thai edition / ฉบับภาษาไทย

This Thai edition is based on [Everyday3D's esp32-cyd-survival-wiki](https://github.com/everyday3d/esp32-cyd-survival-wiki). The Thai edition maintainer reports obtaining permission to adapt the original project. Thank you to Everyday3D / BoxerDawg 3D and the original contributors for creating and sharing this useful offline project. Original attribution and the MIT license are retained.

อ่านวิธีติดตั้งและใช้งานฉบับภาษาไทยที่ [README_TH.md](README_TH.md) และ [คู่มือแผนที่ออฟไลน์](OFFLINE_MAPS_TH.md)

