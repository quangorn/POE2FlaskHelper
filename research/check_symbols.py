"""Полный дамп Debug directory + статистика C++-маниглинга в PathOfExile.exe."""
import re
import struct

EXE = r"D:/Games/Path of Exile 2/PathOfExile.exe"
data = open(EXE, "rb").read()

pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
opt_size = struct.unpack_from("<H", data, pe + 0x14)[0]
sec_start = pe + 0x18 + opt_size

secs = []
for i in range(nsec):
    p = sec_start + i * 40
    nm = data[p:p + 8].rstrip(b"\0").decode("ascii", "replace")
    vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, p + 8)
    secs.append((nm, va, vsize, raw, rawsize))


def va_to_off(v):
    for nm, v0, vs, raw, rs in secs:
        if v0 <= v < v0 + max(vs, rs):
            return raw + (v - v0)
    return None


dd_base = pe + 0x18 + 112  # PE32+: DataDirectory[0] идёт после 112 байт опционального заголовка
TYPES = {0: "NONE", 1: "COFF", 2: "CODEVIEW", 3: "FPO", 4: "MISC", 5: "OMF",
         6: "MIPDB", 7: "OLDNESS", 13: "VMWindows", 14: "VMLinux"}

dd = va_to_off(dd_base and struct.unpack_from("<I", data, dd_base)[0])
debug_rva, debug_size = struct.unpack_from("<II", data, pe + 0x18 + 112 + 6 * 8)
dd = va_to_off(debug_rva)
print(f"Debug directory: RVA 0x{debug_rva:X} -> file 0x{dd:X}, {debug_size // 28} записей")

for i in range(debug_size // 28):
    p = dd + i * 28
    t, ptr, size, addr, rd = struct.unpack_from("<IIIII", data, p)
    name = TYPES.get(t, f"type{t}")
    extra = ""
    if t == 2 and rd > 0:
        blob = data[ptr:ptr + rd]
        sig = blob[:4]
        pdb = blob[4:].split(b"\x00")[0].decode("ascii", "replace")
        extra = f" sig={sig!r} pdb={pdb}"
    print(f"  [{i}] {name:10} size={size} VA=0x{addr:X} raw=0x{ptr:X} rd={rd}{extra}")

# C++ mangling: сколько ??-имён и в каких секциях они лежат
mang = [m.start() for m in re.finditer(rb"\?\?[A-Za-z0-9_@<>]{6,}", data)]
print(f"\nC++ ??-маниглингов: {len(mang)}")
by_sec = {}
for o in mang:
    va = va_to_off(o)
    for nm, v0, vs, raw, rs in secs:
        if raw <= o < raw + rs:
            by_sec[nm] = by_sec.get(nm, 0) + 1
            break
print("  по секциям:", by_sec)
print("  примеры:", [data[o:o + 40].split(b"\x00")[0].decode("ascii", "replace") for o in mang[:6]])

# C++ mangling вида _Z (Itanium/clang) — в PoE2的可能
z = [m.start() for m in re.finditer(rb"_Z[A-Za-z0-9_]{10,}", data)]
print(f"\nItanium-маниглингов (_Z): {len(z)}")
