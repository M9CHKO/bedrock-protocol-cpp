#pragma once

#include <bedrock/debug/PacketFieldDecoder.hpp>
#include <bedrock/debug/ProtocolTypeTsvIndex.hpp>
#include <bedrock/generated/GeneratedProtocolTypes.hpp>
#include <bedrock/protodef/ProtoDefContext.hpp>
#include <bedrock/protodef/ProtoDefDecoder.hpp>
#include <bedrock/protodef/ProtoDefField.hpp>
#include <bedrock/protodef/ProtoDefReader.hpp>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bedrock {

class ProtoDefPacketDecoder {
public:
    explicit ProtoDefPacketDecoder(
        std::string version,
        ProtocolTypeTsvIndex typeIndex = ProtocolTypeTsvIndex()
    )
        : version_(std::move(version)),
          typeIndex_(std::move(typeIndex)) {}

    std::vector<ProtoDefField> decodePacket(
        const std::string& packetName,
        const std::vector<uint8_t>& payload
    ) const {
        auto typeJson = resolveType("packet_" + packetName);
        if (!typeJson.has_value()) {
            return {};
        }

        PacketFieldCursor cursor(payload);
        ProtoDefReader reader(cursor);
        ProtoDefContext context;
        std::vector<ProtoDefField> out;

        ProtoDefDecoder decoder([this](const std::string& typeName) {
            return this->resolveType(typeName);
        });

        try {
            decoder.decode(*typeJson, reader, "", out, context);
        }  catch (const std::exception& e) {
            ProtoDefField err;
            err.path = "$decode_error";
            err.type = "error";
            err.value = e.what();
            err.offset = reader.offset();
            err.size = 0;
            out.push_back(err);
        }

        return out;
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
