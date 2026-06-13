#include <bedrock/bedrock.hpp>

#include <chrono>
#include <iostream>
#include <thread>

int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    bedrock::Relay relay({
        .version = "1.21.2",
        .host = "0.0.0.0",
        .port = 19132,
        .destination = {
            .host = "cpe.ign.gg",
            .port = 19132
        },
        .username = "RelayBot",
        .profile = "RelayBot",
        .offline = false,
        .interactiveAuth = true,
        .enableChunkCaching = false,
        .logging = true
    });

    relay.on("connect", [](bedrock::RelayPlayer& player) {
        std::cout << "New connection "
                  << player.connection.address << ":"
                  << player.connection.port << "\n";

        player.on("clientbound", [](bedrock::RelayPacketEvent& event) {
            (void)event;
        });

        player.on("serverbound", [](bedrock::RelayPacketEvent& event) {
            (void)event;
        });
    });

    relay.listen();
    std::cout << "Relay listening on 0.0.0.0:19132\n";

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
