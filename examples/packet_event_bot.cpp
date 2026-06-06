#include <bedrock/bedrock.hpp>

#include <iostream>

int main() {
    auto client = bedrock::createClient({
        .host = "localhost",
        .port = 19132,
        .username = "PacketBot",
        .version = "1.20.40",
        .offline = true,
        .debug = bedrock::DebugMode::Json,
        .decodePackets = true
    });

    client.on("packet", [](const bedrock::Packet& packet) {
        std::cout << "packet " << packet.name << " id=" << packet.id << "\n";
    });

    client.on("start_game", [&client](const bedrock::Packet&) {
        std::cout << "Joined world\n";

        client.send("text", {
            {"type", "chat"},
            {"message", "Hello from C++ bot"}
        });

        for (const auto& [name, fields] : client.queuedPackets()) {
            std::cout << "queued outgoing packet intent: " << name
                      << " fields=" << fields.size() << "\n";
        }
    });

    client.on("text", [](const bedrock::Packet& packet) {
        auto changed = packet.fields;
        changed["message"] = "[local copy] " + packet.get("message");

        std::cout << changed["message"] << "\n";
    });

    client.on("disconnect", [](const bedrock::Packet& packet) {
        std::cout << "Disconnected";
        if (packet.has("reason")) std::cout << " reason=" << packet.get("reason");
        if (packet.has("message")) std::cout << " message=" << packet.get("message");
        std::cout << "\n";
    });

    return client.run();
}
