# JavaScript bedrock-protocol Parity Roadmap

This file tracks the remaining work to make the C++ library behave like JavaScript `bedrock-protocol` across the bundled Bedrock versions.

## Done In The Current C++ Runtime

- RakNet client and server listener.
- Versioned MCPE batch/framing compression, including the old pre-1.20.61 compression shape.
- `NetworkSettingsRequest` / `NetworkSettings` flow.
- Offline and online login packet generation in-process.
- Xbox profile cache with interactive device-code login.
- Encryption handshake.
- High-level `bedrock::createClient({...})` API without terminal host/version/user arguments.
- Schema-shaped `send`, `write`, `queue`, and packet event decoding.
- Early `createServer`.
- Empty server resource-pack info/stack flow for clients with no required packs.
- Relay packet rewrite core, `createRelayServer`, and live `relay-test-server` example.
- `prismarine-chunk` foundation: paletted subchunks, 1.18 single runtime palette handling, biomes, no-cache `level_chunk`, blob status/miss decode, and tracked world columns.

## Still To Port

- Full JS `createServer` player runtime: player object lifecycle, disconnect semantics, batching queues, resource-pack state, close handling, and all login edge cases.
- Full JS `Relay` runtime parity: downstream player mapping, upstream session mapping, packet mutation hooks, queue flush behavior, compression/resource-pack edge cases, and live proxy tests for every supported login path.
- Full `prismarine-chunk` parity: subchunk packets, local persistence, network persistence, NBT block entities, cache blob hashing/generation, and chunk serialization tests per version.
- Full `prismarine-world` style world provider: loading/unloading, async provider API equivalent, columns by dimension, and block/entity helpers.
- Complete packet datatype audit against `node_modules/bedrock-protocol/src/datatypes` for every bundled version.
- Higher-level bot helpers matching common JS usage: chat helpers, movement helpers, inventory/window helpers, entity tracking, resource pack helpers, forms, and command helpers.
- More live integration tests: online auth, offline auth, public server joins, local server joins, relay joins from a real Minecraft client, and regression tests for 1.20.40/1.20.50.

The rule for new work is: port the JavaScript behavior as a reusable library feature, then add a focused smoke/roundtrip test that covers all versions where the packet or datatype exists.
