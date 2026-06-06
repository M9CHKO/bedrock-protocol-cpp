#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bedrock {

struct RakNetOpenResult {
    bool ok = false;

    std::string host;
    uint16_t port = 19132;

    uint64_t clientGuid = 0;
    uint64_t serverGuid = 0;

    int requestedMtu = 1400;
    int usedMtu = -1;
    int request1PayloadSize = -1;

    int mtuFromReply1 = -1;
    int mtuFromReply2 = -1;

    std::vector<int> triedMtus;

    std::string resolvedIp;
    uint16_t resolvedPort = 0;

    std::string clientAddressFromServer;
    uint16_t clientPortFromServer = 0;

    bool serverSecurity = false;
    uint32_t securityCookie = 0;
    bool hasSecurityCookie = false;

    std::string error;
};

class RakNetConnector {
public:
    static RakNetOpenResult openConnection(
        const std::string& host,
        uint16_t port,
        int mtu = 1400,
        int timeoutMs = 4000
    );

private:
    static std::vector<uint8_t> buildOpenConnectionRequest1(int mtu);

    static std::vector<uint8_t> buildOpenConnectionRequest2(
        const std::string& serverIp,
        uint16_t serverPort,
        int mtu,
        uint64_t clientGuid,
        bool serverSecurity,
        uint32_t securityCookie
    );
};

} // namespace bedrock
