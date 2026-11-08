# PathOfExile.exe memory scanner

Work with the **live game process**: reading HP/Mana/EnergyShield/Ward/Rage through the
offset chain (`poe_scan.py`) and finding the offsets themselves from known values
(`find_offsets.py`). No writes to the process memory — read-only.
The Russian version of this document is [`README.ru.md`](README.ru.md).

The addressing-chain analysis, the POE2-Mem aura code analysis and the revision
comparison are in [`../research/REPORT.md`](../research/REPORT.md). Here: launch and
options only.

## Requirements

Run with **Windows-native** Python 3.x 64-bit, for example
`C:\Users\<user>\AppData\Local\Programs\Python\Python3.12\python.exe`
(`py -3`). cygwin-python does not fit: it has no `ctypes.WinDLL`
(the scripts re-exec themselves on the native interpreter). `pymem` is not required.

For a live run you need the game client running and a character in a location.

## `poe_scan.py` — reading values through the chain

```
python poe_scan.py                              # find PathOfExile*.exe → signature scan → chain → HP/MP/ES/Ward/Rage
python poe_scan.py --file                       # offline: signature in the binary file (1 match → VA 0x45EE348)
python poe_scan.py --rev rev32                  # offsets of another revision
python poe_scan.py --selftest                   # chain on synthetic data, no game process
python poe_scan.py --live --fps 2               # live mode: one HP/Mana/ES line, redrawn 2 times/s (Ctrl+C exits)
python poe_scan.py --offs-file offsets.json --values   # HP/Mana/ES only, no tracing
python poe_scan.py --pid 13996 \
    --offs HealthOff=0x1DC,TotalOff=0x34,CurrentOff=0x38,ManaOff=0x234,ESOff=0x274
```

The chain: `OpenProcess(VM_READ | QUERY_INFORMATION)` → `NtQueryInformationProcess` (PEB)
→ `ImageBase` from PEB and `SizeOfImage` from the PE header → image scan by regex in
1 MB chunks → RIP computation of the anchor → steps (A)–(J): StdVector `{First,Last,End}`,
bucket, `std::wstring` (SSO), finding `"Life"` by names from ComponentLookUp, reading the
vitails, the percent by the same formula.

In non-quiet mode the header prints `GameVersion <patch>` — the game patch version
(`0.5.5f`): PathOfExile.exe has no VERSIONINFO resource, so the version is read from
the game client log `logs/LatestClient.txt` (the startup line `User agent: PoE
poe2_production/tags/0.5.5f Windows x64` of the `[INFO Client <pid>]` session);
VERSIONINFO is used as a fallback for exes that have it.

Options:

| Option | Meaning |
|---|---|
| `--file` | signature scan in the binary file (`EXE_PATH`), no process |
| `--rev NAME` | offsets of the revision (`rev34` by default — the current patch, verified by a live run; `rev33`, `rev32`, `rev2025-10-11` exist; rev34: LocalPlayers +8 and vitals `Ward @+0x2E0`, `Rage @+0x298`) |
| `--offs K=0x..,…` | offsets fed by hand: `HealthOff`, `TotalOff`, `CurrentOff`, `ManaOff`, `ESOff`, `WardOff`, `RageOff`, `BackPtrOff`, `ReservedOff`, `LocalPlayers` |
| `--offs-file FILE` | the same from JSON (the same keys as `--offs`); non-numeric values (the string `GameVersion`) are skipped |
| `--pid N` | the process PID (otherwise the search is by name) |
| `--values` | suppresses the step (A)–(J) tracing, prints the vitals (HP/Mana/ES/Ward/Rage) |
| `--live` | live mode: one HP/Mana/ES status line, redrawn at the given rate (Ctrl+C exits); waits 5 s for the process/login, resyncs when the process dies, FAST cadence by `--fps` |
| `--fps N` | redraw rate of `--live` in Hz (must be > 0), e.g. `--live --fps 2` |
| `--selftest` | the chain run on `find_offsets.build_fake` synthetic data, offline |

Speeds (measured): one `ReadProcessMemory` (4 bytes) ~3 µs; the signature scan of a
78 MB image takes 0.08 s. In a frame (~16 ms at TargetFps 61) the chain is ~10 reads.
Scanning the whole heap is not needed: the chain is restored from offsets (one read of
the global GameStates → walking pointers, ~25 reads).

Reading the values with one command when the layout is found and saved (the scan does
not participate):

```
python poe_scan.py --offs-file offsets.json --values
    Health @+0x1A8: Current=4000 Total=5000 ReservedFlat=100 ReservedPercent=2023 -> 103%
    Mana   @+0x200: Current=2000 Total=3000 ...
    EnergyShield @+0x240: ...
    Ward   @+0x2E0: Current=840 Total=840 ReservedFlat=0 ReservedPercent=0 -> 100%
    Rage   @+0x298: Current=15 Total=30 ReservedFlat=0 ReservedPercent=0 -> 50%
```

## `find_offsets.py` — finding offsets from values

