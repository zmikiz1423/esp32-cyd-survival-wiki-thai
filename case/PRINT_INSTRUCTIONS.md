---
title: Print & Assembly Instructions — ESP32 Touch CYD Cases (3.5" & 2.8")
tags:
  - 3d-printing
  - esp32
  - cyd
  - instructions
---

# Print & Assembly Guide — ESP32 Touch CYD Cases (3.5" & 2.8")

Hey makers! Jimmy here from BoxerDawg 3D. Here are the step-by-step print and assembly notes for both the **3.5" (`rev2`)** and **2.8" (`rev1`)** snap-fit enclosures.

Both models are engineered to print **100% support-free flat on Z=0** with zero hardware required.

---

## 1. Quick Download Links

| Model | Dedicated Folder | Ready-to-Print 3MF | Base STL | Top Bezel STL |
| :--- | :--- | :--- | :--- | :--- |
| **2.8" CYD (`ESP32-2432S028R`)** | [`2.8inch/`](2.8inch/README.md) | [`2.8inch/ESP32_28_Touch_Case_rev1.3mf`](2.8inch/ESP32_28_Touch_Case_rev1.3mf) | [`2.8inch/stl/1_ESP32_28_Case_Base_rev1.stl`](2.8inch/stl/1_ESP32_28_Case_Base_rev1.stl) | [`2.8inch/stl/2_ESP32_28_Case_Top_rev1.stl`](2.8inch/stl/2_ESP32_28_Case_Top_rev1.stl) |
| **3.5" CYD (`Hosyond E32R35T`)** | [`3.5inch/`](3.5inch/README.md) | [`3.5inch/ESP32_35_Touch_Case_rev2.3mf`](3.5inch/ESP32_35_Touch_Case_rev2.3mf) | [`3.5inch/stl/1_ESP32_35_Case_Base_rev2.stl`](3.5inch/stl/1_ESP32_35_Case_Base_rev2.stl) | [`3.5inch/stl/2_ESP32_35_Case_Top_rev2.stl`](3.5inch/stl/2_ESP32_35_Case_Top_rev2.stl) |

---

## 2. Recommended Slicer Settings (Both Models)

- **Slicer Software**: Bambu Studio, OrcaSlicer, or PrusaSlicer.
- **Orientation**: Both parts come pre-oriented **flat on `Z = 0`**:
  - **Base Shell**: Exterior floor facing down against the build plate.
  - **Top Bezel**: **Face-down on `Z = 0`**! This is intentional—it ensures the front face inherits the texture of your bed (like textured PEI) and the 45° screen chamfer prints cleanly with zero supports.
- **Supports**: **None (`0`)** — 100% support-free geometry.
- **Layer Height**: `0.20 mm` Standard (`0.4 mm` nozzle).
- **Wall Loops**: `3` walls (crucial for firm, snappy perimeter tabs).
- **Infill**: `15%` Gyroid.
- **Material**: PLA, PLA Tough, PLA Matte, PETG, or ABS/ASA.

---

## 3. Assembly Instructions

### A. 2.8" CYD Enclosure (`ESP32-2432S028R`)
1. **Drop in the Board**: Angle the USB port (Micro-USB or USB-C) into the left port opening, then lower the board flat into the base. The board rests securely on the 4 raised corner support bosses (`Z = 6.6 mm`).
2. **Snap on the Bezel**: Orient the bezel so the light-sensor cutout matches the LDR photoresistor at the top left. Press down along the perimeter until the locking tabs snap firmly into place.
3. **Disassembly**: Slip a thumbnail or plastic spudger into the bottom pry scoop to pop the bezel free.

### B. 3.5" CYD Enclosure (`Hosyond E32R35T`)
1. **Drop in the Board**: Lower your `101.50 × 55.50 mm` Hosyond 3.5" board into the base shell with the USB-C port facing left and the MicroSD slot facing the bottom scoop. The 4 corner mounting holes seat directly over the 4 tapered M3 alignment pins.
2. **Snap on the Top Bezel**: Place the bezel over the display and press down along the edges until the snap detents click.
3. **Disassembly**: Use either of the two pry slots along the bottom seam to release the tabs.
