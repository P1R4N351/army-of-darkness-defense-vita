"""Reader/writer for Backflip archondb manifest.db files.

Format (decoded from the AoD 1.1.1 assets; verified by byte-identical round trip in tests):
  file      = sequence of varint-length-delimited protobuf messages
  message 0 = 1:{6:version} 3:{1:id 2:name}* 4:variant_count      (global string table)
  then per variant a pair:
    header  = 1:quality 2:language
    body    = 1:{1:id 2:name}*   variant-local strings (physical names)
              2:{1:id}*          physical files present in this variant
              4:{1:logical_id 2:physical_id}*   logical path -> physical file
"""
import sys


def varint(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 0x7f) << s; s += 7
        if c < 0x80:
            return r, i


def enc_varint(n):
    out = bytearray()
    while True:
        c = n & 0x7f; n >>= 7
        if n:
            out.append(c | 0x80)
        else:
            out.append(c)
            return bytes(out)


def fields(b):
    i = 0; out = []
    while i < len(b):
        k, i = varint(b, i); f, t = k >> 3, k & 7
        if t == 0: v, i = varint(b, i)
        elif t == 2:
            l, i = varint(b, i); v = b[i:i + l]; i += l
        elif t == 5: v = b[i:i + 4]; i += 4
        elif t == 1: v = b[i:i + 8]; i += 8
        else: raise ValueError('wiretype %d at %d' % (t, i))
        out.append((f, t, v))
    return out


def enc_fields(fs):
    out = bytearray()
    for f, t, v in fs:
        out += enc_varint((f << 3) | t)
        if t == 0: out += enc_varint(v)
        elif t == 2: out += enc_varint(len(v)) + v
        else: out += v
    return bytes(out)


def messages(d):
    i = 0; out = []
    while i < len(d):
        n, i = varint(d, i); out.append(d[i:i + n]); i += n
    if i != len(d):
        raise ValueError('trailing bytes in manifest')
    return out


def enc_messages(ms):
    return b''.join(enc_varint(len(m)) + m for m in ms)


def kv(v):
    return {a: c for a, _, c in fields(v)}


class Manifest:
    def __init__(self, data):
        self.msgs = messages(data)
        g = fields(self.msgs[0])
        self.globals = {}
        for f, t, v in g:
            if f == 3:
                e = kv(v); self.globals[e[1]] = e[2].decode()
            if f == 4:
                self.variant_count = v
        if len(self.msgs) != 1 + 2 * self.variant_count:
            raise ValueError('variant count mismatch')

    def variants(self):
        for k in range(self.variant_count):
            yield k, kv(self.msgs[1 + 2 * k]), fields(self.msgs[2 + 2 * k])

    def logical_id(self, name):
        ids = [i for i, n in self.globals.items() if n == name]
        if len(ids) != 1:
            raise KeyError('logical name %r resolves to %d ids' % (name, len(ids)))
        return ids[0]

    @staticmethod
    def variant_tables(body):
        strings = {}; present = []; mapping = []
        for f, t, v in body:
            if f == 1:
                e = kv(v); strings[e[1]] = e[2].decode()
            elif f == 2:
                present.append(kv(v)[1])
            elif f == 4:
                e = kv(v); mapping.append((e[1], e[2]))
        return strings, present, mapping

    def name_of(self, strings, pid):
        return strings.get(pid, self.globals.get(pid))

    def set_variant_body(self, k, body):
        self.msgs[2 + 2 * k] = enc_fields(body)

    def encode(self):
        return enc_messages(self.msgs)


if __name__ == '__main__':
    m = Manifest(open(sys.argv[1], 'rb').read())
    for k, hdr, body in m.variants():
        s, p, mp = m.variant_tables(body)
        print(k, hdr, 'strings', len(s), 'present', len(p), 'mapped', len(mp))
