"""Разовый поиск смещения произвольного Vital'а (Spirit/Rage/...) при известном раскладе.

Смещения HP/Mana/ES/Ward/Rage берутся из offsets.json. Порядок поиска:
1. Окно Life..Life+SPAN: целочисленные совпадения max; back-pointer основы обязан
   указывать на Life.
2. Если пусто — скан всех компонентов игрока (comp-вектор: компоненты и до, и после Life),
   owner = каждый comp-указатель, back-pointer обязан указывать на свой comp.
3. Если и там пусто — скан кучи (owner неизвестен, back-pointer проверяется на
   правдоподобие u64-указателя).

Тип значения --types: u32,i32,u16,i16,u64,i64 (по умолчанию все; значения вне диапазона
типа пропускаются). Выравнивание совпадения: 2 байта для 16-бит, 4 для 32/64-бит.
Current проверяется по тому же типу; --current необязателен (поиск только по max), в
выводе печатается фактическое значение в CurrentOff — видна дельта.

Имя owner-компонента разрешается через CLU bucket (Entity -> ComponentLookUpPtr -> имена).

    python find_vital.py --label Spirit --max 949
    python find_vital.py --label Rage --max 100 --current 42 [--pid N]
    python find_vital.py --label Spirit --max 110 --types i16,u32
    python find_vital.py --selftest

Запись в память процесса не выполняется.
"""
import argparse
import ctypes
import json
import os
import struct
import sys

if not hasattr(ctypes, "WinDLL"):
    # Cygwin-сборка python не даёт ctypes.WinDLL: перезапускаемся на Windows-нативном Python.
    import glob
    import shutil
    import subprocess

    script = os.path.realpath(__file__)
    if shutil.which("cygpath"):
        script = subprocess.run(["cygpath", "-w", script], capture_output=True,
                               text=True).stdout.strip()
    candidates = ["C:/Windows/py.exe"]
    candidates += sorted(glob.glob("C:/Users/*/AppData/Local/Programs/Python/Python*/python.exe"))
    for exe in candidates:
        if os.path.exists(exe):
            argv = [exe, "-3", script] + sys.argv[1:] if exe.endswith("py.exe") else [exe, script] + sys.argv[1:]
            os.execv(exe, argv)
    sys.exit("Нужен Windows-нативный Python 3 (64-bit): например "
             "C:/Users/<user>/AppData/Local/Programs/Python/Python312/python.exe find_vital.py ...")

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")     # кириллица в консоли Windows
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import poe_scan as ps
import find_offsets as fo

OFFS_FILE_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "offsets.json")
SPAN = 0x1000        # окно компонента: базовый Life 0x500 + запас на новые поля
TYPES = {"u32": "<I", "i32": "<i", "u16": "<H", "i16": "<h", "u64": "<Q", "i64": "<q"}
TYPENAME = {v: k for k, v in TYPES.items()}


def parse_types(text):
    fmts = []
    for t in text.split(","):
        t = t.strip()
        if t not in TYPES:
            raise SystemExit(f"--types: неизвестный тип {t}; доступны {sorted(TYPES)}")
        fmts.append(TYPES[t])
    return fmts


def offsets_from_file(path, rev):
    """OFFS[rev] + правки из offsets.json — та же конвертация, что в main poe_scan.py."""
    o = dict(ps.OFFS[rev])
    kv = {}
    raw = json.load(open(path, encoding="utf-8"))
    for k, v in raw.items():
        if v is None:            # --save не нашёл/не проверял поле — берём дефолт ревизии
            continue
        kv[k] = v if isinstance(v, int) else int(str(v), 16)
    t = kv.get("TotalOff", o["VitalTotal"])
    o["VitalTotal"] = t
    for k, name in (("HealthOff", "Health"), ("ManaOff", "Mana"), ("ESOff", "EnergyShield"),
                    ("WardOff", "Ward"), ("RageOff", "Rage")):
        if k in kv:
            o[name] = kv[k] - t
    if "CurrentOff" in kv:
        o["VitalCurrent"] = kv["CurrentOff"]
    if "BackPtrOff" in kv:
        o["VitalBackPtr"] = kv["BackPtrOff"]
    if "ReservedOff" in kv:
        o["ReservedPercent"] = kv["ReservedOff"]
    for key in ("CurrentStatePtr", "States", "TOTAL", "AreaInstanceData", "LocalPlayers",
                "EntityId", "EntityIsValid", "EntityDetailsName", "EntityDetailsLookup",
                "CLU_Bucket", "ReservedFlat"):
        if key in kv:
            o[key] = kv[key]
    return o