The "values → offsets" tool: we give the known maxima of the current character, the
script finds exact int32 matches in memory and restores the layout.

```
python find_offsets.py --hp 5000 --mana 3000 --es 1200
python find_offsets.py --hp 5000 --mana 3000 --es 1200 --current-hp 4000   # gives the Current offset too
python find_offsets.py --pid 11436 ...
python find_offsets.py --selftest          # algorithm check on synthetic data (rev33 layout)
python find_offsets.py --bench 32          # stage timings on synthetic data of the given size (MB)
python find_offsets.py --hp 7025 --mana 949 --es 182 --current-hp 6374 --tol 0 --save offsets.json
```

Options:

| Option | Meaning |
|---|---|
| `--hp/--mana/--es N` | the maximum (Total) values of the current character |
| `--current-hp/--current-mana/--current-es N` | the current values; `--current-hp` gives the `Current` offset at once |
| `--pid N` | the process PID (otherwise the search is by name) |
| `--anchor K=0x..,…` | baked anchor: `HealthOff` (Total Health inside Life), `TotalOff` (Total inside Vital), `BackPtrOff` (the Vital's back-pointer to Life), `ManaGap` (Health→Mana), `EsGap` (Mana→ES), `ReservedOff`. Missing keys are taken from the baked `ANCHOR` |
| `--tol N` | tolerance around the anchor (default 8 bytes; `--tol 0` — strict at the anchor) |
| `--wide` | release the anchor: `TotalOff 0x20..0x8C`, `BackPtrOff {0x04..0x28}`, the Vital step is not fixed — a full sweep for revisions with a different layout |
| `--min-score N` | do not print candidates with a score below N (default 5) |
| `--save FILE` | write the layout JSON: the first key is `"GameVersion"` (the patch version from the client log, see above; a warning and no key when the source is missing), then the offsets (heap addresses are not saved) |
| `--selftest` / `--bench MB` | offline algorithm check and stage timings |

Logic:

1. `VirtualQueryEx` → the list of committed-readable regions; search `int32 == hp` in
   all of them (`bytes.find`).
2. For each match, iterate the `TotalOff` hypotheses: Vital base = `address − TotalOff`.
   The Vital is validated by structure: vtable @+0x00, back-pointer to Life @+0x10,
   `ReservedFlat` @+0x18, `ReservedPercent` @+0x1C ∈ [0, 10000].
3. Get the Life component via the back-pointer; inside it search int32 == mana and
   == es → their offsets; each candidate is checked as a Vital (the same `TotalOff`,
   the same back-pointer to Life).
4. Backtrace: find pointers to Life → the std::vector `ComponentListPtr`
   (`First = site − 8k`), Entity = vector − 0x10, check `IsValid == 0xC` / `Id`; then
   pointers to Entity → the std::vector `LocalPlayers` → the offset inside AreaInstance.

Confirming a candidate is direct reading, not a 16 GB scan: `Life+0x08` is the pointer
to Entity, `Entity+0x8C == 0xC`, the component vector `Entity+0x10` contains the Life
address, `Entity+0x88` is the Id. 4–5 reads per candidate. The speed estimate of
`ReadProcessMemory` in this environment: 728 MB/s, one pass over the heap ~22 s, the
full path (4 passes) ~1.5 min. The scan path is the fallback (if the Entity offsets of
this patch do not match the rev33 hypotheses).

The `hp` value occurs in memory many times (not only as Total), so the selection goes
by structural checks (back-pointer + ReservedPercent + the match of the Mana/ES
offsets), not by one match. Candidates are collapsed by the output: they are grouped
by the offset signature (the Life address is not part of the signature), it prints
`unique layouts: N` and `Life objects k` for each group. Two types of noise:

1. **Shifted families** (`TotalOff 0x2C..0x3C`, `HealthOff 0x1A0..0x1B0`) — one Total
   Health needle read with a base shift; the shift is symmetric: the checks are
   relative, so `(base+4, t+4, b+4)` satisfies the same conditions. The absolute check
   tells them apart: the Vital's vtable must lie inside the main module
   (`ImageBase..ImageBase+SizeOfImage`). The score kills them: vtable outside the
   module (−3), stat-id fields (−1), Reserved not at the anchor (−1), the Vital bases
   are multiples of 8 (the shifted families are non-modular). A strict `--tol 0` does
   not produce them at all.
2. **Duplicate Life objects of one layout** — several heap objects with the same
   HP/Mana/ES values (neighboring Life components, monsters). The layout is identical,
   the score is lower.

The best layout prints a ready command `poe_scan.py --offs HealthOff=…,TotalOff=…,ManaOff=…,ESOff=…,CurrentOff=…`
— the offsets are fed as **positions of the Total values inside Life** (the Vital base
is `Off − TotalOff`), and for Mana/ES this is `Off + TotalOff`.

The self-check on synthetic data with the rev33 layout (`--selftest`) restores everything:

```
Total@0x34  Life @0x600000: Health@0x1DC (Vital base 0x1A8) Mana@0x234 ES@0x274 Current@0x38
ComponentListPtr vector @0x400010 (Entity+0x10), index 0 of 2, Entity @0x400000 IsValid=0xC Id=123
LocalPlayers vector @0x2005B8 index 0 count 1 (AreaInstance+0x5B8)
```

The stage timings (`--bench`, each line is a full run):

```
filler 32 MB, HP matches 129:  scan 0.01 s, discover 0.01 s, pointers 0.01 s, first_hits 0.00 s
filler 64 MB, HP matches 257:  scan 0.01 s, discover 0.02 s, pointers 0.02 s, first_hits 0.01 s
filler 128 MB, HP matches 513: scan 0.03 s, discover 0.03 s, pointers 0.04 s, first_hits 0.02 s
```

Volume × 4 ⇒ time × 4, the number of candidates and confirmations is constant — the
dependence is linear. For the real 10 GB the extrapolation is ~30–40 s for all stages
(memory reading is dominant).

Practical rules of a live run:

- give `--current-hp` (then Current gets +2 and `CurrentOff` is found at once);
- read the Total values at full HP (Current == Total gives extra matches);
- pass the candidates with the highest score to `poe_scan.py --offs HealthOff=…,TotalOff=…,…`.

## `find_vital.py` — one-shot search of a new vital's offset (Ward/Rage/…)

When a patch adds vitals to Life (the HP/Mana/ES offsets are already known from
`offsets.json`), the offset is searched by values: the chain (A)–(J) from `poe_scan.py`
takes the local player's Life component, integer matches `int == max` are searched in
the window `Life..Life+--span`, a candidate is checked as a Vital (the mandatory
back-pointer to the owner component, the vtable inside the main module, the stat-ids,
ReservedFlat/Percent; current is checked at `CurrentOff`, +2 to the score). The stage
order: the Life window → the scan of all components of the player (the comp vector,
before and after Life, the owner is resolved by the CLU bucket) → the heap.

```
python find_vital.py --label Ward --max 840 --current 840
python find_vital.py --label Rage --max 30 --current 15
python find_vital.py --label Spirit --max 949            # only max: the actual Current is printed
python find_vital.py --label Spirit --max 110 --types i16,u16
python find_vital.py --selftest      # synthetic: Rage 32-bit in Life, Spirit 16-bit in Position
```

Options: `--types` (u32,i32,u16,i16,u64,i64 — all by default; int16 with zero upper
bytes is indistinguishable from u32, the offset is one), `--span` (the window
0x1000), `--pid`, `--offs-file`, `--rev`, `--heap` (skip Life/components). The best
candidate prints the ready JSON key `"LabelOff": N` and the command
`poe_scan.py --offs LabelOff=…`. A vital outside the Life component — the owner
component is printed by name with the vital offsets inside it, the Life key is not
written.

## `offsets.json` — the layout saved by the scanner

```json
{
  "GameVersion": "0.5.5f",
  "HealthOff": 476,    "TotalOff": 52,     "ManaOff": 564,   "ESOff": 628,
  "WardOff": 788,      "RageOff": 716,
  "BackPtrOff": 16,    "CurrentOff": 56,   "LocalPlayers": 1472,
  "Score": 9
}
```

`GameVersion` is the patch version string (read from `logs/LatestClient.txt` of the
running game, see above); the application ignores it, and the release archive name is
built from it. `WardOff`/`RageOff` are vitals added by the 1.4.0 patch (Total
positions inside Life, the same semantics as HealthOff/ManaOff/ESOff; bases @+0x2E0
and @+0x298, Life index 4). Found by the scanner `find_vital.py`; their Reserved is
zero.

The keys are the same as of `--offs`. `LocalPlayers` is mandatory: without it the
chain does not reach the player list. Heap addresses (`Life`, `Entity`,
`AreaInstance`) are not written to JSON — they change every run.

The working cycle (offsets are found once per patch, after that the scan is not
needed):

```
python find_offsets.py --hp 7025 --mana 949 --es 182 --current-hp 6374 --tol 0 --save offsets.json
python poe_scan.py --offs-file offsets.json           # after a restart: PID, ImageBase, chain — on their own
```

`--area` from the CLI was removed: the AreaInstance address is determined
automatically by the same chain as in `poe_scan.py` (signature → `GameStates*` →
`States[4]` → `+0x290`), so `LocalPlayers` lands in JSON at once.

After saving, a repeated scan is **not needed**: `poe_scan.py` restores the chain by
the signature (one read at `base+RVA`) and walks the heap by pointers. The static and
dynamic quantities (offsets, RVA, `ImageBase`, heap addresses, PID) are analyzed in
section 11 of `../research/REPORT.md`.

`offsets_selftest.json` is the layout found by `--selftest` on synthetic data; it
serves as an algorithm check, not as data of the current build.

The synthetic data was extended to GameState (`CurrentStatePtr @+0x10`,
`States 13×16 @+0x50`, `InGameState(4) → AreaInstance @+0x290`). The chain defects
found on this run are analyzed in section 11 of `../research/REPORT.md`.
