# Updating The Library

Use this file when you change the library itself, update protocol data, or prepare the repository for publishing.

## Rebuild After Code Changes

Windows:

```powershell
cd C:\path\to\bedrock-protocol-cpp
.\scripts\build.ps1
```

Linux / Termux:

```bash
cd /path/to/bedrock-protocol-cpp
./scripts/build.sh
```

Run the roundtrip check:

```bash
./build/protocol-roundtrip
```

## Rebuild Only Your Bot

If you edited only your bot project, do not rebuild the library.

```bash
cmake --build build
```

Your bot links to the already installed library through `CMAKE_PREFIX_PATH`.

## Update Bundled Minecraft Data

The library keeps protocol data in:

```text
data/minecraft-data/
data/generated/
```

Update flow:

```bash
./scripts/vendor_minecraft_data.sh
node tools/generate_protocol_tables.js
./scripts/build.sh
./build/protocol-roundtrip
```

If packet layouts changed, inspect the affected version files:

```text
data/minecraft-data/bedrock/<version>/protocol.json
data/minecraft-data/bedrock/<version>/proto.yml
data/minecraft-data/bedrock/<version>/types.yml
```

## Git Publishing Checklist

Before publishing:

- Keep `build/`, `install/`, auth caches, logs, packet dumps, and zips out of git.
- Keep source, headers, CMake files, scripts, docs, and required data in git.
- Run the build script once from a clean checkout.
- Run `./build/protocol-roundtrip`.
- Test at least one offline local server and one online server if you changed auth/login code.

The repository `.gitignore` already excludes local build output and runtime artifacts.

## Roadmap To Match JavaScript bedrock-protocol

Use this checklist when continuing the port from `node_modules`.

- Keep packet serialization schema-driven: packet name plus params object, like `client.write(name, params)` in JavaScript.
- Port missing `protodef` datatypes from `node_modules/protodef/src/datatypes` and `node_modules/bedrock-protocol/src/datatypes`.
- Add each ported datatype to `protocol-roundtrip`. Current coverage runs across all bundled protocol versions and includes JavaScript numeric endian rules, `bitfield`, fixed `buffer`/`array` counts, `count`, `ipAddress`, `endOfArray`, `entityMetadataLoop`, `entityMetadataItem`, `lstring`, `byterot`, and `varint128` bitflags through `player_auth_input`.
- Prefer native datatype handlers over packet-specific shortcuts.
- Add a protocol-roundtrip case when a new datatype is ported.
- Add live tests for handshake/login/resource-pack flow before changing connection code.
- Add higher-level helpers only after the raw schema packet path works.