def find_life(mem, o):
    """Цепочка (A)-(J) из poe_scan.py, компактно: Life-компонент и comp-вектор игрока."""
    hits = ps.scan_signature(mem, mem.base, mem.size)
    if len(hits) != 1:
        raise SystemExit(f"(A) сигнатура: {len(hits)} совпадений — нужна актуальная ревизия")
    gs = mem.rd_u64(hits[0][2])
    if not gs:
        raise SystemExit("(B) GameStates* = 0 (игра не в игровом состоянии?)")
    states = mem.read(gs + o["States"], o["TOTAL"] * 16)
    if not states or len(states) < o["TOTAL"] * 16:
        raise SystemExit("(D) чтение States не удалось")
    ingame = struct.unpack_from("<Q", states, 4 * 16)[0]
    area = mem.rd_u64(ingame + o["AreaInstanceData"])
    if not area:
        raise SystemExit("(E) area = 0 (загрузка зоны)")
    found = ps.probe_local_players(mem, area, o["LocalPlayers"], o)
    if not found:
        raise SystemExit(f"(F) LocalPlayers не найден (окно ±0x40 от AreaInstance+0x{o['LocalPlayers']:X})")
    delta, off_lp, (first, last, end, count) = found[0]
    player = mem.rd_u64(first)
    if not player:
        raise SystemExit("(F) LocalPlayers пуст")
    if mem.rd_u8(player + o["EntityIsValid"]) != 0xC:
        raise SystemExit("(G/H) сущность невалидна: IsValid != 0xC")
    item = mem.read(player, 0x28)
    if not item or len(item) < 0x28:
        raise SystemExit("(I) чтение ItemStruct не удалось")
    _, details, comp_first, comp_last, _ = struct.unpack_from("<5Q", item)
    lookup = mem.rd_u64(details + o["EntityDetailsLookup"])
    bucket = mem.read(lookup + o["CLU_Bucket"], 36)
    if not bucket or len(bucket) < 36:
        raise SystemExit("(J) чтение CLU bucket не удалось")
    b_first, b_last = struct.unpack_from("<2Q", bucket)
    elems = (b_last - b_first) // 16
    for i in range(elems):
        rec = mem.read(b_first + i * 16, 16)
        if not rec or len(rec) < 12:
            continue
        np_, idx = struct.unpack_from("<QI", rec[:12])
        if ps.read_cstring(mem, np_) == "Life":
            life = mem.rd_u64(comp_first + idx * 8)
            if not life:
                raise SystemExit("Life-компонент = 0")
            ncomp = (comp_last - comp_first) // 8
            arr = mem.read(comp_first, ncomp * 8)
            comp_ptrs = list(struct.unpack_from(f"<{ncomp}Q", arr)) if arr and len(arr) >= ncomp * 8 else []
            return life, player, off_lp, comp_ptrs
    raise SystemExit("(J) Life-компонент не в bucket")


def read_window(mem, addr, span):
    """Блок по кускам: короткое чтение = конец committed-региона (компонент внутри одного)."""
    out = bytearray()
    pos = 0
    while pos < span:
        b = mem.read(addr + pos, min(0x100, span - pos))
        if not b:
            break
        out += b
        pos += len(b)
        if len(b) < 0x100:          # граница региона
            break
    return bytes(out)


def window_hits_t(buf, value, fmts):
    """(позиция, тип) всех int-совпадений value в буфере; выравнивание по размеру типа."""
    out = []
    for fmt in fmts:
        try:
            needle = struct.pack(fmt, value)
        except OverflowError:
            continue
        al = 4 if len(needle) >= 4 else 2
        i = buf.find(needle)
        while i >= 0:
            if i % al == 0:
                out.append((i, fmt))
            i = buf.find(needle, i + 1)
    return out


def scan_value_t(mem, value, fmt, regions, progress=None, chunk=1 << 20):
    """Позиции int == value (формат fmt) во всех регионах; стриминг как у find_offsets.scan_value."""
    try:
        needle = struct.pack(fmt, value)
    except OverflowError:
        return []
    al = 4 if len(needle) >= 4 else 2
    found = []
    seen = 0
    for base, size in regions:
        pos = 0
        while pos < size:
            n = min(chunk, size - pos)
            buf = mem.read(base + pos, n)
            if buf:
                i = buf.find(needle)
                while i >= 0:
                    if (base + pos + i) % al == 0:
                        found.append(base + pos + i)
                    i = buf.find(needle, i + 1)
            pos += max(n - len(needle) + 1, 1)
            seen += n
            if progress and seen >= progress[0]:
                print(f"  скан значений ({TYPENAME[fmt]}): {seen // (1 << 20)} МБ, "
                      f"совпадений {len(found)}", flush=True)
                progress[0] = seen + (1 << 28)
    return found


