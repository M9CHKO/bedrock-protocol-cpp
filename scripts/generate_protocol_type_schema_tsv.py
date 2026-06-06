#!/usr/bin/env python3
from pathlib import Path
import json

base = Path("data/minecraft-data/bedrock")
data_paths_file = Path("data/minecraft-data/dataPaths.json")
out_base = Path("data/generated/protocol-types/bedrock")
out_base.mkdir(parents=True, exist_ok=True)

data_paths = {}
if data_paths_file.exists():
    raw = json.loads(data_paths_file.read_text())
    data_paths = raw.get("bedrock", {})

def strip_bedrock(value: str) -> str:
    return value[len("bedrock/"):] if value.startswith("bedrock/") else value

def protocol_dir_for(version: str) -> str:
    entry = data_paths.get(version)
    if isinstance(entry, dict):
        return strip_bedrock(entry.get("protocol", f"bedrock/{version}"))
    return version

count = 0

for version_dir in sorted(base.iterdir(), key=lambda p: p.name):
    if not version_dir.is_dir():
        continue

    version = version_dir.name
    source_dir = protocol_dir_for(version)
    protocol_path = base / source_dir / "protocol.json"

    if not protocol_path.exists():
        continue

    data = json.loads(protocol_path.read_text())
    types = data.get("types", {})

    out = out_base / f"{version}.tsv"

    with out.open("w") as f:
        f.write("# type\tjson\n")
        f.write(f"# source_protocol_dir\t{source_dir}\n")
        f.write(f"# source_protocol_json\t{protocol_path}\n")

        for name, schema in sorted(types.items()):
            f.write(f"{name}\t{json.dumps(schema, separators=(',', ':'))}\n")

    print(f"[OK] {version}: {len(types)} types from {source_dir} -> {out}")
    count += 1

print(f"Generated {count} protocol type schema TSV files")
