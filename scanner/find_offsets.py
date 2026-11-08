"""Поиск смещений по известным значениям (макс HP / Mana / ES) в памяти процесса.

Скрипт получает известные максимумы текущего персонажа, сканирует память процесса на точные
совпадения int32, проверяет гипотезу о структуре Vital/Life и выдаёт найденные смещения:
смещение Total внутри Vital, смещения Health/Mana/EnergyShield внутри Life-компонента,
адреса Life -> Entity (через ComponentListPtr) и AreaInstance (через LocalPlayers).

    python find_offsets.py --hp 5000 --mana 3000 --es 1200 [--current-hp 4000 --current-mana 2000 --current-es 800]
    python find_offsets.py --pid 11436 ...
    python find_offsets.py --selftest        # проверка алгоритма на синтетической памяти (rev33 layout)

AreaInstance для поиска LocalPlayers определяется автоматически (сигнатура → GameStates* → States[4]).

Запись в память процесса не выполняется.
"""
import argparse
import bisect
import ctypes
import json
import re
import struct
import sys
import time
from collections import defaultdict
from ctypes import wintypes

# ---------------------------------------------------------------- Win32
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
             "C:/Users/<user>/AppData/Local/Programs/Python/Python312/python.exe find_offsets.py ...")

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")     # кириллица в консоли Windows
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")     # SystemExit/argparse пишут в stderr

# Import AFTER the re-exec guard (the pattern of find_vital.py): importing poe_scan before
# the guard fires poe_scan's own module-level guard, which os.execv's the interpreter onto
# poe_scan.py with find_offsets' argv — find_offsets.py never runs under cygwin python.
import poe_scan as ps

PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
MEM_COMMIT = 0x1000
PAGE_READABLE = {0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}  # RO/RW/RC/EXEC*

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p),
                ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wintypes.DWORD),
                ("RegionSize", ctypes.c_size_t),
                ("State", wintypes.DWORD),
                ("Protect", wintypes.DWORD),
                ("Type", wintypes.DWORD)]


psapi = ctypes.WinDLL("psapi", use_last_error=True)
ntdll = ctypes.WinDLL("ntdll", use_last_error=True)


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
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                       ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.VirtualQueryEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
                                    ctypes.POINTER(MEMORY_BASIC_INFORMATION), ctypes.c_size_t]
kernel32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                ctypes.POINTER(wintypes.DWORD)]
psapi.EnumProcesses.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
ntdll.NtQueryInformationProcess.argtypes = [wintypes.HANDLE, ctypes.c_int,
                                            ctypes.POINTER(PROCESS_BASIC_INFORMATION),
                                            ctypes.c_uint, ctypes.POINTER(ctypes.c_uint)]

TARGET_NAMES = ("PathOfExile.exe", "PathOfExile_x64.exe", "PathOfExileSteam.exe", "PathOfExile_KG.exe")


