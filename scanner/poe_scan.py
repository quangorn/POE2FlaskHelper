"""Прототип сканера памяти PathOfExile.exe на чистом Python (ctypes, только чтение).

Логика повторяет C#-код ауры POE2-Mem: сигнатура -> GameStates* -> цепочка -> HP/MP/ES/Ward.
Запись в память процесса отсутствует.

    python poe_scan.py                 # живой запуск: найти процесс и прочитать значения
    python poe_scan.py --file          # офлайн-проверка: сигнатура в файле бинарника
    python poe_scan.py --rev rev32     # смещения ревизии 32
    python poe_scan.py --selftest      # цепочка на синтетике (офлайн, без процесса)
    python poe_scan.py --offs-file offsets.json --values   # только HP/Mana/ES, без трассировки
    python poe_scan.py --offs HealthOff=0x1DC,TotalOff=0x34,...,LocalPlayers=0x5C0
    python poe_scan.py --live --fps 2     # живой режим: одна строка HP/MP/ES, перерисовка 2 раз/с (Ctrl+C — выход)
"""
import ctypes
import json
import re
import struct
import sys
import time
from ctypes import wintypes

if not hasattr(ctypes, "WinDLL"):
    # Cygwin-сборка python не даёт ctypes.WinDLL: перезапускаемся на Windows-нативном Python.
    import glob
    import os
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
             "C:/Users/<user>/AppData/Local/Programs/Python/Python312/python.exe poe_scan.py ...")

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")     # кириллица в консоли Windows
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")     # SystemExit/argparse пишут в stderr

EXE_PATH = r"D:/Games/Path of Exile 2/PathOfExile.exe"
TARGET_NAMES = ("PathOfExile.exe", "PathOfExile_x64.exe", "PathOfExileSteam.exe", "PathOfExile_KG.exe")

# Смещения по ревизиям (из code/revNNN)
OFFS = {
    # rev34: текущий патч (проверено живым прогоном 2026-11-07): от rev33 отличается
    # LocalPlayers 0x5B8 -> 0x5C0 (+8) и добавленными Vital'ами Ward @+0x2E0 (Total@+0x314,
    # Current@+0x38) и Rage @+0x298 (Total@+0x2CC, Life index 4; Reserved flat=0 pct=0).
    # Разовые сканы find_ward/find_vital. Цепочка совпала.
    "rev34": dict(CurrentStatePtr=0x10, States=0x50, TOTAL=13, AreaInstanceData=0x290,
                  LocalPlayers=0x5C0, EntityId=0x88, EntityIsValid=0x8C,
                  EntityDetailsName=0x08, EntityDetailsLookup=0x28, CLU_Bucket=0x28,
                  Health=0x1A8, Mana=0x200, EnergyShield=0x240, Ward=0x2E0, Rage=0x298,
                  VitalTotal=0x34, VitalCurrent=0x38, ReservedFlat=0x18, ReservedPercent=0x1C),
    "rev33": dict(CurrentStatePtr=0x10, States=0x50, TOTAL=13, AreaInstanceData=0x290,
                  LocalPlayers=0x5B8, EntityId=0x88, EntityIsValid=0x8C,
                  EntityDetailsName=0x08, EntityDetailsLookup=0x28, CLU_Bucket=0x28,
                  Health=0x1A8, Mana=0x200, EnergyShield=0x240,
                  VitalTotal=0x34, VitalCurrent=0x38, ReservedFlat=0x18, ReservedPercent=0x1C),
    "rev32": dict(CurrentStatePtr=0x08, States=0x48, TOTAL=13, AreaInstanceData=0x290,
                  LocalPlayers=0x5A8, EntityId=0x88, EntityIsValid=0x8C,
                  EntityDetailsName=0x08, EntityDetailsLookup=0x28, CLU_Bucket=0x28,
                  Health=0x1A8, Mana=0x200, EnergyShield=0x240,
                  VitalTotal=0x34, VitalCurrent=0x38, ReservedFlat=0x18, ReservedPercent=0x1C),
    "rev2025-10-11": dict(CurrentStatePtr=0x08, States=0x48, TOTAL=12, AreaInstanceData=0x290,
                          LocalPlayers=0xA08, EntityId=0x80, EntityIsValid=0x84,
                          EntityDetailsName=0x08, EntityDetailsLookup=0x28, CLU_Bucket=0x28,
                          Health=0x1A8, Mana=0x1F8, EnergyShield=0x230,
                          VitalTotal=0x2C, VitalCurrent=0x30, ReservedFlat=0x10, ReservedPercent=0x14),
}

TEMPLATE = "48 83 EC ?? 48 8B F1 33 ED 48 39 2D ^ ?? ?? ?? ??"

