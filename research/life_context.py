"""Контекст вокруг строк 'Life' в PathOfExile.exe — проверка, есть ли таблица имён компонентов."""
import struct

EXE = r"D:/Games/Path of Exile 2/PathOfExile.exe"
data = open(EXE, "rb").read()

pe_off = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
sec_start = pe_off + 0x18 + struct.unpack_from("<H", data, pe_off + 0x14)[0]
sections = []
for i in range(nsec):
    p = sec_start + i * 40
    name = data[p:p + 8].rstrip(b"\0").decode("ascii", "replace")
    vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, p + 8)
    sections.append((name, va, vsize, raw, rawsize))

def off_to_va(off):
    for name, v, vs, raw, rs in sections:
        if raw <= off < raw + rs:
            return v + (off - raw)
    return None

def strings_near(off, span=400):
    seg = data[off - span // 2:off + span // 2]
    out = []
    for m in __import__("re").finditer(rb"[ -~]{3,}", seg):
        out.append(m.group().decode("ascii"))
    return out

for needle in (b"Life\x00", b"Monster\x00", b"Position\x00"):
    offs = [m.start() for m in __import__("re").finditer(__import__("re").escape(needle), data)]
    for o in offs[:4]:
        print(f"== {needle!r} file 0x{o:X} VA 0x{off_to_va(o):X}")
        print("   соседние строки:", strings_near(o, 300))
        print()
