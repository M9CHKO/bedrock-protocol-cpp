#include <bedrock/bedrock.hpp>

#include <iostream>
#include <string>

int main() {
    auto client = bedrock::createClient({
        .host = "cpe.ign.gg",
        .port = 19132,
        .username = "StewedV",
        .version = "1.26.20",
        .offline = false
    });

    client.on("start_game", [](const bedrock::Packet& packet) {
        std::cout << "\n[JOIN] start_game\n";
        packet.print();
    });

    client.on("move_player", [](const bedrock::Packet& packet) {
        std::cout << "\n[MOVE_PLAYER]\n";
        std::cout << "position = " << packet["position"] << "\n";
        std::cout << "x        = " << packet["position.x"] << "\n";
        std::cout << "y        = " << packet["position.y"] << "\n";
        std::cout << "z        = " << packet["position.z"] << "\n";
        std::cout << "yaw      = " << packet["yaw"] << "\n";
        std::cout << "pitch    = " << packet["pitch"] << "\n";
        std::cout << "mode     = " << packet["mode"] << "\n";
        std::cout << "on_ground= " << packet["on_ground"] << "\n";
        packet.print();
    });

    client.on("text", [](const bedrock::Packet& packet) {
        std::cout << "\n[TEXT]\n";
        std::cout << "type    = " << packet["type"] << "\n";
        std::cout << "message = " << packet["message"] << "\n";
        std::cout << "xuid    = " << packet["xuid"] << "\n";
        packet.print();
    });

    client.on("level_chunk", [](const bedrock::Packet& packet) {
        packet.print();
    });

    client.onError([](const std::string& err) {
        std::cerr << "\n[ERROR] " << err << "\n";
    });

    client.onClose([](const std::string& reason) {
        std::cerr << "\n[CLOSE] " << reason << "\n";
    });

    return client.run();
}
