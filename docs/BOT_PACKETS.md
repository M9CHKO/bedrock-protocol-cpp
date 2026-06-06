# Bot Packet Examples

This page shows how to work with packets inside a bot.

There are three different ideas people often call "changing packets":

- Read an incoming packet and react to it.
- Make a changed copy of decoded packet fields for your own bot logic.
- Send or rewrite packets on the network.

The first two are supported by the current high-level API. Network sending is available for a small supported set of client packets. Full high-level sending/rewriting for every game packet is still growing.

## Enable Packet Decoding

Basic packet events only need `client.on("packet", ...)`.

For decoded fields, enable `decodePackets`:

```cpp
auto client = bedrock::createClient({
    .host = "localhost",
    .port = 19132,
    .username = "Notch",
    .version = "1.20.40",
    .offline = true,
    .debug = bedrock::DebugMode::Json,
    .decodePackets = true
});
```

`decodePackets = true` is slower on very large packets, so turn it on when you need fields for debugging or bot logic.

## Read Packets

```cpp
client.on("packet", [](const bedrock::Packet& packet) {
    std::cout << packet.name << " id=" << packet.id << "\n";
});
```

Listen to one packet name:

```cpp
client.on("start_game", [](const bedrock::Packet& packet) {
    std::cout << "Joined world\n";
});
```

Read a decoded field:

```cpp
client.on("disconnect", [](const bedrock::Packet& packet) {
    if (packet.has("message")) {
        std::cout << "Disconnect message: " << packet.get("message") << "\n";
    }
});
```

## Change A Local Packet Copy

Incoming packets are passed as `const bedrock::Packet&`. That means the handler cannot mutate the packet object from the runtime. If you want to change packet data for your own bot logic, copy the fields first:

```cpp
client.on("text", [](const bedrock::Packet& packet) {
    auto changed = packet.fields;
    changed["message"] = "[bot changed local copy] " + packet.get("message");

    std::cout << changed["message"] << "\n";
});
```

This changes your local copy only. It does not rewrite the packet on the server connection.

## Send A Supported Outgoing Packet

The high-level API has `send()` and `queue()` so bot code can use a bedrock-protocol-like shape. `send()` forwards supported packets to the active runtime when the bot is connected. These packets are wired now:

- `request_chunk_radius`
- `client_cache_status`
- `set_local_player_as_initialized`
- `resource_pack_client_response`

```cpp
client.send("request_chunk_radius", {
    {"radius", "20"}
});
```

Full high-level encoding and network sending for every packet is not finished yet. Unsupported packet names are reported by the runtime instead of being silently sent with a wrong shape.

You can inspect queued packets:

```cpp
for (const auto& [name, fields] : client.queuedPackets()) {
    std::cout << "queued " << name << "\n";
}
```

## Example Bot

The repository includes:

```text
examples/packet_event_bot.cpp
```

Build the library examples:

```bash
./scripts/build.sh
```

Windows:

```powershell
.\scripts\build.ps1
```

Run the example from the build directory after editing host/version/offline settings in the source:

```bash
./build/packet-event-bot
```

Windows:

```powershell
.\build\packet-event-bot.exe
```

## Find Packet Field Names

Open the protocol data for the version you are using:

```text
data/minecraft-data/bedrock/<version>/protocol.json
data/minecraft-data/bedrock/<version>/proto.yml
data/minecraft-data/bedrock/<version>/types.yml
```

External references:

- [PrismarineJS Minecraft Data protocol docs](https://prismarinejs.github.io/minecraft-data/protocol/)
- [Mojang bedrock-protocol-docs](https://github.com/Mojang/bedrock-protocol-docs)
