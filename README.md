# Bedrock Protocol C++

`bedrock-protocol-cpp` created on C++20

It is built for bot projects: you write normal C++ code, put connection settings in `bedrock::createClient({...})`, build your bot, and run your bot executable without passing host/version/user arguments in the terminal.

```cpp
#include <bedrock/bedrock.hpp>

#include <iostream>

int main() {
    auto client = bedrock::createClient({
        .host = "localhost",
        .port = 19132,
        .username = "Notch",
        .version = "1.20.40",
        .offline = true,
        .debug = bedrock::DebugMode::Events
    });

    client.on("start_game", [](const bedrock::Packet&) {
        std::cout << "Joined world\n";
    });

    client.on("packet", [](const bedrock::Packet& packet) {
        std::cout << packet.name << " id=" << packet.id << "\n";
    });

    return client.run();
}
```

## What Works

- RakNet ping and connect.
- Minecraft Bedrock network handshake.
- `NetworkSettingsRequest` / `NetworkSettings`.
- Login packet generation with versioned `clientData`.
- Xbox Live auth for online servers.
- Offline/self-signed auth for local offline servers.
- Resource pack response flow.
- Compression handling for old and new Bedrock protocol shapes.
- Packet id/name decoding across bundled protocol versions.
- Optional deep packet JSON decoding for debugging.
- `bedrock-protocol`-style client creation and event handlers.
- CMake package install for separate bot projects.
- Windows through MSYS2/MinGW, Linux, and Termux builds.

Current limitation: the public high-level API is strongest for connecting, receiving packets, inspecting events, and writing bot logic around incoming packets. Full high-level encoding/sending for every game packet is still growing.

## Quick Start

Build and install the library once:

```bash
cd bedrock-protocol-cpp
./scripts/build.sh
```

Windows PowerShell:

```powershell
cd C:\path\to\bedrock-protocol-cpp
.\scripts\build.ps1
```

Then create your bot project and link to:

```cmake
BedrockProtocol::bedrock_protocol
```

Detailed beginner instructions are here:

- [Getting Started](docs/GETTING_STARTED.md)
- [Bot Packet Examples](docs/BOT_PACKETS.md)
- [Supported Versions](docs/VERSIONS.md)
- [Packet Documentation](docs/PACKETS.md)
- [Updating The Library](docs/MAINTENANCE.md)

## Install Layout

The build scripts install into `install/` by default:

```text
bedrock-protocol-cpp/
  install/
    bin/                       runtime helpers
    include/                   public C++ headers
    lib/                       static library and CMake package files
    share/bedrock-protocol-cpp/  protocol data and clientData template
```

You do not rebuild the library every time you edit your bot. Rebuild the library only when files inside this library change. For normal bot development, rebuild only your bot project.

## Client API

```cpp
auto client = bedrock::createClient({
    .host = "localhost",
    .port = 19132,
    .username = "Notch",
    .profile = "Notch",
    .version = "latest",
    .offline = false,
    .debug = bedrock::DebugMode::Events,
    .decodePackets = false,
    .packetDump = false,
    .regenLogin = false,
    .holdSeconds = 0
});
```

| Option | Default | Meaning |
|---|---:|---|
| `host` | `localhost` | Bedrock server address. |
| `port` | `19132` | Bedrock server port. |
| `username` | `Bot` | Bot name. In online mode this is also the default auth cache profile. |
| `profile` | empty | Xbox auth cache profile. Empty means `username`. |
| `version` | `latest` | Bedrock version from the bundled version table. |
| `offline` | `false` | Use self-signed auth instead of Xbox Live. |
| `debug` | `Off` | `Off`, `Events`, `Packets`, `Json`, or `Trace`. |
| `decodePackets` | `false` | Decode packet fields into JSON-style event fields. |
| `packetDump` | `false` | Print extra packet dump output. |
| `regenLogin` | `false` | Force login packet regeneration for the current run. |
| `holdSeconds` | `0` | `0` keeps the client connected until the process exits. |
| `executableDir` | auto | Directory containing installed runtime helpers. |
| `runtimeExecutable` | auto | Direct path to the `bedrock` helper. |

