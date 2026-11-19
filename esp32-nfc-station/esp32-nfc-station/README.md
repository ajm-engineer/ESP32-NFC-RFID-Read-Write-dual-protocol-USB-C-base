# ESP32 NFC Station

A USB NFC/RFID reader and writer built from an ESP32 DevKit and a PN5180 module, with an optional RC522 for MIFARE Classic. The firmware is a thin bridge: it finds cards and moves bytes, and a program on the computer does the rest. It talks JSON lines over USB serial, so anything that can open a serial port can drive it, from a Python script to a web page using Web Serial in Chrome or Edge.

A small command-line client (`host/nfc_station.py`) is included.

## Features

- ISO 14443-A card activation with 4, 7 and 10-byte UIDs
- ISO-DEP (ISO 14443-4) APDUs with frame chaining in both directions and waiting-time extensions
- NTAG / MIFARE Ultralight (Type 2) page reads and writes
- MIFARE Classic block reads and writes through the optional RC522, with key A or key B
- Only one 13.56 MHz field is on at a time, so the two readers never jam each other
- The field switches off automatically after 15 seconds without a command
- RGB status light with smooth fades
- The client writes URLs to NTAG tags as NDEF records and verifies them by reading them back

## Parts

- ESP32-WROOM-32 DevKit V1 (USB-C, CP2102 USB-serial chip)
- PN5180 NFC module (the common blue board with a printed antenna)
- Optional: RFID-RC522 module
- Optional: RGB LED (common cathode) and three resistors
- Female-to-female jumper wires and a USB-C data cable (not a charge-only cable)

## Wiring

### PN5180

| PN5180 | ESP32 | Notes |
|---|---|---|
| 5V | VIN (5V) | Powers the RF transmitter. Must be 5 V |
| 3.3V | 3V3 | Logic supply |
| GND | GND | |
| SCK | GPIO 18 | |
| MISO | GPIO 19 | |
| MOSI | GPIO 23 | |
| NSS | GPIO 15 | Chip select |
| BUSY | GPIO 5 | |
| RST | GPIO 21 | |

Leave IRQ, GPIO, AUX and REQ unconnected. The DevKit's onboard LED (GPIO 2) lights up while a card is selected.

### RC522 (optional)

| RC522 | ESP32 | Notes |
|---|---|---|
| 3.3V | 3V3 | **3.3 V only. 5 V will damage it.** |
| GND | GND | |
| SCK | GPIO 18 | Shared with the PN5180 |
| MISO | GPIO 19 | Shared with the PN5180 |
| MOSI | GPIO 23 | Shared with the PN5180 |
| SDA | GPIO 4 | The RC522's chip select |
| RST | GPIO 22 | |

Leave IRQ unconnected and keep the two antennas at least 10 cm apart. The RC522 has a weaker field than the PN5180 and can't read 125 kHz cards.

### Status light (optional)

| LED leg | Through | To |
|---|---|---|
| Common cathode (longest leg) | | GND |
| Red | 150 Ω | GPIO 25 |
| Green | 33 Ω | GPIO 26 |
| Blue | 33 Ω | GPIO 27 |

For a common-anode LED, set `RGB_ACTIVE_LOW` to `true` in `firmware/src/pins.h`. A diffused LED gives an even glow instead of three visible dots.

| Light | Meaning |
|---|---|
| Rainbow sweep | Starting up |
| Slow blue breathing | Powered, waiting for the computer |
| Soft steady white | Connected and ready |
| Tiny white flash | A card was detected |
| Slow red blinking | The PN5180 isn't responding (check the wiring) |
| Five red blinks | A command failed |
| Amber breathing, cyan pulse, green | Set by the host program, e.g. "place a card", "working", "done" |

## Flash the firmware

