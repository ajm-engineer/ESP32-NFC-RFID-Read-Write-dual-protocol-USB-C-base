#!/usr/bin/env python3
import argparse
import itertools
import json
import sys
import time

import serial
from serial.tools import list_ports

BAUD = 460800
USB_VIDS = {0x10C4, 0x1A86, 0x303A, 0x0403}
URI_PREFIXES = [
    (0x04, "https://"),
    (0x03, "http://"),
    (0x02, "https://www."),
    (0x01, "http://www."),
]


class StationError(Exception):
    pass


class Station:
    def __init__(self, port):
        self.ser = serial.serial_for_url(port, do_not_open=True, baudrate=BAUD, timeout=0.1)
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.ids = itertools.count(1)
        self.buf = b""

    def close(self):
        try:
            self.request("release", timeout=2)
        except StationError:
            pass
        self.ser.close()

    def _line(self, deadline):
        while time.monotonic() < deadline:
            if b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                try:
                    return json.loads(line.decode("utf-8", "replace"))
                except ValueError:
                    continue
            self.buf += self.ser.read(self.ser.in_waiting or 1)
        return None

    def request(self, cmd, timeout=5.0, **args):
        rid = next(self.ids)
        self.ser.write((json.dumps({"id": rid, "cmd": cmd, **args}) + "\n").encode())
        deadline = time.monotonic() + timeout
        while True:
            msg = self._line(deadline)
            if msg is None:
                raise StationError(f"no reply to '{cmd}'")
            if msg.get("id") != rid:
                continue
            if not msg.get("ok"):
                raise StationError(msg.get("error", "failed"))
            return msg

    def hello(self, attempts=4):
        for _ in range(attempts):
            try:
                return self.request("hello", timeout=1.5)
            except StationError:
                time.sleep(0.5)
        raise StationError("station did not answer (wrong port, or the port is in use?)")


def find_port():
    ports = [p for p in list_ports.comports() if p.vid in USB_VIDS]
    if len(ports) == 1:
        return ports[0].device
    names = ", ".join(p.device for p in list_ports.comports()) or "none"
    raise StationError(f"could not pick a port automatically (found: {names}); use --port")


def detect(st, rc=False):
    msg = st.request("rc_detect" if rc else "detect")
    return msg if msg.get("present") else None


def read_cc(st):
    cc = bytes.fromhex(st.request("t2read", page=3)["hex"])[:4]
    return cc


def ndef_uri(url):
    code, rest = 0x00, url
    for c, prefix in URI_PREFIXES:
        if url.startswith(prefix):
            code, rest = c, url[len(prefix):]
            break
    payload = bytes([code]) + rest.encode("utf-8")
    if len(payload) > 255:
        raise StationError("URL too long for a short record")
    record = bytes([0xD1, 0x01, len(payload)]) + b"U" + payload
    if len(record) < 0xFF:
        tlv = bytes([0x03, len(record)]) + record
    else:
        tlv = bytes([0x03, 0xFF, len(record) >> 8, len(record) & 0xFF]) + record
    tlv += b"\xFE"
    return tlv + b"\x00" * (-len(tlv) % 4)


def need_type2(card):
    if not card:
        raise StationError("no tag on the reader")
    if card["type"] != "type2":
        raise StationError(f"this needs an NTAG/Ultralight (Type 2) tag, found '{card['type']}'")


def cmd_info(st, a):
    h = st.hello()
    print(f"firmware  {h.get('fw')}")
    pn = h.get("pn5180", {})
    print(f"PN5180    {'ok' if pn.get('ok') else 'not detected'}  product {pn.get('product')}  firmware {pn.get('firmware')}")
    rc = h.get("rc522", {})
    print(f"RC522     {'ok ' + rc.get('version', '') if rc.get('ok') else 'not connected'}")


def cmd_scan(st, a):
    last = None
    while True:
        card = detect(st, a.rc)
        if not a.rc:
            st.request("release")
        uid = card["uid"] if card else None
        if card and (uid != last or not a.watch):
            extra = f"  ATS {card['ats']}" if card.get("ats") else ""
            print(f"{card['type']:<18} UID {uid}  SAK {card['sak']}{extra}", flush=True)
        elif not card and not a.watch:
            print("no card")
        last = uid
        if not a.watch:
            return
        time.sleep(0.3)