def make_readers(mem):
    """Случайный доступ по типу: rd(fmt, addr); u64/u32 — частные случаи."""
    def rd(fmt, addr):
        n = struct.calcsize(fmt)
        d = mem.read(addr, n)
        return struct.unpack(fmt, d)[0] if d and len(d) == n else None
    return rd, lambda a: rd("<Q", a), lambda a: rd("<I", a)


def validate(mem, o, base, owner, cur, require_backptr, fmt, rd, u64, u32):
    """Основа Vital'а -> скор структурных проверок по типу fmt; None, если Vital не подтверждён."""
    bp = o.get("VitalBackPtr", 0x10)
    target = u64(base + bp)
    if require_backptr:
        if target != owner:            # обязательное: back-pointer Vital'а на owner-компонент
            return None
    elif not target or target % 8:     # режим кучи: owner неизвестен, правдоподобный указатель
        return None
    score = 1
    vt = u64(base)
    vtab_in = bool(mem.base) and mem.base <= vt < mem.base + mem.size
    if vtab_in:
        score += 1
    ids = [u32(base + k) for k in (0x08, 0x0C, 0x20, 0x24, 0x28)]
    if all(v is not None and v <= 4096 for v in ids):
        score += 1
    flat, pct = u32(base + o["ReservedFlat"]), u32(base + o["ReservedPercent"])
    res_ok = flat is not None and pct is not None and 0 <= pct <= 10000 and 0 <= flat <= 10000
    if res_ok:
        score += 1
    cur_pos = None
    cur_val = rd(fmt, base + o["VitalCurrent"])   # фактическое значение в CurrentOff по типу fmt
    if cur is not None:
        if cur_val == cur:
            cur_pos = o["VitalCurrent"]
            score += 2
        else:
            needle_c = struct.pack(fmt, cur)
            al = 4 if len(needle_c) >= 4 else 2
            block = mem.read(base, 0x60)
            j = block.find(needle_c) if block else -1
            if j >= 0 and j % al == 0:
                score += 1
    if base % 8 == 0:
        score += 1
    return dict(base=base, Score=score, CurrentOff=cur_pos, CurrentVal=cur_val,
                Reserved=(flat, pct) if res_ok else None, VtabIn=vtab_in,
                Backptr=target, Fmt=TYPENAME[fmt])


def scan_candidates(mem, o, positions, owner, cur, require_backptr, rd, u64, u32):
    """(позиция Total, тип) -> кандидаты основ Vital'а; дубли одного base схлопываются по скором."""
    t = o["VitalTotal"]
    cands = {}
    for pos, fmt in positions:
        base = pos - t
        if base < 8:
            continue
        d = validate(mem, o, base, owner, cur, require_backptr, fmt, rd, u64, u32)
        if d and (base not in cands or d["Score"] > cands[base]["Score"]):
            d["pos"] = pos
            cands[base] = d
    return list(cands.values())


def scan_components(mem, o, comp_ptrs, value, cur, span, fmts, rd, u64, u32):
    """Скан всех компонентов игрока: Vital может лежать в компоненте до/после Life.
    Окно comp..comp+span; back-pointer основы обязан указывать на этот comp."""
    t = o["VitalTotal"]
    cands = {}
    total_hits = 0
    for comp in comp_ptrs:
        if not comp:
            continue
        buf = read_window(mem, comp, span)
        hits = window_hits_t(buf, value, fmts)
        total_hits += len(hits)
        for p, fmt in hits:
            base = comp + p - t
            d = validate(mem, o, base, comp, cur, True, fmt, rd, u64, u32)
            if d and (base not in cands or d["Score"] > cands[base]["Score"]):
                d["pos"] = comp + p
                cands[base] = d
    print(f"компонентов {len(comp_ptrs)}, суммарных совпадений: {total_hits}, "
          f"подтверждённых раскладок: {len(cands)}")
    return list(cands.values())


