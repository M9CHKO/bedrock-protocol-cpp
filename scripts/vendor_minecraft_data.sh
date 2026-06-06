#!/usr/bin/env bash
set -euo pipefail

SRC=""

for p in \
  "./node_modules/minecraft-data/minecraft-data/data/bedrock" \
  "./node_modules/minecraft-data/data/bedrock" \
  "../node_modules/minecraft-data/minecraft-data/data/bedrock" \
  "../node_modules/minecraft-data/data/bedrock"
do
  if [ -d "$p" ]; then
    SRC="$p"
    break
  fi
done

if [ -z "$SRC" ]; then
  echo "minecraft-data bedrock data not found"
  echo "Expected one of:"
  echo "  ./node_modules/minecraft-data/minecraft-data/data/bedrock"
  echo "  ./node_modules/minecraft-data/data/bedrock"
  exit 1
fi

DST="./data/minecraft-data/bedrock"

rm -rf "$DST"
mkdir -p "$DST"

cp -R "$SRC"/. "$DST"/

DATA_PATHS=""
for dp in \
  "./node_modules/minecraft-data/minecraft-data/data/dataPaths.json" \
  "./node_modules/minecraft-data/data/dataPaths.json" \
  "../node_modules/minecraft-data/minecraft-data/data/dataPaths.json" \
  "../node_modules/minecraft-data/data/dataPaths.json"
do
  if [ -f "$dp" ]; then
    DATA_PATHS="$dp"
    break
  fi
done

if [ -n "$DATA_PATHS" ]; then
  cp "$DATA_PATHS" "./data/minecraft-data/dataPaths.json"
fi

python3 - <<'PY'
from pathlib import Path
import json

base = Path("data/minecraft-data/bedrock")

versions = []
protocols = {}

for d in sorted(base.iterdir(), key=lambda p: p.name):
    if not d.is_dir():
        continue
    if d.name in ("common", "latest"):
        continue

    version_file = d / "version.json"
    if not version_file.exists():
        continue

    info = json.loads(version_file.read_text())
    mc = info.get("minecraftVersion", d.name)
    proto = info.get("version")

    versions.append({
        "directory": d.name,
        "minecraftVersion": mc,
        "protocol": proto,
        "majorVersion": info.get("majorVersion", ""),
        "releaseType": info.get("releaseType", "")
    })

    if proto is not None:
        protocols[str(proto)] = d.name

manifest = {
    "source": "minecraft-data",
    "edition": "bedrock",
    "versionCount": len(versions),
    "versions": versions,
    "protocolToDirectory": protocols
}

(base / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
print(f"Vendored {len(versions)} Bedrock versions into {base}")
PY
