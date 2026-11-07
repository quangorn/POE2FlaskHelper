"""Сводная таблица смещений по всем извлечённым ревизиям (для сравнения)."""
import os
import re
import glob

REV_DIRS = sorted(glob.glob("code/rev*"))
FILES = {
    "GameStateOffset.cs": ["CurrentStatePtr", "States"],
    "GameStateBuffer.cs": ["TOTAL_STATES"],
    "InGameStateOffset.cs": ["AreaInstanceData", "ZoneSwitchCounter", "MsElapsed"],
    "AreaInstanceOffsets.cs": ["LocalPlayers", "CurrentAreaLevel", "CurrentAreaHash", "EntitiesCount"],
    "EntityOffsets.cs": ["Id", "IsValid"],
    "LifeOffset.cs": ["Health", "Mana", "EnergyShield"],
    "VitalStruct.cs": ["Total", "Current", "ReservedPercent", "ReservedFlat"],
}


def field_offsets(path):
    text = open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r"\[FieldOffset\((0x[0-9A-Fa-f]+)\)\]\s+(?:public\s+)?(?:\w+)\s+(\w+)", text):
        out[m.group(2)] = int(m.group(1), 16)
    m = re.search(r"TOTAL_STATES\s*=\s*(\d+)", text)
    if m:
        out["TOTAL_STATES"] = int(m.group(1))
    return out


def find(revdir, fname):
    hits = glob.glob(os.path.join(revdir, "**", fname), recursive=True)
    return field_offsets(hits[0]) if hits else {}


rows = []
for fname, fields in FILES.items():
    for f in fields:
        row = [fname, f]
        for rev in REV_DIRS:
            v = find(rev, fname).get(f)
            row.append(f"0x{v:X}" if v is not None else "-")
        rows.append(row)

widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
header = ["файл", "поле"] + REV_DIRS
lines = [" | ".join(h.ljust(widths[i]) for i, h in enumerate(header))]
lines.append("-".join("-" * w for w in widths))
for r in rows:
    lines.append(" | ".join(c.ljust(widths[i]) for i, c in enumerate(r)))
print("\n".join(lines))
