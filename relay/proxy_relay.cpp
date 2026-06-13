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
        .motd = "Bedrock Protocol C++ Relay",
        .username = "RelayBot",
        .offline = false,
        .destination = {
            .host = "cpe.ign.gg",
            .port = 19132
        }
    });

    relay.onError([](const std::string& message) {
        std::cerr << "[relay error] " << message << "\n";
    });

    relay.onStatus([](const bedrock::BedrockLiveRelayStatus& status) {
        std::cout << "[relay status]"
                  << " listening=" << status.listening
                  << " downstream=" << status.downstreamJoined
                  << " upstream_started=" << status.upstreamStarted
                  << " upstream_ready=" << status.upstreamReady
                  << " port=" << status.boundPort
                  << "\n";
    });

    relay.onConnect([](bedrock::RelayPlayer& player) {
        std::cout << "[relay] New connection "
                  << player.connection.address << ":"
                  << player.connection.port << "\n";
    });

    relay.listen();
    std::cout << "Relay listening on 0.0.0.0:19132\n";

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