def find_pid(names=TARGET_NAMES):
    """PID игры по имени exe. Toolhelp в этой среде не работает, поэтому EnumProcesses + QueryFullProcessImageNameW."""
    arr = (wintypes.DWORD * 8192)()
    need = wintypes.DWORD()
    if not psapi.EnumProcesses(ctypes.byref(arr), ctypes.sizeof(arr), ctypes.byref(need)):
        return []
    lowered = [n.lower() for n in names]
    out = []
    for pid in arr[: need.value // 4]:
        h = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(1024)
        size = wintypes.DWORD(1024)
        if kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
            exe = buf.value.rsplit("\\", 1)[-1].lower()
            if exe in lowered:
                out.append((buf.value, pid))
        kernel32.CloseHandle(h)
    return out


class LiveMem:
    """Реальный процесс: чтение + перечисление committed-регионов."""

    def __init__(self, pid):
        self.pid = pid
        self.h = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
        if not self.h:
            raise OSError(f"OpenProcess(READ) не удался, err={ctypes.get_last_error()}")
        # base/size главного модуля из PEB: vtable-указатели в живом процессе лежат внутри этого диапазона
        # NtQueryInformationProcess возвращает NTSTATUS: 0 = успех (ctypes даёт int, поэтому проверка `st == 0`)
        info = PROCESS_BASIC_INFORMATION()
        got = ctypes.c_uint(0)
        self.peb = 0
        self.base = 0
        self.size = 0
        st = ntdll.NtQueryInformationProcess(self.h, 0, ctypes.byref(info), ctypes.sizeof(info),
                                             ctypes.byref(got))
        if st == 0 and info.PebBaseAddress:
            self.peb = info.PebBaseAddress
            d = self._read_raw(self.peb + 0x10, 8)          # PEB.ImageBaseAddress
            self.base = struct.unpack("<Q", d)[0] if d else 0
            d = self._read_raw(self.base + 0x3C, 4)         # e_lfanew
            pe = struct.unpack("<I", d)[0] if d else 0
            d = self._read_raw(self.base + pe + 0x18 + 0x40, 4)   # OptionalHeader(PE32+).SizeOfImage
            self.size = struct.unpack("<I", d)[0] if d else 0
        self.image = (self.base, self.base + self.size) if self.base and self.size else None

    def _read_raw(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t(0)
        if not kernel32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
            return None
        return buf.raw[:got.value]

    def regions(self, max_region=1 << 30):
        out, addr = [], 0
        mbi = MEMORY_BASIC_INFORMATION()
        for _ in range(200000):
            if not kernel32.VirtualQueryEx(self.h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
                break
            base = mbi.BaseAddress or 0
            size = mbi.RegionSize
            if mbi.State == MEM_COMMIT and (mbi.Protect & 0xFF) in PAGE_READABLE:
                if size <= max_region:
                    out.append((base, size))
            addr = base + size
            if addr <= 0:
                break
        self._index = RegionIndex(out)
        return out

    def read(self, addr, n):
        """Чтение, ограниченное границей committed-региона (целое чтение через границу падает)."""
        if not addr:
            return None
        r = getattr(self, "_index", None)
        if r:
            reg = r.of(addr)
            if reg is None:
                return None
            base, end = reg
            n = min(n, end - addr)
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

    def rd_u8(self, addr):
        d = self.read(addr, 1)
        return d[0] if d else None

    def close(self):
        kernel32.CloseHandle(self.h)


class RegionIndex:
    """Индекс committed-регионов: поиск региона по адресу за O(log n) (bisect)."""

    def __init__(self, regions):
        self.regions = sorted(regions)
        self.starts = [b for b, _ in self.regions]

    def of(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0:
            return None
        base, size = self.regions[i]
        if addr >= base + size:
            return None
        return base, base + size


class Windows:
    """Кэш окон памяти: чтение один раз, дальше значение по адресу берём из буфера (без syscall)."""

    def __init__(self, mem, regions):
        self.mem = mem
        self.index = RegionIndex(regions)
        self.cache = {}

    def window(self, lo, hi):
        reg = self.index.of(lo)
        if reg is None:
            return None, 0
        base, end = reg
        lo2, hi2 = max(base, lo), min(end, hi)
        key = (lo2, hi2)
        buf = self.cache.get(key)
        if buf is None:
            buf = self.mem.read(lo2, hi2 - lo2)
            self.cache[key] = buf
        return buf, lo2

    def u64(self, buf, lo, addr):
        off = addr - lo
        return struct.unpack_from("<Q", buf, off)[0] if 0 <= off <= len(buf) - 8 else 0

    def u32(self, buf, lo, addr):
        off = addr - lo
        return struct.unpack_from("<I", buf, off)[0] if 0 <= off <= len(buf) - 4 else None

    def u64x(self, buf, lo, addr):
        """Значение из буфера; если адрес за окном — одно чтение процесса."""
        off = addr - lo
        if 0 <= off <= len(buf) - 8:
            return struct.unpack_from("<Q", buf, off)[0]
        return rd_u64(self.mem, addr)



class FakeMem:
    """Синтетическая память для самопроверки: dict(адрес -> bytes), чтение через индекс регионов."""

    def __init__(self, blobs, base=0x10000000):
        self.blobs = blobs
        self.base = base
        self.index = RegionIndex(sorted((b, len(d)) for b, d in blobs.items()))

    def read(self, addr, n):
        reg = self.index.of(addr)
        if reg is None:
            return None
        base, end = reg
        n = min(n, end - addr)
        return self.blobs[base][addr - base:addr - base + n]

    def rd_u64(self, addr):
        d = self.read(addr, 8)
        return struct.unpack_from("<Q", d)[0] if d and len(d) == 8 else None

    def rd_u32(self, addr):
        d = self.read(addr, 4)
        return struct.unpack_from("<I", d)[0] if d and len(d) == 4 else None

    def rd_u8(self, addr):
        d = self.read(addr, 1)
        return d[0] if d else None

    def rd_u16(self, addr):
        d = self.read(addr, 2)
        return struct.unpack_from("<H", d)[0] if d and len(d) == 2 else None

    def regions(self, max_region=1 << 30):
        return [(b, len(d)) for b, d in sorted(self.blobs.items())]


# ---------------------------------------------------------------- примитивы
def scan_value(mem, value, regions, chunk=1 << 20, progress=None):
    """Все адреса, где int32 == value (little-endian, все выравнивания). Один проход по памяти."""
    needle = struct.pack("<I", value)
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
                    found.append(base + pos + i)
                    i = buf.find(needle, i + 1)
            pos += n
            seen += n
            if progress and seen >= progress[0]:
                print(f"  скан значений: {seen // (1 << 20)} МБ, совпадений {len(found)}", flush=True)
                progress[0] = seen + (1 << 28)
    return found


def rd_u64(mem, addr):
    d = mem.read(addr, 8)
    return struct.unpack_from("<Q", d)[0] if d and len(d) == 8 else None


# ---------------------------------------------------------------- Vital / Life
TOTAL_OFFSETS = range(0x20, 0x8C, 4)      # Total(int32) Vital: диапазон гипотез (смещение плавает по патчам)
VITAL_SPAN = 0x600                        # окно поиска Mana/ES внутри Life-компонента

BACKPTR_OFFSETS = (0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x20, 0x28)
RESERVED_OFFSETS = (0x10, 0x14, 0x18, 0x1C, 0x20, 0x24)

# Якорь, найденный живым сканом (rev33/rev34): Health Total@+0x1DC внутри Life, шаг Vital'ов,
# TotalOff, back-pointer. ReservedOff — ReservedPercent (Flat лежит на -4 от него).
ANCHOR = dict(HealthOff=0x1DC, TotalOff=0x34, BackPtrOff=0x10, ManaGap=0x58, EsGap=0x40, ReservedOff=0x1C)


def parse_anchor(text):
    """Строка `HealthOff=0x1DC,TotalOff=0x34,BackPtrOff=0x10,ManaGap=0x58,EsGap=0x40` -> dict.

    Ключи: HealthOff (Total Health внутри Life), TotalOff (Total внутри Vital), BackPtrOff (back-pointer
    Vital'а на Life), ManaGap (Mana-основа минус Health-основа), EsGap (ES-основа минус Mana-основа),
    ReservedOff (ReservedPercent/Flat внутри Vital). Пропущенные ключи берутся из ANCHOR.
    """
    a = dict(ANCHOR)
    for pair in text.split(","):
        if not pair.strip():
            continue
        k, v = pair.split("=")
        a[k.strip()] = int(v, 0)
    return a


def anchor_ranges(anchor, tol=8):
    """Диапазоны гипотез вокруг якоря: всё, что отличается на tol байт (кратное 4)."""
    t = anchor["TotalOff"]
    b = anchor["BackPtrOff"]
    return (range(t - tol, t + tol + 1, 4),
            [x for x in range(b - tol, b + tol + 1, 4) if x > 0],
            range(anchor["ManaGap"] - tol, anchor["ManaGap"] + tol + 1, 4),
            range(anchor["EsGap"] - tol, anchor["EsGap"] + tol + 1, 4),
            [anchor["ReservedOff"]] + [p for p in RESERVED_OFFSETS if p != anchor["ReservedOff"]])


def discover(mem, hp, mana, es, cur_hp=None, regions=None, progress=None, image=None, wide=False,
            anchor=None, tol=8, min_score=0):
    """Известные максимумы -> раскладка Vital/Life (смещения находятся, а не предполагаются).

    Якорь: `HealthOff` (Total Health внутри Life), `TotalOff`, `BackPtrOff`, `ManaGap` (Health->Mana),
    `EsGap` (Mana->ES), `ReservedOff`. По умолчанию перебор свёрнут к ANCHOR (найдено живым сканом)
    с допуском `tol` байт — сдвинутые семейства не порождаются. `wide=True` снимает якорь: полный
    перебор `TotalOff 0x20..0x8C` x `BackPtrOff` (для ревизий с другой раскладкой).
    `image=(base, base+size)` — главный модуль из PEB: vtable Vital'а обязана попасть в этот диапазон.
    """
    regions = regions or mem.regions()
    hp_hits = scan_value(mem, hp, regions, progress=progress)
    print(f"совпадений int32 == {hp} (max HP): {len(hp_hits)}")

    if wide:
        t_range, b_range, mana_gap, es_gap, po_order = TOTAL_OFFSETS, BACKPTR_OFFSETS, None, None, RESERVED_OFFSETS
        health_center = None
    else:
        anchor = anchor or ANCHOR
        t_range, b_range, mana_gap, es_gap, po_order = anchor_ranges(anchor, tol)
        health_center = anchor["HealthOff"]
        print(f"якорь: HealthOff 0x{anchor['HealthOff']:X}, TotalOff 0x{anchor['TotalOff']:X}, "
              f"BackPtrOff 0x{anchor['BackPtrOff']:X}, ManaGap 0x{anchor['ManaGap']:X}, "
              f"EsGap 0x{anchor['EsGap']:X} (допуск {tol} байт)")

    win = Windows(mem, regions)
    needle_m, needle_e = struct.pack("<I", mana), struct.pack("<I", es)
    needle_c = struct.pack("<I", cur_hp) if cur_hp is not None else None
    results, ok_windows = [], 0

    def in_image(v):
        return image is not None and image[0] <= v < image[1]

    for a in hp_hits:
        buf, lo = win.window(a - 0x100, a + VITAL_SPAN + 0x100)
        if not buf:
            continue
        ok_windows += 1
        mpos = [lo + m.start() for m in re.finditer(needle_m, buf)]
        epos = [lo + m.start() for m in re.finditer(needle_e, buf)]
        cpos = [lo + m.start() for m in re.finditer(needle_c, buf)] if needle_c else []
        if not mpos or not epos:
            continue
        for t in t_range:
            vbh = a - t
            vh = win.u64(buf, lo, vbh)
            for b in b_range:
                life = win.u64(buf, lo, vbh + b)
                if not life:
                    continue
                health_off = a - life
                if not (0 < health_off < VITAL_SPAN):
                    continue
                if health_center is not None and abs(health_off - health_center) > tol:
                    continue
                # группировка по значению back-pointer: O(|mpos| + |epos|), квадратного перебора нет
                mgroup, egroup = defaultdict(list), defaultdict(list)
                for mo in mpos:
                    vbm = mo - t
                    if health_off < vbm - life < VITAL_SPAN + 0x200:
                        mgroup[win.u64(buf, lo, vbm + b)].append(vbm)
                for eo in epos:
                    vbe = eo - t
                    if health_off < vbe - life < VITAL_SPAN + 0x400:
                        egroup[win.u64(buf, lo, vbe + b)].append(vbe)
                if life not in mgroup or life not in egroup:
                    continue
                for vbm in mgroup[life]:
                    mana_off = vbm - life
                    vm = win.u64(buf, lo, vbm)
                    if mana_gap is not None and (vbm - vbh) not in mana_gap:
                        continue
                    for vbe in egroup[life]:
                        es_off = vbe - life
                        if es_off <= mana_off:
                            continue
                        if es_gap is not None and (vbe - vbm) not in es_gap:
                            continue
                        ve = win.u64(buf, lo, vbe)
                        score, pct_off = 0, None
                        for po in po_order:
                            pv = win.u32(buf, lo, vbh + po)
                            pf = win.u32(buf, lo, vbh + po - 4)      # ReservedFlat стоит перед Percent
                            # 0 — не доказательство, а stat-id (напр. 1023 @+0x14) совпадает
                            # у всех Vital'ов: проверяем пару Flat/Percent и её диапазоны
                            if (pv is None or pf is None or not 0 < pv <= 10000 or not 0 <= pf <= 10000
                                    or win.u32(buf, lo, vbm + po) != pv
                                    or win.u32(buf, lo, vbm + po - 4) != pf):
                                continue
                            pct_off = po
                            score += 1
                            if not wide and po == anchor["ReservedOff"]:
                                score += 1              # Reserved точно по якорю
                            break
                        for vt in (vh, vm, ve):
                            if in_image(vt):
                                score += 1                  # vtable Vital'а внутри главного модуля
                        # stat-id поля Vital'а (0x08/0x0C/0x20/0x24/0x28) — маленькие int:
                        # у сдвинутых семейств на этих местах указатели/мусор
                        ids = [win.u32(buf, lo, vbh + k) for k in (0x08, 0x0C, 0x20, 0x24, 0x28)]
                        if all(v is not None and v <= 4096 for v in ids):
                            score += 1
                        # основы Vital'ов в объекте Life кратны 8: первое поле структуры —
                        # vtable-указатель, выравнивание объекта 8 байт. Сдвинутые семьи
                        # дают немодульные основы (0x1AC/0x1A4 из живого прогона rev33).
                        for base_off in (health_off - t, mana_off, es_off):
                            if base_off % 8 == 0:
                                score += 1
                        cur = None
                        if needle_c:
                            co = [c - vbh for c in cpos if t < c - vbh <= 0x50]
                            if co:
                                cur = min(co)
                                score += 2              # Current найден и он <= Total
                        if in_image(win.u64x(buf, lo, life)) and win.u64x(buf, lo, life + 0x08):
                            score += 1                  # заголовок Life: vtable + ptr на Entity
                        results.append(dict(TotalOff=t, BackPtrOff=b, Life=life, VitalBase=vbh,
                                            HealthOff=health_off, ManaOff=mana_off, ESOff=es_off,
                                            CurrentOff=cur, ReservedOff=pct_off, Score=score))

    print(f"окон успешно прочитано: {ok_windows} из {len(hp_hits)} (кэш чтения: {len(win.cache)})")
    uniq, seen = [], set()
    for r in results:
        key = (r["TotalOff"], r["BackPtrOff"], r["Life"], r["HealthOff"], r["ManaOff"], r["ESOff"])
        if key not in seen:
            seen.add(key)
            uniq.append(r)
    uniq.sort(key=lambda r: -r["Score"])

    # Группировка по раскладу: только смещения, адрес Life не входит в сигнатуру.
    # Один расклад, найденный на нескольких Life-объектах (монстры/соседи с теми же
    # значениями HP/Mana/ES), схлопывается в одну строку; сдвинутые семьи остаются
    # отдельными строками с более низким скором.
    layouts = {}
    for r in uniq:
        sig = (r["HealthOff"], r["ManaOff"], r["ESOff"], r["TotalOff"],
               r["CurrentOff"], r["BackPtrOff"], r["ReservedOff"])
        d = layouts.setdefault(sig, {"Score": r["Score"], "lives": []})
        d["Score"] = max(d["Score"], r["Score"])
        d["lives"].append(r["Life"])
    ordered = sorted(layouts.items(), key=lambda kv: -kv[1]["Score"])
    print(f"кандидатов Vital/Life-раскладки: {len(uniq)}  уникальных раскладов: {len(ordered)}")
    for sig, d in [kv for kv in ordered if kv[1]["Score"] >= min_score][:12]:
        cur = f"+0x{sig[4]:X}" if sig[4] is not None else "—"
        pct = f"+0x{sig[6]:X}" if sig[6] is not None else "—"
        lives = ", ".join("0x%X" % a for a in d["lives"][:3])
        # HealthOff - позиция Total-значения внутри Life; ManaOff/ESOff - основы Vital'ов
        print(f"  [score {d['Score']}] Health Vital +0x{sig[0] - sig[3]:X} (Total@+0x{sig[0]:X}), "
              f"Mana Vital +0x{sig[1]:X}, ES Vital +0x{sig[2]:X} | "
              f"внутри Vital: Total@+0x{sig[3]:X}, Current{cur}, back-pointer@+0x{sig[5]:X}, "
              f"Reserved{pct} | Life-объектов {len(d['lives'])}: {lives}")
    if ordered:
        sig, d = ordered[0]
        # poe_scan.py --offs ожидает ПОЗиЦИИ Total-значений внутри Life: основа Vital = Off - TotalOff
        offs = f"HealthOff=0x{sig[0]:X},TotalOff=0x{sig[3]:X}," \
               f"ManaOff=0x{sig[1] + sig[3]:X},ESOff=0x{sig[2] + sig[3]:X}"
        if sig[4] is not None:
            offs += f",CurrentOff=0x{sig[4]:X}"
        else:
            print("  CurrentOff не найден — передай --current-hp (текущее HP, не полное)")
        gap = ordered[1][1]["Score"] - d["Score"] if len(ordered) > 1 else None
        tag = "расклад подтверждён" if (gap is None and d["Score"] >= 8) or \
            (gap is not None and d["Score"] - gap >= 2) else "нужна проверка"
        print(f"  [{tag}] python3 poe_scan.py --offs {offs}")
    return uniq


# ---------------------------------------------------------------- backtrace
def scan_pointers(mem, regions, values, chunk=1 << 20, progress=None):
    """Один стриминг-проход по регионам: где u64 == любое значение из `values`.

    Сложность O(объём памяти), а не O(объём x число кандидатов): все значения ищутся одной
    regex-альтернативой. Чанки перекрываются на 8 байт, чтобы не терждать совпадений на границе.
    Возвращает dict(значение -> список адресов).
    """
    vals = [v for v in values if v]
    if not vals:
        return {}
    pattern = re.compile(b"|".join(re.escape(struct.pack("<Q", v)) for v in vals))
    hits = defaultdict(list)
    seen = 0
    for base, size in regions:
        pos = 0
        while pos < size:
            n = min(chunk, size - pos)
            buf = mem.read(base + pos, n)
            if buf:
                for m in pattern.finditer(buf):
                    hits[int.from_bytes(m.group(), "little")].append(base + pos + m.start())
            pos += max(n - 8, 1)
            seen += n
            if progress and seen >= progress[0]:
                print(f"  скан указателей: {seen // (1 << 20)} МБ, найдено {sum(len(v) for v in hits.values())}",
                      flush=True)
                progress[0] = seen + (1 << 28)
    return hits


def find_local_players(mem, area, entity, span=0x1000, max_count=64):
    """Смещение LocalPlayers внутри AreaInstance: std::vector, массив которого содержит Entity.

    Object-размер AreaInstance читается одним блоком; перебор 8-выровненных троек {First,Last,End}
    и проверка наличия Entity в массиве: O(span x max_count) чтений, без скана памяти.
    Возвращает список (смещение_вектора, количество, индекс_элемента).
    """
    buf = mem.read(area, span)
    out = []
    if not buf:
        return out
    needle = struct.pack("<Q", entity)
    for off in range(0, len(buf) - 24, 8):
        first, last, end = struct.unpack_from("<3Q", buf, off)
        if first == 0 or last <= first or (last - first) % 8:
            continue
        count = (last - first) // 8
        if count > max_count:
            continue
        arr = mem.read(first, count * 8)
        if arr and needle in arr:
            out.append((off, count, arr.index(needle) // 8))
    return out


class ProcIndex:
    """Случайный доступ к значениям процесса (u64/u32/u8) через чтение с клиппингом по регионам."""

    def __init__(self, mem, regions):
        self.mem = mem
        self.index = RegionIndex(regions)

    def u64(self, addr):
        d = self.mem.read(addr, 8)
        return struct.unpack_from("<Q", d)[0] if d and len(d) == 8 else None

    def u32(self, addr):
        d = self.mem.read(addr, 4)
        return struct.unpack_from("<I", d)[0] if d and len(d) == 4 else None

    def u8(self, addr):
        d = self.mem.read(addr, 1)
        return d[0] if d else None


def find_vector_at(cidx, first_hits, site, max_index=50, max_count=512):
    """std::vector {First,Last,End}, элементом которого является `site`.

    `first_hits` — результат одного скана: значение указателя -> адреса, где оно лежит.
    Для гипотезы First = site - 8*k (k = индекс элемента) проверяем тройку: O(max_index).
    Возвращает список (адрес_вектора, индекс_элемента, количество).
    """
    out = []
    for k in range(max_index + 1):
        first = site - 8 * k
        for p in first_hits.get(first, []):
            last, end = cidx.u64(p + 8), cidx.u64(p + 16)
            if last is None or end is None or last <= first or end < last:
                continue
            if (last - first) % 8:
                continue
            count = (last - first) // 8
            if count > max_count:
                continue
            out.append((p, (site - first) // 8, count))
    return out


def scan_range_u64(mem, regions, lo, hi, chunk=1 << 20, progress=None):
    """Все u64-значения в диапазоне [lo, hi]: один `bytes.find` по общему старшему префиксу адресов.

    Все адреса внутри диапазона имеют одинаковые старшие 4 байта, поэтому перебор игл не нужен:
    находим префикс, проверяем суффикс (значение) прямо в буфере чанка. Сложность O(объём памяти).
    Возвращает dict(значение -> список адресов, где оно лежит).
    """
    vals = defaultdict(list)
    prefixes = {struct.pack("<Q", lo)[4:], struct.pack("<Q", hi)[4:]}
    for base, size in regions:
        pos = 0
        while pos < size:
            n = min(chunk, size - pos)
            buf = mem.read(base + pos, n)
            if not buf:
                pos += n
                continue
            for pfx in prefixes:
                i = buf.find(pfx)
                while i >= 0:
                    if i >= 4:
                        val = struct.unpack_from("<Q", buf, i - 4)[0]
                        if lo <= val <= hi:
                            vals[val].append(base + pos + i - 4)
                    i = buf.find(pfx, i + 1)
            pos += max(n - 8, 1)
            if progress and pos and pos + base >= progress[0]:
                print(f"  скан диапазона: {(base + pos) // (1 << 20)} МБ, значений {len(vals)}", flush=True)
                progress[0] = base + pos + (1 << 28)
    return vals


def collect_first_hits(mem, regions, sites, max_index=50, progress=None):
    """Гипотезы First std::vector: site - 8*k (k = индекс элемента), где k <= max_index.

    Группируем sites по старшим 4 байтам адреса (все они в одном 4-ГБ блоке) и на каждую группу
    делаем один проход сканом диапазона: O(объём памяти x число групп), а не regex из |sites|x|k| игл.
    """
    if not sites:
        return {}
    groups = defaultdict(list)
    for s in sites:
        groups[s >> 32].append(s)
    out = {}
    for ss in groups.values():
        out.update(scan_range_u64(mem, regions, min(ss) - 8 * max_index, max(ss), progress=progress))
    return out


def find_component_list(cidx, life_hits, first_hits, life, entity_off=0x10, valid_byte=0x8C, id_off=0x88):
    """Life-адрес -> ComponentListPtr std::vector -> Entity (вектор лежит в Entity+entity_off)."""
    out = []
    for site in life_hits.get(life, []):
        for vec, idx, count in find_vector_at(cidx, first_hits, site, max_count=50):
            entity = vec - entity_off
            valid = cidx.u8(entity + valid_byte)
            out.append(dict(array=site, vector=vec, index=idx, count=count, Entity=entity,
                            IsValid=valid, Id=cidx.u32(entity + id_off)))
    return out


def find_area_instance(cidx, entity_hits, first_hits, entity):
    """Entity-адрес -> LocalPlayers std::vector; смещение вектора внутри AreaInstance."""
    out = []
    for site in entity_hits.get(entity, []):
        for vec, idx, count in find_vector_at(cidx, first_hits, site, max_count=50):
            out.append(dict(vector=vec, index=idx, count=count))
    return out


def confirm_direct(mem, cands, entity_valid=0x8C, entity_id=0x88, comp_vector=0x10):
    """Быстрое подтверждение без скана кучи: Life+0x08 -> Entity (владельца компонента),
    IsValid == 0xC, компонентный вектор Entity содержит Life. 4-5 чтений на кандидата
    вместо скана 16 ГБ. Смещения Entity - гипотезы rev33, проверяются значениями.
    """
    out = []
    for r in cands:
        ent = mem.rd_u64(r["Life"] + 0x08)
        if not ent:
            continue
        b = mem.read(ent + entity_valid, 1)
        if not b or b[0] != 0xC:
            continue
        vec = mem.read(ent + comp_vector, 24)
        if not vec:
            continue
        first, last, end = struct.unpack_from("<3Q", vec)
        count = (last - first) // 8
        if not (first and 0 < count <= 64 and last <= end):
            continue
        arr = mem.read(first, count * 8)
        if not arr:
            continue
        pk = struct.pack("<Q", r["Life"])
        if pk not in arr:
            continue
        r["Entity"], r["CompIndex"], r["CompCount"], r["Id"] = \
            ent, arr.index(pk) // 8, count, mem.rd_u32(ent + entity_id)
        out.append(r)
    return out


def confirm(cidx, life_hits, first_hits, cands):
    """Подтверждение кандидата: от Life-адреса должен находиться Entity с IsValid == 0xC."""
    out = []
    for r in cands:
        comp = [c for c in find_component_list(cidx, life_hits, first_hits, r["Life"]) if c["IsValid"] == 0xC]
        if comp:
            c = comp[0]
            r["Entity"], r["CompIndex"], r["CompCount"], r["Id"] = \
                c["Entity"], c["index"], c["count"], c["Id"]
            out.append(r)
    return out



# ---------------------------------------------------------------- selftest
def build_fake():
    """Синтетическая память с layout rev33: AreaInstance -> LocalPlayers -> Entity -> Life -> Vital."""
    base = 0x10000000
    area = 0x200000
    ent = 0x400000
    details = 0x500000
    life = 0x600000
    lookup = 0x800000
    players_array = 0x300000          # куча: массив указателей на локальных игроков
    comp_array = 0x310000             # куча: массив указателей на компоненты Entity
    bucket_array = 0x900000
    str_life = 0xA00000
    str_pos = 0xA00100

    HP, MP, ES = 5000, 3000, 1200
    CH, CM, CE = 4000, 2000, 800

    def vital(total, current, flat=100, pct=2023):
        b = bytearray(0x40)
        struct.pack_into("<Q", b, 0x00, 0x7FF716000000)      # vtable
        struct.pack_into("<I", b, 0x08, 1)
        struct.pack_into("<I", b, 0x0C, 2)
        struct.pack_into("<Q", b, 0x10, life)                # LifeComponentPtr
        struct.pack_into("<i", b, 0x18, flat)
        struct.pack_into("<i", b, 0x1C, pct)
        struct.pack_into("<I", b, 0x20, 7)
        struct.pack_into("<I", b, 0x34, total)
        struct.pack_into("<I", b, 0x38, current)
        return bytes(b)

    blobs = {}
    # Life component: Header + Health@0x1A8, Mana@0x200, ES@0x240
    comp = bytearray(0x500)
    struct.pack_into("<Q", comp, 0x00, 0x7FF716001000)
    struct.pack_into("<Q", comp, 0x08, ent)                  # EntityPtr
    comp[0x1A8:0x1A8 + 0x40] = vital(HP, CH)
    comp[0x200:0x200 + 0x40] = vital(MP, CM)
    comp[0x240:0x240 + 0x40] = vital(ES, CE)
    blobs[life] = bytes(comp)

    # Entity: ItemStruct {VTable, EntityDetailsPtr, ComponentListPtr vector @ +0x10}
    e = bytearray(0x90)
    struct.pack_into("<Q", e, 0x00, 0x7FF716002000)
    struct.pack_into("<Q", e, 0x08, details)
    struct.pack_into("<Q", e, 0x10, comp_array)              # First
    struct.pack_into("<Q", e, 0x18, comp_array + 16)         # Last
    struct.pack_into("<Q", e, 0x20, comp_array + 16)         # End
    struct.pack_into("<I", e, 0x88, 123)
    e[0x8C] = 0xC
    blobs[ent] = bytes(e)

    blobs[comp_array] = struct.pack("<2Q", life, 0x700000)

    # EntityDetails: name (SSO wstring) + ComponentLookUpPtr@0x28
    d = bytearray(0x30)
    name = "DexFourb"                       # SSO: wstring в буфере объекта (capacity <= 8)
    # layout: buf_ptr(0x08)+reserved(0x10) = inline data, length(0x18), pad(0x1C), capacity(0x20)
    struct.pack_into("<Q", d, 0x08, int.from_bytes(name[:4].encode("utf-16le"), "little"))
    struct.pack_into("<Q", d, 0x10, int.from_bytes(name[4:8].encode("utf-16le"), "little"))
    struct.pack_into("<I", d, 0x18, len(name))
    struct.pack_into("<I", d, 0x20, 8)
    struct.pack_into("<Q", d, 0x28, lookup)
    blobs[details] = bytes(d)

    # ComponentLookUp: bucket @+0x28 {Data{First,Last,End}, UnknownPtr, Capacity}
    l = bytearray(0x60)
    struct.pack_into("<Q", l, 0x28, bucket_array)
    struct.pack_into("<Q", l, 0x30, bucket_array + 32)
    struct.pack_into("<Q", l, 0x38, bucket_array + 32)
    struct.pack_into("<Q", l, 0x40, 0)                       # UnknownPtr
    struct.pack_into("<I", l, 0x48, 2)                       # Capacity
    blobs[lookup] = bytes(l)

    blobs[bucket_array] = struct.pack("<QII", str_life, 0, 0) + struct.pack("<QII", str_pos, 1, 0)
    blobs[str_life] = b"Life\0"
    blobs[str_pos] = b"Position\0"

    # AreaInstance: LocalPlayers std::vector @ +0x5B8 -> [Entity]
    a = bytearray(0x600)
    struct.pack_into("<Q", a, 0x00, 0x7FF716003000)
    struct.pack_into("<Q", a, 0x5B8, players_array)
    struct.pack_into("<Q", a, 0x5B8 + 8, players_array + 8)
    struct.pack_into("<Q", a, 0x5B8 + 16, players_array + 8)
    blobs[area] = bytes(a)

    blobs[players_array] = struct.pack("<Q", ent)

    # GameState: CurrentStatePtr vector @+0x10, States 13x16 @+0x50, InGameState = States[4]
    gs_global = 0xB00000
    gsobj = 0xC00000
    state_arr = 0xC10000
    ingame = 0xC20000
    g = bytearray(0x130)
    struct.pack_into("<Q", g, 0x10, state_arr)
    struct.pack_into("<Q", g, 0x18, state_arr + 16)
    struct.pack_into("<Q", g, 0x20, state_arr + 16)
    struct.pack_into("<Q", g, 0x50 + 4 * 16, ingame)      # States[4] = InGameState
    blobs[gsobj] = bytes(g)
    blobs[state_arr] = struct.pack("<2Q", ingame, 0)       # First/Last: current = *(Last-0x10)
    ig = bytes(0x290) + struct.pack("<Q", area)            # InGameState+0x290 = AreaInstance
    blobs[ingame] = ig
    blobs[gs_global] = struct.pack("<Q", gsobj)

    return FakeMem(blobs, base)


def selftest():
    mem = build_fake()
    regions = mem.regions()
    # vtable в синтетике лежат в диапазоне 0x7FF716000000..0x7FF716003000: этим имитируем главный модуль
    image = (0x7FF716000000, 0x7FF716004000)
    res = discover(mem, 5000, 3000, 1200, cur_hp=4000, regions=regions, image=image)
    exp = (0x34, 0x1A8, 0x200, 0x240, 0x38)   # Vital-основы Health/Mana/ES, TotalOff, CurrentOff для rev33
    ok = any(r["TotalOff"] == exp[0] and r["HealthOff"] - r["TotalOff"] == exp[1]
             and r["ManaOff"] == exp[2] and r["ESOff"] == exp[3]
             and r["CurrentOff"] == exp[4] for r in res)
    print(f"ожидаемые rev33-смещения найдены: {ok}  "
          f"(Health Vital 0x1A8, Mana Vital 0x200, ES Vital 0x240, Total 0x34, Current 0x38)")

    # тот же поиск с переданным якорем и нулевым допуском (--anchor ... --tol 0)
    strict = discover(mem, 5000, 3000, 1200, cur_hp=4000, regions=regions, image=image,
                      anchor=parse_anchor("HealthOff=0x1DC,TotalOff=0x34,BackPtrOff=0x10,ManaGap=0x58,"
                                          "EsGap=0x40,ReservedOff=0x14"), tol=0)
    ok_strict = any(r["TotalOff"] == exp[0] and r["HealthOff"] - r["TotalOff"] == exp[1]
                    and r["ManaOff"] == exp[2] and r["ESOff"] == exp[3]
                    and r["CurrentOff"] == exp[4] for r in strict)
    print(f"строгий якорь (--tol 0) находит rev33: {ok_strict}, кандидатов {len(strict)}")
    ok = ok and ok_strict

    idx = ProcIndex(mem, regions)
    life_hits = scan_pointers(mem, regions, [r["Life"] for r in res] + [0x600000])
    sites = [s for lst in life_hits.values() for s in lst]
    first_hits = collect_first_hits(mem, regions, sites)
    comp = find_component_list(idx, life_hits, first_hits, 0x600000)
    for c in comp:
        valid = f"0x{c['IsValid']:X}" if c["IsValid"] is not None else "—"
        print(f"  ComponentListPtr vector @0x{c['vector']:X} (Entity+0x{c['vector'] - 0x400000:X}), "
              f"index {c['index']} из {c['count']}, Entity @0x{c['Entity']:X} "
              f"IsValid={valid} Id={c['Id']}")
    conf = confirm(idx, life_hits, first_hits, res)
    print(f"подтверждённых раскладок (найден Entity с IsValid==0xC): {len(conf)}")
    ok = ok and len(conf) > 0

    # быстрый путь: Life+0x08 -> Entity без скана кучи (4-5 чтений на кандидата)
    fast = confirm_direct(mem, [dict(r) for r in res])
    off = find_local_players(mem, 0x200000, 0x400000)
    lp_line = f"LocalPlayers @AreaInstance+0x{off[0][0]:X} count {off[0][1]} index {off[0][2]}" \
        if off else "LocalPlayers не найден"
    print(f"  confirm_direct: подтверждённых {len(fast)}, {lp_line}")
    ok = ok and len(fast) > 0 and bool(off) and off[0][0] == 0x5B8

    # круг --save / --offs-file: записать расклад и прочитать его обратно теми же ключами
    r = fast[0]
    data = {"HealthOff": r["HealthOff"], "TotalOff": r["TotalOff"],
            "ManaOff": r["ManaOff"] + r["TotalOff"], "ESOff": r["ESOff"] + r["TotalOff"],
            "BackPtrOff": r["BackPtrOff"], "ReservedOff": r["ReservedOff"],
            "CurrentOff": r["CurrentOff"], "LocalPlayers": off[0][0]}
    path = "offsets_selftest.json"
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(data, fh)
    back = json.load(open(path, encoding="utf-8"))
    t = back["TotalOff"]
    round_ok = (back["HealthOff"] - t == 0x1A8 and back["ManaOff"] - t == 0x200
                and back["ESOff"] - t == 0x240 and back["CurrentOff"] == 0x38
                and back["LocalPlayers"] == 0x5B8)
    print(f"  круг --save/--offs-file: Health Vital 0x{back['HealthOff'] - t:X}, "
          f"Mana 0x{back['ManaOff'] - t:X}, ES 0x{back['ESOff'] - t:X}, Current 0x{back['CurrentOff']:X}, "
          f"LocalPlayers 0x{back['LocalPlayers']:X} -> {round_ok}")
    ok = ok and round_ok

    ent_hits = scan_pointers(mem, regions, [r["Entity"] for r in conf] + [0x400000])
    ent_sites = [s for lst in ent_hits.values() for s in lst]
    first_hits2 = collect_first_hits(mem, regions, ent_sites)
    area = find_area_instance(idx, ent_hits, first_hits2, 0x400000)
    for v in area:
        print(f"  LocalPlayers vector @0x{v['vector']:X} index {v['index']} count {v['count']} "
              f"(AreaInstance+0x{v['vector'] - 0x200000:X})")
    return ok


def bench(n_mb, hits_per_mb=4):
    """Замер стадий на синтетике объёмом n_mb МБ: время должно расти линейно по объёму памяти."""
    import random
    rnd = random.Random(1234)
    mem = build_fake()
    hp, mp, es, cur = 5000, 3000, 1200, 4000
    tile = bytearray(rnd.randbytes(1 << 20))       # псевдослучайный filler: плотность совпадений как в куче
    pack_hp = struct.pack("<I", hp)
    for off in range(0, 1 << 20, (1 << 20) // hits_per_mb):
        tile[off:off + 4] = pack_hp
    tile = bytes(tile)
    for k in range(n_mb):
        mem.blobs[0x140000000 + k * (1 << 20)] = tile   # реалистичные адреса кучи (> 4 ГБ)
    mem.index = RegionIndex(sorted((b, len(d)) for b, d in mem.blobs.items()))
    regions = mem.regions()
    progress = [1 << 30]                      # прогресс не печатаем: бенчмерка тихая

    t0 = time.perf_counter()
    found = scan_value(mem, hp, regions, progress=progress)
    t1 = time.perf_counter()
    cands = discover(mem, hp, mp, es, cur, regions, progress)
    t2 = time.perf_counter()
    idx = ProcIndex(mem, regions)
    life_hits = scan_pointers(mem, regions, [r["Life"] for r in cands], progress=progress)
    t3 = time.perf_counter()
    first_hits = collect_first_hits(mem, regions, [s for lst in life_hits.values() for s in lst],
                                    progress=progress)
    t4 = time.perf_counter()
    confirmed = confirm(idx, life_hits, first_hits, cands)
    t5 = time.perf_counter()
    print(f"filler {n_mb} МБ, HP-совпадений {len(found)}: scan {t1 - t0:.2f} с, discover {t2 - t1:.2f} с, "
          f"pointers {t3 - t2:.2f} с, first_hits {t4 - t3:.2f} с, confirm {t5 - t4:.2f} с | "
          f"кандидатов {len(cands)}, подтверждённых {len(confirmed)}")


def locate_area(mem, rev=None, verbose=True):
    """AreaInstance для шага LocalPlayers: та же цепочка, что poe_scan.py (A)-(E).

    сигнатура -> статическая GameStates* -> GameState+States[4] -> InGameState+AreaInstanceData.
    Читается ~10 раз, поэтому вызывается после скана значений (адрес зоны не должен быть
    протухшим: вход в зону пересоздаёт AreaInstance).
    """
    import poe_scan as ps
    o = dict(ps.OFFS[rev or ps.DEFAULT_REV])
    if not (mem.base and mem.size):
        if verbose:
            print("  AreaInstance: неизвестен base/SizeOfImage модуля (PEB)")
        return None
    hits = ps.scan_signature(mem, mem.base, mem.size)
    if len(hits) != 1:
        if verbose:
            print(f"  AreaInstance: сигнатура дала {len(hits)} совпадений (нужно ровно 1)")
        return None
    gs = mem.rd_u64(hits[0][2])
    if not gs:
        if verbose:
            print("  AreaInstance: GameStates* == 0 (игра не в игре?)")
        return None
    states = mem.read(gs + o["States"], o["TOTAL"] * 16)
    if not states or len(states) < o["TOTAL"] * 16:
        if verbose:
            print(f"  AreaInstance: чтение States @GameState+0x{o['States']:X} не удалось")
        return None
    ingame = struct.unpack_from("<Q", states, 4 * 16)[0]      # InGameState = States[4].X
    if not ingame:
        if verbose:
            print("  AreaInstance: InGameState == 0 (загрузка/меню)")
        return None
    area = mem.rd_u64(ingame + o["AreaInstanceData"])
    if not area:
        if verbose:
            print(f"  AreaInstance: InGameState+0x{o['AreaInstanceData']:X} == 0")
        return None
    return area


def main():
    ap = argparse.ArgumentParser(description="Поиск смещений по известным максимумам HP/Mana/ES")
    ap.add_argument("--hp", type=int, help="максимальное HP персонажа (Total)")
    ap.add_argument("--mana", type=int, help="максимальная мана (Total)")
    ap.add_argument("--es", type=int, help="максимальный энергетический щит (Total)")
    ap.add_argument("--current-hp", type=int, help="текущее HP, для смещения Current")
    ap.add_argument("--current-mana", type=int)
    ap.add_argument("--current-es", type=int)
    ap.add_argument("--pid", type=int, help="PID процесса (поиск по имени, если не задан)")
    ap.add_argument("--selftest", action="store_true", help="проверка алгоритма на синтетической памяти")
    ap.add_argument("--bench", type=int, metavar="МБ", help="замер стадий на синтетике заданного объёма")
    ap.add_argument("--wide", action="store_true", help="поиск с нуля: полный перебор гипотез, без якоря")
    ap.add_argument("--anchor", metavar="K=0x..,K=0x..",
                    help="передать найденные смещения: HealthOff=0x1DC,TotalOff=0x34,BackPtrOff=0x10,"
                         "ManaGap=0x58,EsGap=0x40,ReservedOff=0x1C (по умолчанию — якорь живого скана)")
    ap.add_argument("--tol", type=int, default=8,
                    help="допуск вокруг якоря в байтах (--tol 0 = искать строго по якорю)")
    ap.add_argument("--min-score", type=int, default=5, metavar="N",
                    help="не печатать расклады со скором ниже N (по умолчанию 5; 0 = печатать все)")
    ap.add_argument("--save", metavar="FILE",
                    help="записать подтверждённый расклад в JSON (файл статичен для билда: "
                         "poe_scan.py --offs-file FILE после рестарта, без скана)")
    args = ap.parse_args()

    if args.bench:
        for mb in (args.bench, args.bench * 2, args.bench * 4):
            bench(mb)
        raise SystemExit(0)

    if args.selftest:
        ok = selftest()
        raise SystemExit(0 if ok else 1)

    if not (args.hp and args.mana and args.es):
        ap.error("нужны --hp, --mana и --es")

    pid = args.pid
    exe_path = None
    if not pid:
        pids = find_pid()
        if not pids:
            print("процесс игры не найден — запустите PathOfExile.exe")
            raise SystemExit(2)
        pid = pids[0][1]
        exe_path = pids[0][0]
        print("найдены процессы:", pids)
    if exe_path is None:
        exe_path = ps.image_path(pid)
    gv_full = ps.game_version(exe_path, pid)
    gv = ps.game_version_short(gv_full)
    print(f"GameVersion {gv} (FileVersion {gv_full})" if gv_full
          else "GameVersion: источник не найден (logs/LatestClient.txt, fallback VERSIONINFO)")

    mem = LiveMem(pid)
    regions = mem.regions()
    total = sum(s for _, s in regions)
    print(f"PID {pid}, committed-регионов {len(regions)}, суммарно {total // (1 << 20)} МБ, "
          f"ImageBase 0x{mem.base:X} SizeOfImage 0x{mem.size:X}")
    progress = [1 << 28]
    anchor = parse_anchor(args.anchor) if args.anchor else None
    cands = discover(mem, args.hp, args.mana, args.es, args.current_hp, regions, progress,
                     image=mem.image, wide=args.wide, anchor=anchor, tol=args.tol,
                     min_score=args.min_score)
    confirmed = confirm_direct(mem, cands)
    if confirmed:
        print(f"подтверждено прямым чтением (Life+0x08 -> Entity, IsValid==0xC): {len(confirmed)}")
        idx = ent_hits = first_hits2 = None       # скан кучи не нужен
    else:
        print("прямое подтверждение не сработало (смещения Entity этого патча другие) - "
              "медленный путь: скан указателей по куче, ~4 прохода по 16 ГБ (~1.5 мин)")
        progress = [1 << 28]                 # сброс порога: после discover он больше общего объёма
        idx = ProcIndex(mem, regions)
        life_hits = scan_pointers(mem, regions, [r["Life"] for r in cands], progress=progress)
        life_sites = [s for lst in life_hits.values() for s in lst]
        first_hits = collect_first_hits(mem, regions, life_sites, progress=progress)
        confirmed = confirm(idx, life_hits, first_hits, cands)
        print(f"подтверждённых раскладок (найден Entity с IsValid==0xC): {len(confirmed)}")
        ent_hits = scan_pointers(mem, regions, [r["Entity"] for r in confirmed], progress=progress)
        ent_sites = [s for lst in ent_hits.values() for s in lst]
        first_hits2 = collect_first_hits(mem, regions, ent_sites, progress=progress)
    # AreaInstance: руками не задаётся — берём из цепочки poe_scan (A)-(E)
    area = locate_area(mem)
    if area:
        print(f"AreaInstance найдена по цепочке: 0x{area:X}")
    else:
        print("AreaInstance не найдена: смещение LocalPlayers не вычислить "
              "(проверь, что персонаж в игре, и что ревизия в poe_scan.py соответствует патчу)")
    # группировка подтверждённых по раскладу (только смещения): несколько Life-объектов
    # одного расклада печатаются одним блоком
    shown = {}
    for r in confirmed:
        sig = (r["HealthOff"], r["ManaOff"], r["ESOff"], r["TotalOff"],
               r["CurrentOff"], r["BackPtrOff"], r["ReservedOff"])
        d = shown.setdefault(sig, {"Score": r["Score"], "rows": []})
        d["Score"] = max(d["Score"], r["Score"])
        d["rows"].append(r)
    lp = []
    for sig, d in sorted(shown.items(), key=lambda kv: -kv[1]["Score"])[:8]:
        r = d["rows"][0]
        cur = f"+0x{r['CurrentOff']:X}" if r["CurrentOff"] is not None else "—"
        pct = f"+0x{r['ReservedOff']:X}" if r["ReservedOff"] is not None else "—"
        print(f"  [score {d['Score']}] Health Vital +0x{r['HealthOff'] - r['TotalOff']:X} "
              f"(Total@+0x{r['HealthOff']:X}), Mana Vital +0x{r['ManaOff']:X}, ES Vital +0x{r['ESOff']:X} | "
              f"внутри Vital: Total@+0x{r['TotalOff']:X}, Current{cur}, back-pointer@+0x{r['BackPtrOff']:X}, "
              f"Reserved{pct} | подтверждённых Life-объектов {len(d['rows'])}")
        for r in d["rows"][:3]:
            print(f"    Life @0x{r['Life']:X} -> Entity @0x{r['Entity']:X} (Id={r['Id']}, компонент Life "
                  f"index {r['CompIndex']} из {r['CompCount']})")
            for v in (find_area_instance(idx, ent_hits, first_hits2, r["Entity"]) if idx else []):
                # смещение = адрес вектора - адрес AreaInstance
                if area and area <= v["vector"]:
                    off = v["vector"] - area
                    lp.append(off)
                    print(f"      LocalPlayers @AreaInstance+0x{off:X} index {v['index']} count {v['count']} | "
                          f"poe_scan.py --offs LocalPlayers=0x{off:X}")
                else:
                    print(f"      LocalPlayers vector @0x{v['vector']:X} index {v['index']} count {v['count']}")
    if area:
        # LocalPlayers — вектор из одного элемента (локальный игрок), поэтому кандидаты
        # ранжируются: сначала index 0, затем меньший count; дубли между Life-объектами схлопываются.
        hits = set()
        for r in confirmed[:8]:
            hits.update(find_local_players(mem, area, r["Entity"]))
        ranked = sorted(hits, key=lambda t: (t[2], t[1], t[0]))
        for off, count, idx_p in ranked:
            print(f"    LocalPlayers @AreaInstance+0x{off:X}: count {count}, index {idx_p} | "
                  f"poe_scan.py --offs LocalPlayers=0x{off:X}")
        if ranked:
            lp = [off for off, _, _ in ranked]      # первый — лучший кандидат для --save
            print(f"    лучший кандидат: LocalPlayers=0x{ranked[0][0]:X}")
    if args.save:
        # расклад статичен для билда (структурные смещения), адреса кучи - нет:
        # после рестарта достаточно poe_scan.py --offs-file <файл>
        sig, d = max(shown.items(), key=lambda kv: kv[1]["Score"]) if shown else \
            ((None, None), None)
        if sig is None:
            print(f"{args.save} не записан: подтверждённого расклада нет")
        else:
            data = {}
            if gv:
                # версия патча игры первой строкой: статична, как смещения; имя релизного архива
                data["GameVersion"] = gv
            else:
                print(f"WARN: GameVersion не записан — источник версии не найден "
                      f"(logs/LatestClient.txt, fallback VERSIONINFO)")
            data["HealthOff"] = sig[0]
            data["TotalOff"] = sig[3]
            data["ManaOff"] = sig[1] + sig[3]
            data["ESOff"] = sig[2] + sig[3]
            data["BackPtrOff"] = sig[5]
            data["Score"] = d["Score"]
            if sig[6] is not None:
                data["ReservedOff"] = sig[6]
            if sig[4] is not None:
                data["CurrentOff"] = sig[4]
            if lp:
                data["LocalPlayers"] = lp[0]
            else:
                print("  LocalPlayers не записан: AreaInstance не найдена — файл не пройдёт шаг (F) "
                      "в poe_scan.py; проверь, что персонаж в игре")
            with open(args.save, "w", encoding="utf-8") as fh:
                json.dump(data, fh, indent=2)
            print(f"расклад записан в {args.save}: {json.dumps(data)}")
            print(f"  после рестарта: python3 poe_scan.py --offs-file {args.save}")
    mem.close()


if __name__ == "__main__":
    main()
