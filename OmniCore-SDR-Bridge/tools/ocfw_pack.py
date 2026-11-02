#!/usr/bin/env python3
"""Build the OmniCore bridge release files.

  .ocfw      64-byte ocfw_header_t + application image; `och firmware update`
  Merge.bin  stage-0 + bootloader + application + header, for WCH-Link

Layout and header format: Common/ocfw.h.
"""
import argparse
import struct
import time
import zlib

BOOT_ADDR = 0x40000
FLAG_ADDR = 0x4E000
APP_ADDR = 0x20000
HEADER_OFFSET = 0x1FF00
HEADER_MAGIC = 0x5746434F
HEADER_VERSION = 1
HW_ID = 0x48417B01
BOOTID_MAGIC = 0x4C42434F
BOOTID_OFFSET = 0x40
HEADER_FMT = '<7I24s2I'  # everything before header_crc32 (60 bytes)


def header(image, version, build_id, build_unix):
    major, minor, patch = (int(x) for x in version.split('.'))
    body = struct.pack(HEADER_FMT, HEADER_MAGIC, HEADER_VERSION, HW_ID, len(image),
                       zlib.crc32(image), (major << 16) | (minor << 8) | patch, build_unix,
                       build_id.encode()[:23], 0, 0)
    return body + struct.pack('<I', zlib.crc32(body))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--version', required=True, help='bridge version, e.g. 1.0.0')
    ap.add_argument('--build-id', default='')
    ap.add_argument('--v3f', required=True)
    ap.add_argument('--boot', required=True)
    ap.add_argument('--app', required=True)
    ap.add_argument('--ocfw', required=True)
    ap.add_argument('--merge', required=True)
    a = ap.parse_args()

    v3f = open(a.v3f, 'rb').read()
    boot = open(a.boot, 'rb').read()
    app = open(a.app, 'rb').read()
    if len(v3f) > APP_ADDR:
        raise SystemExit(f'stage-0 is {len(v3f)} bytes, limit {APP_ADDR}')
    if len(boot) > FLAG_ADDR - BOOT_ADDR:
        raise SystemExit(f'bootloader is {len(boot)} bytes, limit {FLAG_ADDR - BOOT_ADDR}')
    if len(app) > HEADER_OFFSET:
        raise SystemExit(f'application is {len(app)} bytes, limit {HEADER_OFFSET}')
    if struct.unpack_from('<I', boot, BOOTID_OFFSET)[0] != BOOTID_MAGIC:
        raise SystemExit('bootloader image has no ocfw_bootid_t at +0x40')

    hdr = header(app, a.version, a.build_id, int(time.time()))
    with open(a.ocfw, 'wb') as f:
        f.write(hdr + app)

    merge = bytearray(b'\xff' * (APP_ADDR + HEADER_OFFSET + 256))
    merge[0:len(v3f)] = v3f
    merge[BOOT_ADDR:BOOT_ADDR + len(boot)] = boot
    merge[APP_ADDR:APP_ADDR + len(app)] = app
    merge[APP_ADDR + HEADER_OFFSET:APP_ADDR + HEADER_OFFSET + len(hdr)] = hdr
    with open(a.merge, 'wb') as f:
        f.write(merge)
    print(f'{a.ocfw}: {a.version} ({a.build_id}), {len(app)} bytes, crc32 {zlib.crc32(app):08x}')
    print(f'{a.merge}: {len(merge)} bytes (stage-0 {len(v3f)}, boot {len(boot)}, app {len(app)})')


if __name__ == '__main__':
    main()
