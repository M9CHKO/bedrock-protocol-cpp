#!/usr/bin/env python3
from pathlib import Path
import json

base = Path("data/minecraft-data/bedrock")
data_paths_file = Path("data/minecraft-data/dataPaths.json")
out_base = Path("data/generated/packet-schema/bedrock")
out_base.mkdir(parents=True, exist_ok=True)

data_paths = {}
if data_paths_file.exists():
    raw = json.loads(data_paths_file.read_text())
    data_paths = raw.get("bedrock", {})

def strip_bedrock(value: str) -> str:
    if value.startswith("bedrock/"):
        return value[len("bedrock/"):]
    return value

def protocol_dir_for(version: str) -> str:
    entry = data_paths.get(version)
    if isinstance(entry, dict):
        return strip_bedrock(entry.get("protocol", f"bedrock/{version}"))
    return version

def load_protocol_types(version: str):
    pdir = protocol_dir_for(version)
    protocol_path = base / pdir / "protocol.json"
    if not protocol_path.exists():
        return None, pdir, protocol_path
    data = json.loads(protocol_path.read_text())
    return data.get("types", {}), pdir, protocol_path

count = 0

for version_dir in sorted(base.iterdir(), key=lambda p: p.name):
    if not version_dir.is_dir():
        continue

    version = version_dir.name
    types, source_protocol_dir, protocol_path = load_protocol_types(version)

    if types is None:
        continue

    rows = []

    for key, value in types.items():
        if not key.startswith("packet_") or key == "packet":
            continue

        packet_name = key[len("packet_"):]
        fields = []

        if isinstance(value, list) and len(value) >= 2 and value[0] == "container":
            container = value[1]
            if isinstance(container, list):
                for field in container:
                    if isinstance(field, dict):
                        name = field.get("name", "")
                        typ = field.get("type", "")
                        fields.append((name, json.dumps(typ, separators=(",", ":"))))

        rows.append((packet_name, fields))

    out = out_base / f"{version}.tsv"

    with out.open("w") as f:
        f.write("# packet\tfield\ttype\n")
        f.write(f"# source_protocol_dir\t{source_protocol_dir}\n")
        f.write(f"# source_protocol_json\t{protocol_path}\n")

        for packet_name, fields in sorted(rows):
            for field_name, field_type in fields:
                f.write(f"{packet_name}\t{field_name}\t{field_type}\n")

    count += 1
    print(f"[OK] {version}: {len(rows)} packet schemas from {source_protocol_dir} -> {out}")

print(f"Generated {count} packet schema TSV files")
