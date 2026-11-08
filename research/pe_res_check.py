"""PE resource parse for the shipped exe (works on stripped MSVC PE: objdump -x gives
the section table and the Resource Directory, the tree is walked from the raw bytes).
Usage: python research/pe_res_check.py <exe|zip> <project_version>  (SPEC §2 resources)."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import zipfile
import xml.etree.ElementTree as ET

src = sys.argv[1]
expect = sys.argv[2]
if src.lower().endswith(".zip"):
    tmp = tempfile.mkdtemp()
    zf = zipfile.ZipFile(src)
    name = [n for n in zf.namelist() if n.lower().endswith(".exe")][0]
    target = str(pathlib.Path(tmp) / pathlib.Path(name).name)
    with open(target, "wb") as f:
        f.write(zf.read(name))
    data, shown = pathlib.Path(target).read_bytes(), name
else:
    target, data, shown = src, pathlib.Path(src).read_bytes(), src

env = dict(os.environ, LANG="C")  # objdump in English: stable regexes for the section table
dump = subprocess.run(["objdump", "-x", target], capture_output=True, text=True, env=env).stdout
image_base = int(re.search(r"ImageBase\s+([0-9a-f]+)", dump).group(1), 16)
res_rva = int(re.search(r"Entry 2 ([0-9a-f]+) ([0-9a-f]+)", dump).group(1), 16)

sections = []  # (name, rva, vsize, file_off)
for line in dump.splitlines():
    m = re.match(r"\s+\d+\s+(\S+)\s+([0-9a-f]{8})\s+([0-9a-f]{16})\s+([0-9a-f]{16})\s+([0-9a-f]{8})\s+2\*\*\d+", line)
    if m:
        sections.append((m.group(1), int(m.group(3), 16) - image_base, int(m.group(2), 16), int(m.group(5), 16)))
print(f"{shown}: {len(data)} bytes; sections " + ", ".join(f"{n} rva={r:#x} size={v:#x} off={o:#x}" for n, r, v, o in sections))

def off_of(rva):
    for _, rva0, vsize, off in sections:
        if rva0 <= rva < rva0 + vsize:
            return off + (rva - rva0)
    sys.exit(f"RVA {rva:#x} is not inside a loaded section")

def u(off, size):
    return int.from_bytes(data[off:off + size], "little")

base = off_of(res_rva)
entries = []  # (type, name_or_id, language, file_off, size)

def walk(node, path):
    # IMAGE_RESOURCE_DIRECTORY: Characteristics(4) TimeDateStamp(4) Major(2) Minor(2)
    # NumberOfNamedEntries(2) NumberOfIdEntries(2), then 8-byte entries from offset 16.
    named, ident = u(node + 12, 2), u(node + 14, 2)
    for i in range(named + ident):
        e = node + 16 + 8 * i
        did, doff = u(e, 4), u(e + 4, 4)
        if did & 0x80000000:
            # Named entry: the high bit marks a name, the low 16 bits (key & 0xFFFF) are
            # the offset of the name string from the Resource Directory base - NOT doff,
            # which is the child offset (review D1, fixed 2026-11-08: WAV leaves printed type '').
            p = base + (did & 0xFFFF)
            ln = u(p, 2)
            item = data[p + 2:p + 2 + 2 * ln].decode("utf-16")
        else:
            item = did
        child = base + (doff & 0x7FFFFFFF)
        if doff & 0x80000000:
            walk(child, path + [item])
        else:
            # leaf IMAGE_RESOURCE_DATA_ENTRY { OffsetToData(RVA), Size, CodePage, Reserved };
            # deepest level = language directory, so `item` is the language id.
            entries.append((path[0], path[1], item, off_of(u(child, 4)), u(child + 4, 4)))

walk(base, [])

# RT_ ids from WinUser.h: RT_ICON 3, RT_RCDATA 10, RT_MESSAGETABLE 11,
# RT_GROUP_ICON = RT_ICON + DIFFERENCE(11) = 14, RT_VERSION 16, RT_MANIFEST 24.
# rc writes WAVE as a NAMED type (the name string decodes to 'WAVE', seen by walk() as
# path[0]), so 11 is never a WAVE type here; 14 is RT_GROUP_ICON, NOT WAVE (the 101 leaf
# is the 146-byte GRPICONDIR: header + 10 icon records).
RT = {3: "ICON", 10: "RCDATA", 11: "MESSAGETABLE", 14: "GROUP_ICON", 16: "VERSIONINFO",
      24: "MANIFEST"}
counts = {}
for typ, name, lang, off, size in entries:
    key = (RT.get(typ, typ), name)
    counts[key] = counts.get(key, 0) + 1

print("Resource tree (type, resource name/id, entry count):")
for key in sorted(counts, key=str):
    print(f"  {key[0]} {key[1]} -> {counts[key]}")

man = [e for e in entries if e[0] == 24]
assert len(man) == 1, f"RT_MANIFEST count {len(man)} (must be exactly 1: app.rc embeds it; /MANIFEST:NO adds no second copy)"
typ, name, lang, off, size = man[0]
raw = data[off:off + size]
print(f"\nRT_MANIFEST: type=24 (RT_MANIFEST), id/name={name}, language=0x{lang:04x} (1033 en-US), "
      f"{size} bytes, XML decl at start: {raw[:5] == b'<?xml'}")
root = ET.fromstring(raw.decode("utf-8"))
ns = {"asm": "urn:schemas-microsoft-com:asm.v1", "v3": "urn:schemas-microsoft-com:asm.v3"}
ai = root.find("asm:assemblyIdentity", ns)
print("root:", root.tag, root.attrib)
print("assemblyIdentity:", ai.attrib)
assert ai is not None and ai.attrib["version"] == f"{expect}.0", f"manifest version {ai.attrib['version']} != project VERSION {expect} + .0 revision"
assert name == 1, f"RT_MANIFEST resource id is {name}, expected 1"
rel = root.find("v3:trustInfo/v3:security/v3:requestedPrivileges/v3:requestedExecutionLevel", ns)
print("requestedExecutionLevel:", rel.attrib)
ws = root.find("v3:application/v3:windowsSettings", ns)
print("windowsSettings:", [(c.tag.split('}')[-1], c.text) for c in ws])

# rc stores VERSIONINFO under the RT_VERSION type name (chr(16)), not the literal string.
vi = [data[off:off + size] for typ, name, lang, off, size in entries if typ == 16]
s = vi[0].decode("utf-16", "ignore")
print("VERSIONINFO contains project version:", expect in s, "| 4-field:", f"{expect}.0" in s)

dlls = sorted(set(m.group(1) for m in re.finditer(r"DLL Name: (\S+)", dump)))
print("Imported DLLs:", dlls)
# S0-3: CMake's default MSVC libs must not end up in the shipped exe.
forbidden = {"advapi32.dll", "ole32.dll", "oleaut32.dll", "uuid.dll", "comdlg32.dll"}
bad = {d.lower() for d in dlls} & forbidden
assert not bad, f"CMake default libraries leaked into the shipped exe: {sorted(bad)}"
print("No CMake default libs (advapi32/ole32/oleaut32/uuid/comdlg32) in the exe.")
print("\nCHECKS PASSED")
