"""Проверка сигнатуры и имён компонентов в бинарнике PathOfExile.exe."""
import re
import struct

EXE = r"D:/Games/Path of Exile 2/PathOfExile.exe"
data = open(EXE, "rb").read()

# --- PE sections ---
pe_off = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe_off + 6)[0]
sections = []
sec_start = pe_off + 0x18 + struct.unpack_from("<H", data, pe_off + 0x14)[0]
for i in range(nsec):
    p = sec_start + i * 40
    name = data[p:p + 8].rstrip(b"\0").decode("ascii", "replace")
    vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, p + 8)
    sections.append((name, va, vsize, raw, rawsize))

print("Секции:")
for name, va, vsize, raw, rawsize in sections:
    print(f"  {name:8} VA 0x{va:X} size 0x{vsize:X} raw 0x{raw:X} 0x{rawsize:X}")


def va_to_off(va):
    for name, v, vs, raw, rs in sections:
        if v <= va < v + max(vs, rs):
            return raw + (va - v)
    return None


def off_to_va(off):
    for name, v, vs, raw, rs in sections:
        if raw <= off < raw + rs:
            return v + (off - raw)
    return None


base = 0x7FF712410000  # базовый адрес из живого лога (rev33)
target_static = 0x7FF7169FE348

# --- signature scan ---
prefix = bytes.fromhex("4883EC")          # 48 83 EC ??
mid = bytes.fromhex("488BF133ED48392D")   # 48 8B F1 33 ED 48 39 2D
pat = re.compile(re.escape(prefix) + b"." + re.escape(mid))

hits = []
for m in pat.finditer(data):
    off = m.start()
    disp_off = off + 12  # позиция ^ : 4 байта после 48 39 2D
    disp = struct.unpack_from("<i", data, disp_off)[0]
    anchor_va = off_to_va(off)
    if anchor_va is None:
        continue
    rip_base = off_to_va(disp_off) + 4    # адрес сразу после disp
    tgt = rip_base + disp
    hits.append((off, anchor_va, disp, tgt))

print(f"\nСовпадений сигнатуры: {len(hits)}")
for off, va, disp, tgt in hits[:12]:
    print(f"  file 0x{off:X} VA 0x{va:X} disp {disp:+d} -> RIP target 0x{tgt:X}")

# проверка совпадения с адресом из лога
for off, va, disp, tgt in hits:
    if tgt == target_static:
        print(f"\nСОВПАДЕНИЕ С ЖИВЫМ ЛОГОМ: file 0x{off:X} VA 0x{va:X} disp {disp:+d} -> 0x{tgt:X}")
        print(f"  смещение якоря от базы: 0x{va - base:X}  (лог: 0x45EE348)")