def cmd_dump(st, a):
    need_type2(detect(st))
    pages = a.pages
    if not pages:
        cc = read_cc(st)
        pages = 4 + cc[2] * 2 if cc[0] == 0xE1 else 16
    for start in range(0, pages, 4):
        block = bytes.fromhex(st.request("t2read", page=start)["hex"])
        for i in range(4):
            p = start + i
            if p >= pages:
                break
            b = block[i * 4:i * 4 + 4]
            text = "".join(chr(x) if 32 <= x < 127 else "." for x in b)
            print(f"{p:3d}  {b.hex(' ').upper()}  {text}")


def cmd_write_url(st, a):
    need_type2(detect(st))
    cc = read_cc(st)
    if cc[0] != 0xE1:
        raise StationError("tag is not NDEF formatted (capability container missing)")
    if cc[3] & 0x0F:
        raise StationError("tag is read-only")
    data = ndef_uri(a.url)
    capacity = cc[2] * 8
    if len(data) > capacity:
        raise StationError(f"needs {len(data)} bytes, tag holds {capacity}")
    for i in range(0, len(data), 4):
        st.request("t2write", page=4 + i // 4, hex=data[i:i + 4].hex())
    back = b""
    for i in range(0, len(data), 16):
        back += bytes.fromhex(st.request("t2read", page=4 + i // 4)["hex"])
    if back[:len(data)] != data:
        raise StationError("verify failed: read-back does not match")
    print(f"wrote and verified {len(data)} bytes: {a.url}")


def cmd_apdu(st, a):
    card = detect(st)
    if not card or card["type"] != "isodep":
        raise StationError("no ISO-DEP (ISO 14443-4) card on the reader")
    r = st.request("apdu", hex=a.hex.replace(" ", ""), timeout=30)["hex"]
    print(f"{r[:-4] or '(no data)'}  SW {r[-4:]}" if len(r) >= 4 else r)


def cmd_mfc_read(st, a):
    args = {"block": a.block, "keyB": a.key_b}
    if a.key:
        args["key"] = a.key
    b = bytes.fromhex(st.request("rc_mfc_read", **args)["hex"])
    print(f"{a.block:3d}  {b.hex(' ').upper()}")


def cmd_led(st, a):
    args = {"state": a.state}
    if a.brightness is not None:
        args["brightness"] = a.brightness
    st.request("led", **args)


def main():
    p = argparse.ArgumentParser(description="Talk to an ESP32 + PN5180 NFC station over USB.")
    p.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0 (auto-detected if omitted)")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ports", help="list serial ports")
    sub.add_parser("info", help="firmware and reader versions")
    s = sub.add_parser("scan", help="read the UID of the card on the reader")
    s.add_argument("--watch", action="store_true", help="keep scanning")
    s.add_argument("--rc", action="store_true", help="use the RC522 instead of the PN5180")
    s = sub.add_parser("dump", help="read an NTAG/Ultralight tag")
    s.add_argument("--pages", type=int, help="number of pages to read")
    s = sub.add_parser("write-url", help="write a URL as an NDEF record to an NTAG/Ultralight tag")
    s.add_argument("url")
    s = sub.add_parser("apdu", help="send an APDU to an ISO-DEP card")
    s.add_argument("hex")
    s = sub.add_parser("mfc-read", help="read a MIFARE Classic block (RC522)")
    s.add_argument("block", type=int)
    s.add_argument("--key", help="12 hex characters (default FFFFFFFFFFFF)")
    s.add_argument("--key-b", action="store_true")
    s = sub.add_parser("led", help="set the status light")
    s.add_argument("state", choices=["waiting", "idle", "attention", "busy", "success", "error", "off"])
    s.add_argument("--brightness", type=int)
    a = p.parse_args()

    if a.cmd == "ports":
        for port in list_ports.comports():
            print(f"{port.device:<16} {port.description}")
        return 0

    try:
        st = Station(a.port or find_port())
    except (StationError, serial.SerialException) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    try:
        st.hello()
        {"info": cmd_info, "scan": cmd_scan, "dump": cmd_dump, "write-url": cmd_write_url,
         "apdu": cmd_apdu, "mfc-read": cmd_mfc_read, "led": cmd_led}[a.cmd](st, a)
        return 0
    except StationError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    finally:
        st.close()


if __name__ == "__main__":
    sys.exit(main())
