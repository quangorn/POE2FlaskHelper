"""Ищет в PathOfExile.exe области, где имена компонентов стоят рядом (кандидат на таблицу имён)."""
import re
import struct

EXE = r"D:/Games/Path of Exile 2/PathOfExile.exe"
data = open(EXE, "rb").read()

pe = struct.unpack_from("<I", data, 0x3C)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
opt = struct.unpack_from("<H", data, pe + 0x14)[0]
ss = pe + 0x18 + opt
secs = []
for i in range(nsec):
    p = ss + i * 40
    nm = data[p:p + 8].rstrip(b"\0").decode("ascii", "replace")
    vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, p + 8)
    secs.append((nm, va, vsize, raw, rawsize))


def off_to_va(o):
    for nm, v0, vs, raw, rs in secs:
        if raw <= o < raw + rs:
            return v0 + (o - raw)


names = ["Life", "Position", "Rotation", "Twitch", "Flee", "Deployed", "Velocity", "Aoe",
         "Laser", "Corpse", "Buff", "Buffs", "Model", "Player", "Monster", "Party",
         "PlayerHost", "Spirit", "Predator", "Summoner", "Charge", "Prepared", "ProxySet",
         "ModList", "ObjectFactory", "Stats", "Instance", "Shrine", "Chest"]

pos = {}
for n in names:
    pos[n] = [m.start() for m in re.finditer(re.escape(n.encode() + b"\x00"), data)]

# окна 4 КБ: считаем сколько имён компонентов попадает
best = []
anchors = sorted(set(o // 0x1000 for lst in pos.values() for o in lst))
for page in range(anchors[0] // 0x1000, anchors[-1] // 0x1000 + 2):
    lo, hi = page * 0x1000, page * 0x1000 + 0x1000
    inside = [n for n, lst in pos.items() if any(lo <= o < hi for o in lst)]
    if len(inside) >= 4:
        best.append((lo, sorted(inside)))

for lo, inside in best:
    va = off_to_va(lo)
    seg = data[lo:lo + 0x1000]
    strs = [m.group().decode("ascii", "replace") for m in re.finditer(rb"[ -~]{2,}", seg)]
    print(f"=== file 0x{lo:X} VA 0x{va:X}: {len(inside)} имён: {', '.join(inside)}")
    print("   первые строки:", strs[:14])
