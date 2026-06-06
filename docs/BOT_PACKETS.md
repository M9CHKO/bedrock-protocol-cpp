# Bot Packet Examples

This page shows how to work with packets inside a bot.

There are three different ideas people often call "changing packets":

- Read an incoming packet and react to it.
- Make a changed copy of decoded packet fields for your own bot logic.
- Send or rewrite packets on the network.

The first two are supported by the current high-level API. Network sending uses the versioned packet schema, like JavaScript `bedrock-protocol` / `protodef`: give `send()` a packet name and a `ProtoDefValue` object shaped like that packet for the selected Minecraft version.

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

## Send An Outgoing Packet

The high-level API has `write()` and `send()` so bot code can use a bedrock-protocol-like shape. `write()` forwards the packet to the active runtime when the bot is connected. Any packet present in the bundled registry for the chosen version can be encoded when you provide the fields required by that packet schema.

```cpp
client.write("request_chunk_radius", bedrock::object({
    {"chunk_radius", bedrock::i32(20)},
    {"max_radius", bedrock::u32(0)}
}));
```

Enums use their schema names:

```cpp
client.write("resource_pack_client_response", bedrock::object({
    {"response_status", bedrock::str("completed")},
    {"resourcepackids", bedrock::array({})}
}));
```

Nested containers use nested objects:

```cpp
client.write("move_player", bedrock::object({
    {"runtime_id", bedrock::u64(1)},
    {"position", bedrock::object({
        {"x", bedrock::f32(0.0f)},
        {"y", bedrock::f32(64.0f)},
        {"z", bedrock::f32(0.0f)}
    })},
    {"pitch", bedrock::f32(0.0f)},
    {"yaw", bedrock::f32(0.0f)},
    {"head_yaw", bedrock::f32(0.0f)},
    {"mode", bedrock::str("normal")},
    {"on_ground", bedrock::boolean(true)},
    {"ridden_runtime_id", bedrock::u64(0)},
    {"tick", bedrock::u64(1)}
}));
```

Some packet shapes change between versions. For example, `text` in newer versions has extra `category`/filtered-message fields. Always use the schema for the version selected in `createClient()`.

You can inspect queued packets:

```cpp
for (const auto& [name, fields] : client.queuedPacketValues()) {
    std::cout << "queued " << name << "\n";
}
```

## Example Bot

The repository includes:

```text
examples/packet_event_bot.cpp
examples/medium_bot.cpp
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
./build/medium-bot
```

Windows:

```powershell
.\build\packet-event-bot.exe
.\build\medium-bot.exe
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