def resolve_owner(mem, o, comp):
    """Vital-основа -> имя owner-компонента: back-pointer (comp+0x08 = Entity), CLU bucket."""
    ent = mem.rd_u64(comp + 0x08)
    if not ent:
        return None
    item = mem.read(ent, 0x28)
    if not item or len(item) < 0x28:
        return None
    _, details, comp_first, comp_last, _ = struct.unpack_from("<5Q", item)
    lookup = mem.rd_u64(details + o["EntityDetailsLookup"])
    if not lookup:
        return None
    bucket = mem.read(lookup + o["CLU_Bucket"], 36)
    if not bucket or len(bucket) < 36:
        return None
    b_first, b_last = struct.unpack_from("<2Q", bucket)
    elems = (b_last - b_first) // 16
    for i in range(elems):
        rec = mem.read(b_first + i * 16, 16)
        if not rec or len(rec) < 12:
            continue
        np_, idx = struct.unpack_from("<QI", rec[:12])
        if mem.rd_u64(comp_first + idx * 8) == comp:
            return ps.read_cstring(mem, np_), idx
    return None


def report(cands, label, cur, mem=None, o=None, life=None):
    if not cands:
        print(f"кандидатов нет: ни одно совпадение не подтверждено структурой Vital'а. "
              f"Проверь значения (max != 0), --types, --span и ревизию")
        return None
    ordered = sorted(cands, key=lambda d: -d["Score"])
    print(f"кандидатов {label}-раскладки: {len(ordered)}")
    ccur = o["VitalCurrent"] if o is not None else None
    for d in ordered[:8]:
        cur_s = f"Current@+0x{d['CurrentOff']:X} == {cur}" if d["CurrentOff"] is not None \
            else (f"текущий не найден (по факту @+0x{ccur:X} = {d['CurrentVal']} {d['Fmt']} — "
                  f"передал {cur}, проверь дельту)" if cur is not None
                  else f"Current@+0x{ccur:X} = {d['CurrentVal']} ({d['Fmt']})" if ccur is not None
                  else "текущий не передавался")
        res = f"Reserved flat={d['Reserved'][0]} pct={d['Reserved'][1]}" if d["Reserved"] \
            else "Reserved не валидны"
        vtab = "vtable в модуле" if d["VtabIn"] else "vtable вне модуля"
        line = (f"  [score {d['Score']}] {label} Vital @+0x{d['base'] - life:X} (Total@+0x{d['pos'] - life:X}) | "
                f"{cur_s} | {res} | {vtab} | тип {d['Fmt']}")
        owner = resolve_owner(mem, o, d["Backptr"]) if mem is not None and o is not None else None
        if owner:
            line += f" | owner-компонент {owner[0]} index {owner[1]}"
            if owner[0] != "Life":
                # Vital вне Life: смещение d['base'] - d['Backptr'] — основа Vital'а внутри owner
                line += (f" (основа Vital внутри owner @+0x{d['base'] - d['Backptr']:X}, "
                         f"Total внутри owner @+0x{d['pos'] - d['Backptr']:X})")
        print(line)
    best = ordered[0]
    gap = ordered[1]["Score"] - best["Score"] if len(ordered) > 1 else None
    key = f"{label.capitalize()}Off"
    owner = resolve_owner(mem, o, best["Backptr"]) if mem is not None and o is not None else None
    if owner and owner[0] != "Life":
        print(f"  [!] Vital лежит не в Life-компоненте (owner {owner[0]} index {owner[1]}) — "
              f"смещение от Life бессмысленно; основа Vital внутри owner "
              f"@+0x{best['base'] - best['Backptr']:X} (Total @+0x{best['pos'] - best['Backptr']:X}, "
              f"тип {best['Fmt']})")
        print(f"  для poe_scan.py этот Vital нужен отдельным полем цепочки (компонент по имени), "
              f"Life-ключ {key} не пишется")
        return best
    tag = "подтверждён" if (gap is None and best["Score"] >= 6) or \
        (gap is not None and best["Score"] - gap >= 2) else "нужна проверка"
    rel = best["pos"] - life
    print(f"  [{tag}] python poe_scan.py --offs {key}=0x{rel:X} ({best['Fmt']})")
    print(f"  в offsets.json: \"{key}\": {rel}")
    return best


