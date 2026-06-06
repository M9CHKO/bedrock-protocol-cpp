#!/usr/bin/env python3
from pathlib import Path
import json

base = Path("data/minecraft-data/bedrock")
out_base = Path("data/generated/block-runtime/bedrock")
out_base.mkdir(parents=True, exist_ok=True)

count = 0

for version_dir in sorted(base.iterdir(), key=lambda p: p.name):
    if not version_dir.is_dir():
        continue

    blocks_path = version_dir / "blocks.json"
    if not blocks_path.exists():
        continue

    try:
        blocks = json.loads(blocks_path.read_text())
    except Exception as e:
        print(f"[SKIP] {version_dir.name}: {e}")
        continue

    rows = []

    if isinstance(blocks, list):
        for block in blocks:
            if not isinstance(block, dict):
                continue

            name = block.get("name") or block.get("displayName")
            runtime_id = (
                block.get("runtimeId")
                if "runtimeId" in block
                else block.get("id")
            )

            if name is None or runtime_id is None:
                continue

            if not str(name).startswith("minecraft:"):
                name = "minecraft:" + str(name)

            rows.append((int(runtime_id), str(name)))

    rows.sort(key=lambda x: x[0])

    out = out_base / f"{version_dir.name}.tsv"
    with out.open("w") as f:
        f.write("# runtimeId\tname\n")
        for runtime_id, name in rows:
            f.write(f"{runtime_id}\t{name}\n")

    count += 1
    print(f"[OK] {version_dir.name}: {len(rows)} blocks -> {out}")

print(f"Generated {count} block runtime TSV files")