1. Install [VS Code](https://code.visualstudio.com/) and the PlatformIO IDE extension.
2. Open the `firmware` folder (File → Open Folder).
3. Plug in the ESP32. On Windows, install the Silicon Labs CP210x driver if no COM port appears.
4. Click PlatformIO's **Upload** button. If it stalls on "Connecting...", hold the board's **BOOT** button until the upload starts.
5. Optional: open the Serial Monitor at 460800 baud. You should see `{"event":"ready",...}`. Close the monitor before using the client, since only one program can use the port at a time.

## Use the client

Python 3.8 or newer.

```bash
cd host
pip install -r requirements.txt

python nfc_station.py ports                          # list serial ports
python nfc_station.py info                           # firmware and reader versions
python nfc_station.py scan                           # UID of the card on the reader
python nfc_station.py scan --watch                   # keep scanning (Ctrl+C to stop)
python nfc_station.py dump                           # read an NTAG/Ultralight tag
python nfc_station.py write-url https://example.com  # write a URL to an NTAG tag
python nfc_station.py apdu 00A4040007D2760000850101  # send an APDU to an ISO-DEP card
python nfc_station.py scan --rc                      # scan with the RC522
python nfc_station.py mfc-read 4                     # read a MIFARE Classic block (default key)
python nfc_station.py led success                    # set the status light
```

The port is detected automatically when one ESP32-style USB serial adapter is plugged in. Otherwise pass it before the command: `--port COM5` or `--port /dev/ttyUSB0`.

`write-url` writes only the NDEF data area. It never writes the lock bytes or the capability container, so it can't make a tag permanently read-only.

## Protocol

One JSON object per line at 460800 baud. Requests carry an `id` and replies echo it. Every reply has `ok`, and failures add an `error` message. Byte data is sent as hex strings.

```
-> {"id":1,"cmd":"hello"}
<- {"id":1,"ok":true,"fw":"esp32-nfc-station 0.3.0","pn5180":{...},"rc522":{...}}
```

| Command | Arguments | Reply |
|---|---|---|
| `hello` | | `fw`, `pn5180` and `rc522` versions |
| `detect` | | `present`, `type` (`isodep`, `type2`, `other`), `uid`, `atqa`, `sak`, `ats`. Leaves the field on |
| `apdu` | `hex` | `hex`: the response, including the status word |
| `t2read` | `page` | `hex`: 16 bytes (4 pages) |
| `t2write` | `page`, `hex` (4 bytes) | |
| `release` | | Deselects the card and turns the field off |
| `rf` | `on` | Turns the PN5180 field on or off |
| `rc_detect` | | RC522: `present`, `type` (`mifare_classic_1k`, `type2`, `isodep`, ...), `uid`, `sak` |
| `rc_mfc_read` | `block`, optional `key` (12 hex, default `FFFFFFFFFFFF`), `keyB` | `hex`: 16 bytes |
| `rc_mfc_write` | `block`, `hex` (16 bytes), optional `key`, `keyB`, `force` | Refuses block 0 and sector trailers unless `force` is true |
| `rc_off` | | Turns the RC522 off |
| `led` | `state` (`waiting`, `idle`, `attention`, `busy`, `success`, `error`, `off`), optional `brightness` (5-100) | |

After `detect`, send `release` before the next `detect`. A card that is already selected won't answer a new search.

## Tuning

- **Card not detected or drops out:** `rfOn()` in `pn5180.cpp` waits 10 ms after the field turns on. Cards that draw more power may need 50-150 ms.
- **Timeouts on slow cards:** cards can ask for more time, and that is handled automatically. If long operations still time out, raise the margin on `sFwtMs` in `rats()` in `iso14443.cpp`.
- **Garbled replies:** lower the SPI clock in `pn5180.cpp` (`7000000` to `2000000`) and keep the jumper wires short.
- **Weak field:** distance matters most, so lay the card flat and centered on the antenna. A module with a larger antenna helps next.

## Status

The protocol logic (activation for 4 and 7-byte UIDs, chaining in both directions, waiting-time extensions, Type 2 reads and writes) has been tested against simulated cards. Expect some tuning on real hardware; see Tuning.

## Disclaimer

This software and hardware design are provided "as is", without warranty of any kind. The author is not responsible for any damage, data loss, locked or destroyed cards, or misuse resulting from their use.

Only read, write or test cards and tags that you own or have explicit permission to use. Don't use this project to copy, clone or tamper with access badges, payment cards, transit cards or any other credential, to get into places or systems you aren't authorized to enter, or to break any law or policy. Writing the wrong data to a MIFARE Classic sector trailer or to an NTAG's lock bytes can make a card permanently unusable. You are responsible for following the radio regulations where you live.

## License

MIT. See [LICENSE](LICENSE).
