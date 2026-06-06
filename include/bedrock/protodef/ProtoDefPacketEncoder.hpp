#pragma once

#include <bedrock/debug/ProtocolTypeTsvIndex.hpp>
#include <bedrock/generated/GeneratedProtocolTypes.hpp>
#include <bedrock/protodef/ProtoDefEncoder.hpp>
#include <bedrock/protodef/ProtoDefValue.hpp>
#include <bedrock/protodef/ProtoDefWriter.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bedrock {

class ProtoDefPacketEncoder {
public:
    explicit ProtoDefPacketEncoder(
        std::string version,
        ProtocolTypeTsvIndex typeIndex = ProtocolTypeTsvIndex()
    )
        : version_(std::move(version)),
          typeIndex_(std::move(typeIndex)) {}

    std::vector<uint8_t> encodePacket(
        const std::string& packetName,
        const ProtoDefValue& value
    ) const {
        auto typeJson = resolveType("packet_" + packetName);
        if (!typeJson.has_value()) {
            throw std::runtime_error("packet schema not found: " + packetName);
        }

        ProtoDefWriter writer;

        ProtoDefEncoder encoder([this](const std::string& typeName) {
            return this->resolveType(typeName);
        });

        encoder.encode(*typeJson, value, writer);
        return writer.take();
    }

private:
    std::string version_;
    ProtocolTypeTsvIndex typeIndex_;

    std::optional<std::string> resolveType(const std::string& typeName) const {
        auto fromIndex = typeIndex_.findTypeJson(version_, typeName);
        if (fromIndex.has_value()) {
            return fromIndex;
        }

        return bedrock::generatedProtocolTypeJson(typeName);
    }
};

}
