#!/usr/bin/env python3
"""Build a reproducible unsigned homebrew TRP with clean, predicate-based metadata.

TRP writer and PNG encoder adapted from Golden Balloon (MIT), pinned in
PROVENANCE.md; copyright (c) 2026 Golden Balloon contributors.
"""
import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path
from xml.sax.saxutils import escape

HERE = Path(__file__).resolve().parent


def mapping():
    data = json.loads((HERE / 'mapping.json').read_text())
    trophies = data['trophies']
    if (data['schema'] != 1 or data['title_id'] != 'AODD00001' or
            data['communication_id'] != 'AODD00001' or
            [t['id'] for t in trophies] != list(range(52)) or
            len({t['key'] for t in trophies}) != 52):
        raise ValueError('invalid frozen v1 trophy mapping')
    return data


def metadata():
    """Port-authored labels and factual conditions; no copied publisher prose."""
    data = json.loads((HERE / 'metadata.json').read_text())
    if set(data) != {t['key'] for t in mapping()['trophies']}:
        raise ValueError('metadata must cover exactly the 52 frozen achievements')
    for item in data.values():
        for field, limit in [('name', 64), ('detail', 128), ('predicate', 256)]:
            if not isinstance(item.get(field), str) or not 1 <= len(item[field]) <= limit:
                raise ValueError('invalid trophy metadata ' + field)
    return data


def png(width, height, tid):
    """Original geometric badge: no external art, fonts or game textures."""
    def chunk(tag, data):
        return (struct.pack('>I', len(data)) + tag + data +
                struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff))
    rows = []
    for y in range(height):
        row = bytearray(b'\0')
        for x in range(width):
            border = min(x, y, width-1-x, height-1-y) < 8
            # 6-bit visible ID stripe distinguishes the 52 clean badges.
            stripe = height//3 < y < 2*height//3 and (tid >> min(5, x*6//width)) & 1
            row.extend((185, 130, 65, 255) if border or stripe else (22, 29, 38, 255))
        rows.append(row)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b''))


def xml(config=False):
    data = mapping()
    labels = metadata()
    lines = ['<!--Sce-Np-Trophy-Signature: ' + 'x'*320 + '-->',
             '<trophyconf version="1.1" platform="psp2" policy="large">',
             ' <npcommid>AODD00001_00</npcommid>',
             ' <trophyset-version>01.00</trophyset-version>',
             ' <parental-level license-area="default">0</parental-level>']
    if not config:
        lines += [' <title-name>Army of Darkness Defense - Homebrew</title-name>',
                  ' <title-detail>Local original-game achievement completions. Unofficial homebrew set.</title-detail>']
    for t in data['trophies']:
        attrs = 'id="%03d" hidden="no" ttype="B" pid="-1"' % t['id']
        if config:
            lines.append(' <trophy %s/>' % attrs)
        else:
            lines += [' <trophy %s>' % attrs,
                      '  <name>%s</name>' % escape(labels[t['key']]['name']),
                      '  <detail>%s</detail>' % escape(labels[t['key']]['detail']),
                      ' </trophy>']
    return ('\n'.join(lines + ['</trophyconf>']) + '\n').encode()


def trp(files):
    entries = list(files.items())
    offset = 64 + 64*len(entries)
    table, bodies = [], []
    for name, payload in entries:
        if len(name.encode('ascii')) > 31:
            raise ValueError('TRP member name too long')
        offset = (offset + 15) & ~15
        bodies.append((offset, payload))
        table.append(name.encode('ascii').ljust(32, b'\0') + struct.pack('>QQI12x', offset, len(payload), 0))
        offset += len(payload)
    image = bytearray(offset)
    image[:64] = struct.pack('>IIQIII20s16x', 0xDCA24D00, 2, offset, len(entries), 64, 0, b'\0'*20)
    image[64:64+len(entries)*64] = b''.join(table)
    for pos, payload in bodies:
        image[pos:pos+len(payload)] = payload
    image[0x1c:0x30] = hashlib.sha1(image).digest()
    return bytes(image)


def build():
    files = {'TROPCONF.SFM': xml(True), 'TROP.SFM': xml(), 'ICON0.PNG': png(320, 176, 63)}
    for t in mapping()['trophies']:
        files['TROP%03d.PNG' % t['id']] = png(240, 240, t['id'])
    return trp(files)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', required=True, type=Path)
    args = ap.parse_args()
    data = build()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    print('52 original mappings, AODD00001_00, %d bytes, sha256 %s' % (len(data), hashlib.sha256(data).hexdigest()))


if __name__ == '__main__':
    main()