DEFAULT_REV = "rev34"        # ревизия, проверенная живым прогоном; --rev переопределяет



def build_pattern():
    """Собирает regex из строки шаблона; '?' = маска, '^' = позиция якоря (disp32)."""
    toks = TEMPLATE.split()
    chunks, anchor, byte_index = [], None, 0
    for t in toks:
        if t == "^":
            anchor = byte_index
            continue
        if t == "??":
            chunks.append(b".")
            byte_index += 1
        else:
            chunks.append(re.escape(bytes.fromhex(t)))
            byte_index += 1
    return re.compile(b"".join(chunks)), anchor


def scan_signature(mem, base, size, chunk=1 << 20):
    """Скан образа модуля: (live-адрес якоря, disp32, live-адрес глобальной переменной)."""
    rx, anchor = build_pattern()
    hits, pos = [], 0
    while pos < size:
        n = min(chunk, size - pos)
        buf = mem.read(base + pos, n)
        if not buf:
            pos += n
            continue
        for m in rx.finditer(buf):
            if m.start() > n - 24:      # совпадение залезает в следующий chunk — пропуск
                continue
            disp = struct.unpack_from("<i", buf, m.start() + anchor)[0]
            anchor_live = base + pos + m.start() + anchor
            hits.append((anchor_live, disp, anchor_live + disp + 4))
        pos += n
    return hits


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
ntdll = ctypes.WinDLL("ntdll", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


class PROCESS_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [("ExitStatus", ctypes.c_long),
                ("_pad0", ctypes.c_long),
                ("PebBaseAddress", ctypes.c_void_p),
                ("AffinityMask", ctypes.c_ulonglong),
                ("BasePriority", ctypes.c_long),
                ("_pad1", ctypes.c_long),
                ("UniqueProcessId", ctypes.c_size_t),
                ("InheritedFromUniqueProcessId", ctypes.c_size_t)]


kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                ctypes.POINTER(wintypes.DWORD)]
psapi.EnumProcesses.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
ntdll.NtQueryInformationProcess.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                            ctypes.POINTER(PROCESS_BASIC_INFORMATION),
                                            ctypes.c_uint,
                                            ctypes.POINTER(ctypes.c_uint)]