def build_fake_labels():
    """Синтетика find_offsets.build_fake + Ward @Life+0x288, Rage @Life+0x308.
    Spirit — Vital вне Life, в Position-компоненте @+0x40, Total/Current как int16
    (проверка скана компонентов, owner-разрешения и multi-type)."""
    import find_offsets as fo

    def vital(total, cur, fmt="<I", cfmt="<I"):
        w = bytearray(0x40)
        struct.pack_into("<Q", w, 0x00, 0x7FF716000000)      # vtable
        struct.pack_into("<I", w, 0x08, 1)
        struct.pack_into("<I", w, 0x0C, 2)
        struct.pack_into("<i", w, 0x18, 10)                  # ReservedFlat
        struct.pack_into("<i", w, 0x1C, 20)                  # ReservedPercent
        struct.pack_into("<I", w, 0x20, 7)
        struct.pack_into(fmt, w, 0x34, total)
        struct.pack_into(cfmt, w, 0x38, cur)
        return w

    m = fo.build_fake()
    comp = bytearray(m.blobs[0x600000])
    for base, total, cur in ((0x288, 300, 150), (0x308, 100, 25)):
        w = vital(total, cur)
        struct.pack_into("<Q", w, 0x10, 0x600000)            # back-pointer на Life
        comp[base:base + 0x40] = w
    m.blobs[0x600000] = bytes(comp)

    # Position-компонент: comp-вектор[1] = 0x700000, CLU index 1 ("Position"); Spirit @+0x40
    c2 = bytearray(0x600)
    struct.pack_into("<Q", c2, 0x00, 0x7FF716001000)
    struct.pack_into("<Q", c2, 0x08, 0x400000)               # back-pointer на Entity
    w = vital(110, 42, "<h", "<h")                          # Spirit как int16
    struct.pack_into("<Q", w, 0x10, 0x700000)                # back-pointer на свой comp
    c2[0x40:0x40 + 0x40] = w
    m.blobs[0x700000] = bytes(c2)
    m.index = fo.RegionIndex(sorted((b, len(d)) for b, d in m.blobs.items()))
    m.base, m.size, m.pid, m.peb = 0x7FF602440000, 0x4AD4265, 0, 0
    return m


def selftest():
    """Life-режим (Rage, 32-бит), скан компонентов (Spirit в Position) + owner-разрешение.
    int16 с нулевыми верхними байтами неотличим от u32/i32 — совпадения и offset одинаковы."""
    m = build_fake_labels()
    o = dict(ps.OFFS["rev33"])
    saved = ps.scan_signature
    anchor_live = m.base + 0x10E961
    ps.scan_signature = lambda mem, base, size: [(anchor_live, 0xB00000 - anchor_live - 4, 0xB00000)]
    rd, u64, u32 = make_readers(m)
    fmts = parse_types("u32,i32,u16,i16,u64,i64")
    life, player, off_lp, comp_ptrs = find_life(m, o)
    print(f"синтетика: Life @0x{life:X} player @0x{player:X} LocalPlayers@+0x{off_lp:X}, "
          f"компонентов {len(comp_ptrs)}")
    buf = read_window(m, life, SPAN)
    ok = True

    # Rage: Vital внутри Life (окно Life, owner Life index 0), 32-бит
    positions = [(p + life, f) for p, f in window_hits_t(buf, 100, fmts)]
    cands = scan_candidates(m, o, positions, life, 25, True, rd, u64, u32)
    best = report(cands, "Rage", 25, m, o, life)
    good = bool(best) and best["pos"] - life == 0x33C and best["CurrentOff"] == 0x38 \
        and best["Fmt"] in ("i32", "u32") and resolve_owner(m, o, best["Backptr"]) == ("Life", 0)
    print(f"ожидаемый RageOff 0x33C (32-бит, owner Life) найден: {good}")
    ok = ok and good

    # Spirit: Vital вне Life — окно Life пусто, скан компонентов, owner = Position index 1
    positions = [(p + life, f) for p, f in window_hits_t(buf, 110, fmts)]
    life_cands = scan_candidates(m, o, positions, life, 42, True, rd, u64, u32)
    comp_cands = scan_components(m, o, comp_ptrs, 110, 42, SPAN, fmts, rd, u64, u32)
    best = report(comp_cands, "Spirit", 42, m, o, life)
    owner = resolve_owner(m, o, best["Backptr"]) if best else None
    good = not life_cands and bool(best) and best["Fmt"] in ("i16", "u16", "u32", "i32") \
        and owner == ("Position", 1) and best["base"] - best["Backptr"] == 0x40
    print(f"Spirit 16-бит в Position-компоненте: owner {owner}, основа внутри owner 0x40 -> {good}")
    ok = ok and good
    ps.scan_signature = saved
    return ok


