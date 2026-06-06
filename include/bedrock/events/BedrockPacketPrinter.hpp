#pragma once

#include <bedrock/events/BedrockPacket.hpp>

#include <iomanip>
#include <ostream>
#include <sstream>

namespace bedrock {

class BedrockPacketPrinter {
public:
    static std::string toString(const BedrockPacket& packet, std::size_t maxBufferBytes = 50) {
        std::ostringstream out;

        out << "{\n";
        out << "  data: {\n";
        out << "    name: '" << packet.data.name << "',\n";
        out << "    params: {\n";

        for (const auto& [key, value] : packet.data.params) {
            if (value == "undefined" || value == "<switch>") {
                continue;
            }
            out << "      " << key << ": " << formatValue(value) << ",\n";
        }

        out << "    }\n";
        out << "  },\n";
        out << "  metadata: { size: " << packet.metadata.size << " },\n";
        out << "  buffer: " << formatBuffer(packet.buffer, maxBufferBytes) << ",\n";
        out << "  fullBuffer: " << formatBuffer(packet.fullBuffer, maxBufferBytes);

        if (!packet.decodeError.empty()) {
            out << ",\n  decodeError: '" << packet.decodeError << "'";
        }

        out << "\n}";
        return out.str();
    }

private:
    static std::string formatValue(const std::string& value) {
        if (value == "true" || value == "false") return value;
        if (value == "<switch>") return "undefined";
        if (value.rfind("bytes:", 0) == 0) return "<Buffer " + value + ">";

        bool numeric = !value.empty();
        for (char c : value) {
            if (!(c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9'))) {
                numeric = false;
                break;
            }
        }

        if (numeric) return value;

        return "'" + escape(value) + "'";
    }

    static std::string formatBuffer(const std::vector<uint8_t>& bytes, std::size_t maxBytes) {
        std::ostringstream out;
        out << "<Buffer ";

        std::size_t shown = std::min(bytes.size(), maxBytes);

        for (std::size_t i = 0; i < shown; ++i) {
            if (i) out << " ";
            out << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(bytes[i]);
        }

        if (bytes.size() > shown) {
            out << " ... " << std::dec << (bytes.size() - shown) << " more bytes";
        }

        out << ">";
        return out.str();
    }

    static std::string escape(const std::string& value) {
        std::string out;
        for (unsigned char uc : value) {
            char c = static_cast<char>(uc);
            if (c == '\\' || c == '\'') {
                out.push_back('\\');
                out.push_back(c);
            } else if (uc < 32 || uc == 127) {
                out += "\\x";
                const char* hex = "0123456789abcdef";
                out.push_back(hex[(uc >> 4) & 0x0f]);
                out.push_back(hex[uc & 0x0f]);
            } else {
                out.push_back(c);
            }
        }
        return out;
    }
};

inline std::ostream& operator<<(std::ostream& os, const BedrockPacket& packet) {
    os << BedrockPacketPrinter::toString(packet);
    return os;
}

} // namespace bedrock
