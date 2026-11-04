# GPS wiring how-to (2.8" CYD + ATGM336H)

This covers wiring the Teyleten ATGM336H GPS module and its ceramic antenna into the [2.8" GPS case](README.md). The firmware (1.6.0 and later, 2.8" board) reads the GPS and uses it for the map, the track and the radar screen, but none of that has been tried on real hardware yet. It is tracked in [#12](https://github.com/everyday3d/esp32-cyd-survival-wiki/issues/12). The pin labels should be confirmed on your own board before power-up (see the check list at the end).

## What you need
- Sunton `ESP32-2432S028R` (2.8" CYD) in the printed GPS case
- Teyleten ATGM336H module ([Amazon affiliate link](https://link.amazon/B066Kz9AX), about 16 x 13 mm) and its ceramic antenna (19 x 6 x 6.5 mm, 100 mm cable, 3 x 3 mm IPEX plug)
- A 4-pin 1.25 mm cable for the CYD's **CN1** connector (the board's pre-crimped "speaker/extension" style leads work), or 3 thin wires and a soldering iron
- Soldering iron, flux, thin solder, small tweezers, tape

## 1. How the parts connect

| GPS module | Goes to | CYD CN1 pin |
|---|---|---|
| VCC | 3V3 | 3V3 |
| GND | GND | GND |
| TXD (GPS sends) | GPIO 27 (CYD receives) | IO27 |
| RXD (GPS receives) | not connected | (IO22 is left spare) |

The module talks 9600 baud serial. The CYD only needs to listen, so just three wires are needed: power, ground and the GPS's TX line into GPIO 27. Do not connect RXD, and do not use 5 V: the CYD's pins are 3.3 V.

CN1 on the 2.8" board carries GND, IO22, IO27 and 3V3. Retailers list the pin order differently, so match the labels printed on **your** board's silkscreen, not the order in a listing.

The antenna is a passive ceramic one. It has no power wire; it plugs into the small IPEX (u.FL) socket on the module and the module powers it.

## 2. Solder the module
1. Cut the header strip so you only have the pins you need, or solder wires straight to the VCC, GND and TXD pads.
2. Keep joints flat and short. The module bay is about 15 mm wide and the lid closes down on it.
3. Solder and let it cool. Check there is no solder bridge between neighbouring pins.

## 3. Fit the antenna
1. Press the IPEX plug straight down onto the module's socket until it clicks. Do not pry sideways; the plug is tiny and easy to tear off.
2. Lay the ceramic antenna in the antenna bay on its raised pad, ceramic side up, with its flat face toward the sky. The lid has a thin 1.2 mm roof over it, which GPS signals pass through.
3. Drop the antenna cable into the open notch in the wall between the two bays. It is open at the top, so the wire lowers straight in.
4. Coil the extra cable in the module bay. About 100 mm is enough; do not pull it tight.

## 4. Route the wires to the CYD
1. Sit the module in its bay, antenna cable already in the notch.
2. Feed the three power and signal wires out through the wall passage into the main case. It lines up with CN1.
3. Plug the 4-pin cable into CN1 on the board (or solder to the pads). Check the orientation: it only fits one way.
4. Close the case. The lid should drop all the way down and click. If wires stop it, re-tuck them.

## 5. Check before you power up
- 3V3 on the module goes to 3V3 on CN1, not to an I/O pin.
- GND goes to GND.
- Only the GPS TXD goes to IO27.
- Nothing is shorted: with a meter on continuity, check 3V3 to GND shows no beep.
- The antenna plug is seated, and the antenna faces up toward the lid roof.

## 6. First run
- Take the device outside with a clear view of the sky. A first fix can take a few minutes.
- Open the **Map** and tap **Me** once it has a fix, or go to Mesh, then Nearby, then Radar. The **Share** button there turns on sharing your position with nearby boards.
- The module's small LED blinks when it has a fix (some boards also provide a PPS pin; it is not needed).

## Notes
- Cable and bay sizes are from measurements of one kit: module 16 x 13 mm, antenna 19 x 6 x 6.5 mm, IPEX plug 3 x 3 mm, cable 100 mm. Other kits may differ.
- Nothing here has been run on the finished board yet.