def main():
    ap = argparse.ArgumentParser(description="Разовый поиск смещения Vital'а (Spirit/Rage/...) "
                                             "при известном раскладе HP/Mana/ES, по любому int-типу")
    ap.add_argument("--label", required=True, help="имя параметра (Spirit/Rage/...), ключ JSON = LabelOff")
    ap.add_argument("--max", type=int, required=True, help="максимальное значение (Total)")
    ap.add_argument("--current", type=int, help="текущее значение (Current, необязателен)")
    ap.add_argument("--types", default="u32,i32,u16,i16,u64,i64",
                    help="целочисленные форматы поиска (по умолчанию все: u32,i32,u16,i16,u64,i64)")
    ap.add_argument("--pid", type=int, help="PID процесса (поиск по имени, если не задан)")
    ap.add_argument("--offs-file", default=OFFS_FILE_DEFAULT,
                    help="JSON со смещениями (по умолчанию offsets.json рядом со скриптом)")
    ap.add_argument("--rev", default=ps.DEFAULT_REV, help=f"ревизия по умолчанию ({ps.DEFAULT_REV})")
    ap.add_argument("--span", type=lambda x: int(x, 0), default=SPAN,
                    help="размер окна компонента (по умолчанию 0x1000)")
    ap.add_argument("--heap", action="store_true",
                    help="пропустить Life и компоненты, сразу сканировать кучу")
    ap.add_argument("--selftest", action="store_true",
                    help="проверка алгоритма на синтетике (Rage 32-бит в Life, Spirit 16-бит в Position)")
    args = ap.parse_args()

    if args.selftest:
        raise SystemExit(0 if selftest() else 1)

    fmts = parse_types(args.types)
    names = "/".join(TYPENAME[f] for f in fmts)
    if args.max == 0:
        print("внимание: max = 0 — нулей в куче тысячи, структурные проверки почти наверняка "
              "не отделят Vital; нужен персонаж с ненулевым значением")
    if args.current is not None and args.current == args.max:
        print("внимание: current == max — Current-проверка теряет отличительную силу")
    if args.current is None:
        print("поиск только по max (Current-проверка +2 не участвует, отбор по структурным); "
              "в выводе печатается фактическое значение в CurrentOff — видна дельта")

    o = offsets_from_file(args.offs_file, args.rev)
    t = o["VitalTotal"]
    print(f"смещения: TotalOff 0x{t:X}, CurrentOff 0x{o['VitalCurrent']:X}, "
          f"back-pointer 0x{o.get('VitalBackPtr', 0x10):X}, LocalPlayers 0x{o['LocalPlayers']:X}")

    pid = args.pid
    if not pid:
        pids = ps.find_pid(ps.TARGET_NAMES)
        if not pids:
            print("процесс игры не найден — запустите PathOfExile.exe")
            raise SystemExit(2)
        pid = pids[0][1]
        print("найдены процессы:", pids)

    # LiveMem: чтение + список committed-регионов (нужен для режима скана кучи)
    mem = fo.LiveMem(pid)
    life, player, off_lp, comp_ptrs = find_life(mem, o)
    rd, u64, u32 = make_readers(mem)
    print(f"Life-компонент @0x{life:X} (player @0x{player:X}, компонентов {len(comp_ptrs)})")

    cands = []
    if not args.heap:
        buf = read_window(mem, life, args.span)
        positions = [(p + life, f) for p, f in window_hits_t(buf, args.max, fmts)]
        print(f"окно Life..0x{args.span:X}: прочитано {len(buf)} байт, типов {names}: "
              f"совпадений {len(positions)}")
        cands = scan_candidates(mem, o, positions, life, args.current, True, rd, u64, u32)
    if not cands and not args.heap:
        print("в окне Life кандидатов нет — скан всех компонентов игрока (comp-вектор, "
              "и до, и после Life)")
        cands = scan_components(mem, o, comp_ptrs, args.max, args.current, args.span, fmts, rd, u64, u32)
    if not cands:
        print(f"кандидатов нет — скан кучи (owner Vital'а неизвестен), по {len(fmts)} типов, "
              f"~{22 * len(fmts)} с на 16 ГБ")
        regions = mem.regions()
        progress = [1 << 28]
        positions = []
        for fmt in fmts:
            positions += [(p, fmt) for p in scan_value_t(mem, args.max, fmt, regions, progress)]
        print(f"всего совпадений по всем типам: {len(positions)}")
        cands = scan_candidates(mem, o, positions, None, args.current, False, rd, u64, u32)

    report(cands, args.label, args.current, mem, o, life)
    mem.close()


if __name__ == "__main__":
    main()