Events:

```cpp
client.on("packet", [](const bedrock::Packet& packet) {});
client.on("start_game", [](const bedrock::Packet& packet) {});
client.on("disconnect", [](const bedrock::Packet& packet) {});
client.onText([](const bedrock::TextPacket& text) {});
```

Packet examples for bots are in [docs/BOT_PACKETS.md](docs/BOT_PACKETS.md).

## Supported Versions

The bundled protocol data currently includes these Bedrock versions:

| Minecraft | Protocol |
|---:|---:|
| 0.14.3 | 70 |
| 0.15.6 | 82 |
| 1.0.0 | 100 |
| 1.16.201 | 422 |
| 1.16.210 | 428 |
| 1.16.220 | 431 |
| 1.17.0 | 440 |
| 1.17.10 | 448 |
| 1.17.30 | 465 |
| 1.17.40 | 471 |
| 1.18.0 | 475 |
| 1.18.11 | 486 |
| 1.18.30 | 503 |
| 1.19.1 | 527 |
| 1.19.10 | 534 |
| 1.19.20 | 544 |
| 1.19.21 | 545 |
| 1.19.30 | 554 |
| 1.19.40 | 557 |
| 1.19.50 | 560 |
| 1.19.60 | 567 |
| 1.19.62 | 567 |
| 1.19.63 | 568 |
| 1.19.70 | 575 |
| 1.19.80 | 582 |
| 1.20.0 | 589 |
| 1.20.10 | 594 |
| 1.20.15 | 594 |
| 1.20.30 | 618 |
| 1.20.40 | 622 |
| 1.20.50 | 630 |
| 1.20.61 | 649 |
| 1.20.71 | 662 |
| 1.20.80 | 671 |
| 1.21.0 | 685 |
| 1.21.2 | 686 |
| 1.21.20 | 712 |
| 1.21.30 | 729 |
| 1.21.42 | 748 |
| 1.21.50 | 766 |
| 1.21.60 | 776 |
| 1.21.70 | 786 |
| 1.21.80 | 800 |
| 1.21.90 | 818 |
| 1.21.93 | 819 |
| 1.21.100 | 827 |
| 1.21.111 | 844 |
| 1.21.120 | 859 |
| 1.21.124 | 860 |
| 1.21.130 | 898 |
| 1.26.0 | 924 |
| 1.26.10 | 944 |
| 1.26.20 | 975 |

More details and version-specific notes are in [docs/VERSIONS.md](docs/VERSIONS.md).

## Build Status Checks

Run the local protocol roundtrip check:

```bash
./build/protocol-roundtrip
```

Expected result:

```text
[ROUNDTRIP] checkedVersions=27 failures=0
```

## VS Code

Open the library folder itself:

```text
C:\path\to\bedrock-protocol-cpp
```

Do not open the parent folder that contains `bedrock-protocol-cpp/`, `node_modules/`, logs, and zip files. The checked-in `.vscode/settings.json` expects `${workspaceFolder}` to be the library root.

Then run:

```text
CMake: Configure
CMake: Build
```

If IntelliSense still shows a red include for `<bedrock/bedrock.hpp>`, run:

```text
C/C++: Reset IntelliSense Database
```

## References

- JavaScript inspiration: [`PrismarineJS/bedrock-protocol`](https://github.com/PrismarineJS/bedrock-protocol)
- Protocol data: [`PrismarineJS/minecraft-data`](https://github.com/PrismarineJS/minecraft-data)
- Packet tables: [PrismarineJS Minecraft Data documentation](https://prismarinejs.github.io/minecraft-data/)
- Mojang protocol notes: [`Mojang/bedrock-protocol-docs`](https://github.com/Mojang/bedrock-protocol-docs)

## License

Add your project license before publishing this repository.
