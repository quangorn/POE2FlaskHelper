"""
Извлечение C#-кода сканирования/чтения памяти из EyeAuras ауры (Script.json).

Работает с любым Script.json: текущая ревизия, ревизии из config.bak.7z (git),
любой скачанный пак другой ревизии.

    python extract_code.py <Script.json> <выходная_папка>
"""
import json
import os
import sys

# Файлы, не относящиеся к чтению памяти (UI / конфиг / документация)
EXCLUDE_NAMES = {"PoeEyeComponent.cs", "PoeEyeComponent.razor", "PoeHelperConfig.cs", "README.md"}

# namespace -> путь внутри CheatCartridge/GameHelper
NAMESPACE_DIRS = {
    "CheatCartridge": "CheatCartridge",
    "CheatCartridge.GameHelper": "CheatCartridge/GameHelper",
    "CheatCartridge.GameHelper.GameOffsets": "CheatCartridge/GameHelper/GameOffsets",
    "CheatCartridge.GameHelper.GameOffsets.States": "CheatCartridge/GameHelper/GameOffsets/States",
    "CheatCartridge.GameHelper.GameOffsets.States.InGameState": "CheatCartridge/GameHelper/GameOffsets/States/InGameState",
    "CheatCartridge.GameHelper.GameOffsets.Objects.Components": "CheatCartridge/GameHelper/GameOffsets/Objects/Components",
    "CheatCartridge.GameHelper.Natives": "CheatCartridge/GameHelper/Natives",
    "CheatCartridge.GameHelper.RemoteEnums": "CheatCartridge/GameHelper/RemoteEnums",
    "CheatCartridge.GameHelper.RemoteObjects": "CheatCartridge/GameHelper/RemoteObjects",
    "CheatCartridge.GameHelper.RemoteObjects.States": "CheatCartridge/GameHelper/RemoteObjects/States",
    "CheatCartridge.GameHelper.RemoteObjects.States.InGameStateObjects": "CheatCartridge/GameHelper/RemoteObjects/States/InGameStateObjects",
    "CheatCartridge.GameHelper.RemoteObjects.Components": "CheatCartridge/GameHelper/RemoteObjects/Components",
    "CheatCartridge.GameHelper.Scaffolding": "CheatCartridge/GameHelper/Scaffolding",
}


def collect_content(node, found):
    """Собирает (Name, Content) из произвольного JSON-дерева EyeAuras."""
    if isinstance(node, dict):
        if isinstance(node.get("Content"), str) and isinstance(node.get("Name"), str):
            found.append((node["Name"], node["Content"]))
        for v in node.values():
            collect_content(v, found)
    elif isinstance(node, list):
        for v in node:
            collect_content(v, found)


def dir_for(name, content):
    """Папка для файла: по namespace из Content, иначе по имени файла."""
    for ns in sorted(NAMESPACE_DIRS, key=len, reverse=True):
        if f"namespace {ns};" in content:
            return NAMESPACE_DIRS[ns]
    return "unclassified"


def extract(script_json_path, out_dir):
    data = json.load(open(script_json_path, encoding="utf-8-sig"))
    found = []
    collect_content(data, found)
    written = []
    seen = set()
    for name, content in found:
        if name in EXCLUDE_NAMES or name in seen:
            continue
        seen.add(name)
        folder = dir_for(name, content)
        path = os.path.join(out_dir, folder, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        written.append((folder, name, len(content)))
    return written


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    files = extract(src, dst)
    print(f"{src} -> {dst}: {len(files)} файлов")
    for folder, name, size in sorted(files):
        print(f"  {folder}/{name} ({size})")