def find_pid(names):
    """PID игры по имени exe. Toolhelp в этой среде возвращает 0 записей (err 24),
    поэтому EnumProcesses + QueryFullProcessImageNameW."""
    arr = (wintypes.DWORD * 8192)()
    need = wintypes.DWORD()
    if not psapi.EnumProcesses(ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(need)):
        return []
    lowered = [n.lower() for n in names]
    found = []
    for pid in arr[: need.value // 4]:
        h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(1024)
        if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            exe = buf.value.rsplit("\\", 1)[-1].lower()
            if exe in lowered:
                found.append((buf.value, pid))
        kernel32.CloseHandle(h)
    return found


class Mem:
    """Только чтение чужого процесса. Все значения — из ReadProcessMemory."""

    def __init__(self, pid):
        self.h = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not self.h:
            raise OSError(f"OpenProcess не удался, err={ctypes.get_last_error()}")
        info = PROCESS_BASIC_INFORMATION()
        n = ctypes.c_uint(0)
        st = ntdll.NtQueryInformationProcess(self.h, 0, ctypes.byref(info),
                                             ctypes.sizeof(info), ctypes.byref(n))
        if st != 0 or not info.PebBaseAddress:
            raise OSError(f"NtQueryInformationProcess, status=0x{st & 0xFFFFFFFF:X}")
        self.peb = info.PebBaseAddress
        self.pid = pid
        self.name = f"PID {pid}"
        self.base = self.rd_u64(self.peb + 0x10)   # PEB.ImageBaseAddress = base главного модуля
        if not self.base:
            raise OSError(f"чтение PEB.ImageBaseAddress не удался (процесс выходящий?)")
        pe = self.rd_u32(self.base + 0x3C)
        if not pe:
            raise OSError(f"чтение PE-заголовка @ 0x{self.base:X} не удался")
        self.size = self.rd_u32(self.base + pe + 0x18 + 0x40)  # OptionalHeader(PE32+).SizeOfImage
        if not self.size:
            raise OSError(f"чтение SizeOfImage @ 0x{self.base + pe + 0x18 + 0x40:X} не удался")

    def image_size(self):
        return self.size

    def read(self, addr, n):
        if addr is None or addr <= 0 or n <= 0:
            return None
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t(0)
        if not kernel32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
            return None
        return buf.raw[:got.value]

    def rd_u64(self, addr):
        d = self.read(addr, 8)
        return struct.unpack_from("<Q", d)[0] if d and len(d) == 8 else None

    def rd_u32(self, addr):
        d = self.read(addr, 4)
        return struct.unpack_from("<I", d)[0] if d and len(d) == 4 else None

    def rd_u16(self, addr):
        d = self.read(addr, 2)
        return struct.unpack_from("<H", d)[0] if d and len(d) == 2 else None

    def rd_u8(self, addr):
        d = self.read(addr, 1)
        return d[0] if d else None

    def close(self):
        kernel32.CloseHandle(self.h)


def read_cstring(mem, addr, maxlen=64):
    """Читает C-строку (ansi) по кусочам, пока не встретит NUL."""
    out = bytearray()
    pos = addr
    for _ in range(maxlen // 16 + 1):
        chunk = mem.read(pos, 16)
        if not chunk:
            break
        i = chunk.find(b"\x00")
        out += chunk if i < 0 else chunk[:i]
        if i >= 0:
            break
        pos += 16
    return out.decode("ascii", "replace")


def read_vector(mem, addr, max_count=64):
    """std::vector {First,Last,End} по адресу -> (first, last, end, count) или None."""
    vec = mem.read(addr, 24)
    if not vec or len(vec) < 24:      # у границы committed-региона чтение укорачивается
        return None
    first, last, end = struct.unpack_from("<3Q", vec)
    if not first or last <= first or last > end or (last - first) % 8:
        return None
    count = (last - first) // 8
    if not 0 < count <= max_count:
        return None
    return first, last, end, count


def probe_local_players(mem, area, off, o, radius=0x40):
    """LocalPlayers в окрестности ожидаемого смещения: игра патчем двигает поле на ±8..0x40.

    Кандидат принимается, только если тройка — валидный std::vector<IntPtr> и его первый
    элемент — Entity с IsValid == 0xC. Возвращает [(|сдвиг|, смещение, (first,last,end,count))],
    отсортированный: точное попадание первым, дальше ближайшие сдвиги.
    """
    out = []
    for delta in range(0, radius + 1, 8):
        for cur in ({off} if not delta else {off - delta, off + delta}):
            if cur < 0:
                continue
            v = read_vector(mem, area + cur)
            if not v:
                continue
            player = mem.rd_u64(v[0])
            if not player or mem.rd_u8(player + o["EntityIsValid"]) != 0xC:
                continue
            out.append((delta, cur, v))
    return sorted(out, key=lambda t: (t[0], t[1]))


def chain(mem, o, quiet=False, out=None):
    """Цепочка косвенной адресации: base -> GameStates* -> ... -> Life-компонент.
    quiet=True (--values): печатать только значения Vital'ов, трассировку шагов suppressed.
    out=dict: после скана записать global_addr, после bucket — life_index (кэш для --live)."""
    def p(*a, **k):
        if not quiet:
            print(*a, **k)
    # (A) скан сигнатуры
    base, size = mem.base, mem.size
    hits = scan_signature(mem, base, size)
    if len(hits) != 1:
        p(f"сигнатура: {len(hits)} совпадений — паттерн не уникален, нужны уточнения")
        for h in hits[:6]:
            p(f"  anchor 0x{h[0]:X} disp {h[1]:+d} target 0x{h[2]:X}")
        raise SystemExit(1)
    anchor_live, disp, global_addr = hits[0]
    if out is not None:
        out["global_addr"] = global_addr
    p(f"(A) base 0x{base:X}  anchor 0x{anchor_live:X} (base+0x{anchor_live - base:X})  disp {disp:+d}")
    p(f"    GameStates глобальная @ 0x{global_addr:X}  (base+0x{global_addr - base:X})")

    # (B) GameState object
    gs = mem.rd_u64(global_addr)
    p(f"(B) GameState object @ 0x{gs:X}")
    if not gs:
        raise SystemExit("GameState pointer = 0 (игра не в игровом состоянии?)")

    # (C) CurrentStatePtr: текущее смещение = *(Last - 0x10)
    vec = mem.read(gs + o["CurrentStatePtr"], 24)
    if not vec or len(vec) < 24:   # короткое чтение через границу региона
        raise SystemExit(f"(C) чтение CurrentStatePtr @GameState+0x{o['CurrentStatePtr']:X} не удалось")
    first, last, end = struct.unpack_from("<3Q", vec)
    cur = mem.rd_u64(last - 0x10) if last else None
    p(f"(C) CurrentStatePtr @+0x{o['CurrentStatePtr']:X}: First 0x{first:X} Last 0x{last:X} "
          f"count {(last - first) // 8} -> current 0x{cur:X}")

    # (D) States buffer, InGameState = States[4].X
    states = mem.read(gs + o["States"], o["TOTAL"] * 16)
    if not states or len(states) < o["TOTAL"] * 16:
        raise SystemExit(f"(D) чтение States @GameState+0x{o['States']:X} не удалось")
    ptrs = [struct.unpack_from("<2Q", states, i * 16)[0] for i in range(o["TOTAL"])]
    ingame = ptrs[4]
    p(f"(D) States @+0x{o['States']:X}, {o['TOTAL']}×16; InGameState(4) @ 0x{ingame:X}")

    # (E) AreaInstanceData
    area = mem.rd_u64(ingame + o["AreaInstanceData"])
    p(f"(E) AreaInstance @ 0x{area:X} (InGameState+0x{o['AreaInstanceData']:X})")

    # (F) LocalPlayers: поле патчами сдвигается — при непопадании ищем в окрестности ±0x40
    off_lp = o["LocalPlayers"]
    found = probe_local_players(mem, area, off_lp, o)
    if not found:
        raise SystemExit(f"(F) LocalPlayers не найден ни по AreaInstance+0x{off_lp:X}, ни в окне "
                         f"±0x40: тройка не std::vector или Entity невалидна. Проверь --rev "
                         f"(сейчас EntityIsValid=0x{o['EntityIsValid']:X}); смещения ищет "
                         f"find_offsets.py --hp .. --mana .. --es .. --save offsets.json")
    delta, off_lp, (first, last, end, count) = found[0]
    if delta:
        p(f"(F) ! AreaInstance+0x{o['LocalPlayers']:X} не вектор; LocalPlayers сдвинулся на "
          f"{delta:+#x} -> +0x{off_lp:X} (передай --offs LocalPlayers=0x{off_lp:X} или --rev)")
    p(f"(F) LocalPlayers @+0x{off_lp:X}: First 0x{first:X} Last 0x{last:X} End 0x{end:X}")
    players = mem.read(first, count * 8)
    player = struct.unpack_from("<Q", players)[0] if players else None
    p(f"(F) LocalPlayers: count={count}, Player entity @ {('0x%X' % player) if player else '—'}")
    if not player:
        raise SystemExit(f"LocalPlayers пуст: смещения этого патча не совпадают (проверь "
                         f"LocalPlayers текущей ревизии). Запусти find_offsets.py --pid {mem.pid} "
                         f"и передай найденные смещения в poe_scan.py --offs ...")

    # (G/H) Entity: IsValid, Id
    valid = mem.rd_u8(player + o["EntityIsValid"])
    pid = mem.rd_u32(player + o["EntityId"])
    p(f"(G/H) IsValid=0x{valid:X} (нужен 0xC), Id={pid}")
    if valid != 0xC:
        raise SystemExit("сущность невалидна — смещения не совпадают с патчем")

    # (I) ItemStruct: VTable@0x00, EntityDetailsPtr@0x08, ComponentListPtr vector @+0x10..0x28
    item = mem.read(player, 0x28)
    if not item or len(item) < 0x28:
        raise SystemExit("(I) чтение ItemStruct не удалось")
    vtable, details = struct.unpack_from("<2Q", item, 0x00)
    comp_first, comp_last, comp_end = struct.unpack_from("<3Q", item, 0x10)
    lookup = mem.rd_u64(details + o["EntityDetailsLookup"])
    wstr = mem.read(details + o["EntityDetailsName"], 32)
    if not wstr or len(wstr) < 24:
        raise SystemExit("(I) чтение EntityDetails name не удалось")
    buf_ptr, reserved, length, pad14, capacity, pad1c = struct.unpack_from("<2Q4i", wstr)
    if capacity <= 8:
        name_raw = (struct.pack("<Q", buf_ptr) + struct.pack("<Q", reserved))[: length * 2]
    else:
        name_raw = mem.read(buf_ptr, length * 2)
    path = name_raw.decode("utf-16le", "replace") if name_raw else ""
    p(f"(I) EntityDetails @ 0x{details:X} path={path!r} ComponentLookUpPtr @ 0x{lookup:X}")

    # (J) ComponentLookUp: bucket {Data(24), UnknownPtr(8), Capacity(4)}; элемент 16 байт
    bucket = mem.read(lookup + o["CLU_Bucket"], 24 + 8 + 4)
    if not bucket or len(bucket) < 36:
        raise SystemExit("(J) чтение CLU bucket не удалось")
    b_first, b_last, b_end, unknown, capacity = struct.unpack_from("<3QQI", bucket)
    elems = (b_last - b_first) // 16 if b_first else 0
    p(f"(J) bucket @+0x{o['CLU_Bucket']:X}: First 0x{b_first:X} Last 0x{b_last:X} "
          f"Capacity={capacity} elems={elems}")
    if capacity > 50:
        raise SystemExit(f"Capacity={capacity} > 50 — смещения не подходят")

    comp = mem.read(comp_first, (comp_last - comp_first) // 8 * 8) if comp_first and comp_last > comp_first else None
    life_addr = None
    names = {}
    for i in range(elems):
        rec = mem.read(b_first + i * 16, 16)
        if not rec or len(rec) < 12:
            continue
        np, idx = struct.unpack_from("<QI", rec[:12])
        s = read_cstring(mem, np)
        names[s] = idx
        if s == "Life" and comp and 0 <= idx < len(comp) // 8:
            life_addr = struct.unpack_from("<Q", comp, idx * 8)[0]
    if out is not None and "Life" in names:
        out["life_index"] = names["Life"]
    p(f"(J) Components: {len(names)} шт., Life index={names.get('Life')}, Life component @ 0x{life_addr:X}")
    if not life_addr:
        p("   первые имена:", list(names)[:20])
        raise SystemExit("Life-компонент не найден")

    # Vital'ы (Ward печатается, только если смещение есть в раскладе)
    stats = [("Health", o["Health"]), ("Mana", o["Mana"]), ("EnergyShield", o["EnergyShield"])]
    for extra in ("Ward", "Rage"):
        if o.get(extra):
            stats.append((extra, o[extra]))
    for label, off in stats:
        v = life_addr + off
        total = mem.rd_u32(v + o["VitalTotal"])
        current = mem.rd_u32(v + o["VitalCurrent"])
        flat = mem.rd_u32(v + o["ReservedFlat"])
        pct = mem.rd_u32(v + o["ReservedPercent"])
        reserved = int((pct / 10000 * total) + flat + 0.999999) if total else 0
        unres = total - reserved
        pr = round(100 * current / unres) if unres > 0 else 0
        print(f"    {label} @+0x{off:X}: Current={current} Total={total} "
              f"ReservedFlat={flat} ReservedPercent={pct} -> {pr}%")


def find_life_index(mem, b_first, elems):
    """Индекс Life в CLU-bucket по именам (живой перескан, когда кэш life_index невалиден)."""
    for i in range(elems):
        rec = mem.read(b_first + i * 16, 16)
        if not rec or len(rec) < 12:
            continue
        np, idx = struct.unpack_from("<QI", rec[:12])
        if read_cstring(mem, np) == "Life":
            return idx
    return None


def tick(mem, o, cache):
    """Живой тик: только динамические указатели; global_addr, off_lp, life_index — из кэса старта.
    ~8 ReadProcessMemory. Возвращает (ok, строка): True и значения HP/MP/ES,
    либо False и статус при разрыве цепочки."""
    gs = mem.rd_u64(cache["global_addr"])
    if gs is None:
        return False, "чтение global_addr не удалось — процесс закрыт?"
    if not gs:
        return False, "GameState=0 — загрузка/меню"
    states = mem.read(gs + o["States"], o["TOTAL"] * 16)
    if not states or len(states) < o["TOTAL"] * 16:   # короткое чтение через границу региона
        return False, "чтение States не удалось — процесс закрыт?"
    ingame = struct.unpack_from("<2Q", states, 4 * 16)[0]
    area = mem.rd_u64(ingame + o["AreaInstanceData"])
    if not area:
        return False, "area=0 — загрузка зоны"
    off_lp = cache.get("off_lp", o["LocalPlayers"])
    v = read_vector(mem, area + off_lp)
    if not v:
        found = probe_local_players(mem, area, o["LocalPlayers"], o)
        if not found:
            return False, "LocalPlayers не найден (окно ±0x40) — смещения не подходят"
        _, off_lp, v = found[0]
        cache["off_lp"] = off_lp
    player = mem.rd_u64(v[0])
    if not player:
        return False, "player=0 — загрузка"
    valid = mem.rd_u8(player + o["EntityIsValid"])
    if valid != 0xC:
        return False, f"сущность невалидна: IsValid=0x{valid:X}"
    item = mem.read(player, 0x28)
    if not item or len(item) < 0x28:
        return False, "чтение entity не удалось"
    _, details, comp_first, comp_last = struct.unpack_from("<4Q", item, 0x00)
    ncomp = (comp_last - comp_first) // 8 if comp_first and comp_last > comp_first else 0
    if not ncomp:
        return False, "вектор компонентов пуст"
    lookup = mem.rd_u64(details + o["EntityDetailsLookup"])
    if not lookup:
        return False, "ComponentLookUp=0"
    bucket = mem.read(lookup + o["CLU_Bucket"], 36)
    if not bucket or len(bucket) < 36:
        return False, "чтение bucket не удалось"
    b_first, b_last = struct.unpack_from("<2Q", bucket, 0x00)
    elems = (b_last - b_first) // 16 if b_first else 0
    rescan = False
    idx = cache.get("life_index")
    if idx is None or idx >= elems:
        idx = find_life_index(mem, b_first, elems)
        cache["life_index"] = idx
        rescan = True
    if idx is None:
        return False, "Life-компонент не в bucket"
    if idx >= ncomp:
        return False, f"Life index={idx} за границей comp-вектора ({ncomp})"
    life = mem.rd_u64(comp_first + idx * 8)
    if not life:
        return False, "Life-компонент = 0"
    lo = min(o["ReservedFlat"], o["VitalTotal"], o["VitalCurrent"])
    hi = max(o["ReservedFlat"] + 4, o["ReservedPercent"] + 4,
             o["VitalTotal"] + 4, o["VitalCurrent"] + 4)
    span = hi - lo
    parts = []
    stats = (("HP", "Health"), ("MP", "Mana"), ("ES", "EnergyShield"))
    if o.get("Ward"):
        stats += (("WR", "Ward"),)
    if o.get("Rage"):
        stats += (("RG", "Rage"),)
    for label, key in stats:
        base = life + o[key] + lo
        block = mem.read(base, span)   # одно чтение на vital: ReservedFlat..VitalCurrent
        if not block or len(block) < span:
            return False, f"чтение {label} не удалось"
        flat = struct.unpack_from("<I", block, o["ReservedFlat"] - lo)[0]
        pct = struct.unpack_from("<I", block, o["ReservedPercent"] - lo)[0]
        total = struct.unpack_from("<I", block, o["VitalTotal"] - lo)[0]
        current = struct.unpack_from("<I", block, o["VitalCurrent"] - lo)[0]
        reserved = int((pct / 10000 * total) + flat + 0.999999) if total else 0
        unres = total - reserved
        pr = round(100 * current / unres) if unres > 0 else 0
        parts.append(f"{label} {current:>6}/{total:>6} {pr:>3}%")
    line = " | ".join(parts)
    if rescan:
        line += " | life_index перескан"
    return True, line


class COORD(ctypes.Structure):
    _fields_ = [("X", ctypes.c_short), ("Y", ctypes.c_short)]


class SMALL_RECT(ctypes.Structure):
    _fields_ = [("Left", ctypes.c_short), ("Top", ctypes.c_short),
                ("Right", ctypes.c_short), ("Bottom", ctypes.c_short)]


class CONSOLE_SCREEN_BUFFER_INFO(ctypes.Structure):
    _fields_ = [("Size", COORD), ("Cursor", COORD), ("Attributes", wintypes.WORD),
                ("Window", SMALL_RECT), ("MaxWindowSize", COORD)]


kernel32.GetStdHandle.argtypes = [wintypes.DWORD]
kernel32.GetStdHandle.restype = wintypes.HANDLE
kernel32.GetConsoleScreenBufferInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(CONSOLE_SCREEN_BUFFER_INFO)]
kernel32.GetConsoleMode.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.SetConsoleMode.argtypes = [wintypes.HANDLE, wintypes.DWORD]


def console_width():
    """Ширина окна консоли (колонок); None, если вывод не консоль."""
    h = kernel32.GetStdHandle(-11)          # STD_OUTPUT_HANDLE
    info = CONSOLE_SCREEN_BUFFER_INFO()
    if not kernel32.GetConsoleScreenBufferInfo(h, ctypes.byref(info)):
        return None
    return info.Window.Right - info.Window.Left + 1


def enable_vt():
    """VT-обработка ANSI-эскейпов (\\x1b[J, перемещения курсора) в консоли; False, если консоль
    не принимает — тогда отрисовка остаётся старой: одна строка \\r + padding."""
    h = kernel32.GetStdHandle(-11)
    mode = wintypes.DWORD()
    if not kernel32.GetConsoleMode(h, ctypes.byref(mode)):
        return False
    return bool(kernel32.SetConsoleMode(h, mode.value | 0x0004))   # ENABLE_VIRTUAL_TERMINAL_PROCESSING


def pack_parts(text, width):
    """Живая строка режется по ' | ' на линии <=width (Vital'ы не рвутся); цельная
    статус-строка остаётся одной линией."""
    lines, cur = [], ""
    for p in text.split(" | "):
        cand = (cur + " | " + p) if cur else p
        if len(cand) <= width:
            cur = cand
        else:
            if cur:
                lines.append(cur)
            cur = p
    lines.append(cur)
    return lines


def process_alive(pid):
    """Жив ли процесс (OpenProcess LIMITED): детект выхода игрока из игры."""
    h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return False
    kernel32.CloseHandle(h)
    return True


def bootstrap(pid_hint, o, cache):
    """Старт/ребут живого режима: процесс -> скан сигнатуры -> полная цепочка (кэш global_addr,
    life_index). Возвращает (mem, None) при успехе, (None, статус) при неудаче; SystemExit
    из chain() (не залогинен, смещения не подошли) превращается в статус."""
    pids = find_pid(TARGET_NAMES)
    if not pids:
        return None, "процесс игры не найден — ожидание запуска"
    pid = pid_hint if pid_hint and process_alive(pid_hint) else pids[0][1]
    try:
        mem = Mem(pid)
    except OSError as e:
        return None, str(e)
    cache.clear()
    try:
        chain(mem, o, quiet=True, out=cache)
    except SystemExit as exc:
        mem.close()
        return None, f"цепочка не завершилась: {exc}"
    return mem, None


def live_run(o, fps, pid_hint):
    """Машина состояний живого режима: FAST — значения каждые 1/fps; статус (меню, не залогинен,
    процесс закрыт) — сон 5 с. При смерти процесса — ребут: find_pid + Mem + перескан сигнатуры.
    Строка перерисовывается НА МЕСТЕ при смене текста: VT-эскейпы чистят предыдущие линии
    (подъём курсора + \\x1b[J) и печатают новые, реанные по ' | ' под ширину консоли
    (Vital'ы не рвутся). Без VT — fallback одной строкой (\r + padding, обрезка)."""
    period = 1.0 / fps
    vt = enable_vt()
    last_text = None
    prev_n = 1        # линий предыдущей отрисовки (для подъёма курсора)
    cache = {}
    mem = None
    pid_cur = pid_hint
    dead = False

    def write(text):
        nonlocal last_text, prev_n
        if text == last_text:
            return
        w = console_width()
        width = min(120, w) if w else 120
        lines = [ln[:width] for ln in pack_parts(text, width)]
        if vt:
            # курсор стоит на последней линии предыдущей отрисовки: подняться, \r,
            # \x1b[J — очистить от курсора до конца экрана; печатать без \n на конце
            sys.stdout.write((f"\x1b[{prev_n - 1}A" if prev_n > 1 else "") + "\r\x1b[J")
            sys.stdout.write("\r\n".join(lines))
            sys.stdout.flush()
            prev_n = len(lines)
        else:
            sys.stdout.write("\r" + lines[0].ljust(width))
            sys.stdout.flush()
        last_text = text

    try:
        next_t = time.perf_counter()
        slow = True
        while True:
            if mem is None:
                mem, status = bootstrap(pid_hint, o, cache)
                if mem is None:
                    write(status)
                    slow = True
                    time.sleep(5)
                    continue
                pid_cur = mem.pid
                if dead:
                    write(f"процесс перезапущен (PID {pid_cur}): base 0x{mem.base:X} — перескан сигнатуры")
                    dead = False
            ok, text = tick(mem, o, cache)
            write(text)
            if not ok:
                slow = True
                # свой VM_READ хэндл держит объект процесса живым, OpenProcess на вышедшем
                # процессе с ним успехует — проверять живость нужно только отпустив mem.close()
                mem.close()
                if not process_alive(pid_cur):      # игрок вышел: хэндл мёртв — ребут
                    mem = None
                    cache.clear()
                    dead = True
                    write(f"процесс закрыт (PID {pid_cur}) — ожидание перезапуска")
                else:
                    try:                            # процесс жив (меню/загрузка): хэндл заново, кэш в силе
                        mem = Mem(pid_cur)
                    except OSError as e:
                        mem = None
                        cache.clear()
                        dead = True
                        write(f"процесс закрыт (PID {pid_cur}): {e} — ожидание перезапуска")
                time.sleep(5)
                continue
            if slow:                                  # возврат из статуса к значениям: новый отсчёт
                next_t = time.perf_counter()
                slow = False
            next_t += period
            now = time.perf_counter()
            if next_t < now:      # тик не вписался в период: без догона-очереди
                next_t = now + period
            time.sleep(next_t - now)
    except KeyboardInterrupt:
        sys.stdout.write("\n")
    if mem:
        mem.close()


def offline_file_check():
    """Скан сигнатуры по файлу бинарника (без процесса) и перевод совпадений в VA."""
    data = open(EXE_PATH, "rb").read()
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
        for nm, v, vs, raw, rs in secs:
            if raw <= o < raw + rs:
                return v + (o - raw)

    rx, anchor = build_pattern()
    hits = []
    for m in rx.finditer(data):
        disp = struct.unpack_from("<i", data, m.start() + anchor)[0]
        va = off_to_va(m.start() + anchor)
        hits.append((m.start(), va, disp, va + disp + 4))
    print(f"файл {EXE_PATH}: {len(hits)} совпадение(й)")
    for i, va, disp, tgt in hits:
        print(f"  file 0x{i:X}  VA(anchor) 0x{va:X}  disp {disp:+d}  -> target VA 0x{tgt:X}")
    print("  сравнение с живым логом rev33: GameStates = base+0x45EE348 (base=0x7FF712410000)")


def selftest():
    """Цепочка на синтетике find_offsets.build_fake: офлайн, без процесса игры.
    Проверяет (A)-(J) и значения Vital'ов; цепочка вызывается с quiet=True (режим --values)."""
    import find_offsets as fo
    m = fo.build_fake()
    m.base, m.size, m.pid, m.peb = 0x7FF602440000, 0x4AD4265, 0, 0
    m.close = lambda: None
    gs_global = 0xB00000
    anchor_live = m.base + 0x10E961
    global scan_signature
    saved = scan_signature
    scan_signature = lambda mem, base, size: [(anchor_live, gs_global - anchor_live - 4, gs_global)]
    chain(m, dict(OFFS["rev33"]), quiet=True)
    scan_signature = saved


if __name__ == "__main__":
    rev = DEFAULT_REV
    args = sys.argv[1:]
    if "--selftest" in args:
        selftest()
        raise SystemExit(0)
    if "--rev" in args:
        rev = args[args.index("--rev") + 1]
    if "--file" in args:
        offline_file_check()
        raise SystemExit(0)

    o = dict(OFFS[rev])
    kv = {}
    if "--offs" in args:
        # смещения из find_offsets.py: TotalOff=0x34,CurrentOff=0x38,BackPtrOff=0x10,ReservedOff=0x1C
        # HealthOff/ManaOff/ESOff - смещения Total-значений внутри Life; основа Vital = Off - TotalOff.
        for pair in args[args.index("--offs") + 1].split(","):
            k, v = pair.split("=")
            kv[k] = int(v, 16)
    if "--offs-file" in args:
        # смещения, сохранённые find_offsets.py (--save): читать после рестарта без скана
        raw = json.load(open(args[args.index("--offs-file") + 1], encoding="utf-8"))
        for k, v in raw.items():
            if v is None:            # --save не нашёл/не проверял поле — берём дефолт ревизии
                continue
            kv[k] = v if isinstance(v, int) else int(str(v), 16)
    if kv:
        t = kv.get("TotalOff", o["VitalTotal"])
        o["VitalTotal"] = t
        for k, name in (("HealthOff", "Health"), ("ManaOff", "Mana"),
                        ("ESOff", "EnergyShield"), ("WardOff", "Ward"),
                        ("RageOff", "Rage")):
            if k in kv:
                o[name] = kv[k] - t
        if "CurrentOff" in kv:
            o["VitalCurrent"] = kv["CurrentOff"]
        if "BackPtrOff" in kv:
            o["VitalBackPtr"] = kv["BackPtrOff"]
        if "ReservedOff" in kv:
            o["ReservedPercent"] = kv["ReservedOff"]
        # смещения цепочки передаются своими именами (найдены живым сканом / selftest)
        for key in ("CurrentStatePtr", "States", "TOTAL", "AreaInstanceData", "LocalPlayers",
                    "EntityId", "EntityIsValid", "EntityDetailsName", "EntityDetailsLookup",
                    "CLU_Bucket", "ReservedFlat"):
            if key in kv:
                o[key] = kv[key]

    quiet = "--values" in args
    live = "--live" in args
    fps = 2.0
    if "--fps" in args:
        fps = float(args[args.index("--fps") + 1])
        if fps <= 0:
            raise SystemExit("--fps должен быть > 0")
    if live:
        quiet = True
    pid = None
    if "--pid" in args:
        pid = int(args[args.index("--pid") + 1])
    if live:
        # статус-машина: ожидание процесса/логина (сон 5 с), FAST-темп по --fps, ребут при смерти процесса
        live_run(o, fps, pid)
        raise SystemExit(0)
    if pid is None:
        pids = find_pid(TARGET_NAMES)
        if not pids:
            print("процесс игры не найден — запустите PathOfExile.exe")
            raise SystemExit(2)
        pid = pids[0][1]
        if not quiet:
            print("найдены процессы:", pids, "| смещения:", rev)

    mem = Mem(pid)
    if not quiet:
        print(f"PID {pid} PEB 0x{mem.peb:X} ImageBase 0x{mem.base:X} SizeOfImage 0x{mem.size:X}")
    chain(mem, o, quiet)
    mem.close()
