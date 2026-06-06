#pragma once

#include <algorithm>

#include <bedrock/protodef/ProtoDefContext.hpp>
#include <bedrock/protodef/ProtoDefField.hpp>
#include <bedrock/protodef/ProtoDefReader.hpp>
#include <bedrock/generated/GeneratedProtocolTypes.hpp>

#include <cctype>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace bedrock {

class ProtoDefDecoder {
public:
    using TypeResolver = std::function<std::optional<std::string>(const std::string&)>;

    ProtoDefDecoder() = default;

    explicit ProtoDefDecoder(TypeResolver resolver)
        : resolver_(std::move(resolver)) {}

    void decode(
        const std::string& typeJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        const std::string type = trim(typeJson);

        if (isJsonString(type)) {
            decodeTypeName(unquote(type), reader, path, out, context);
            return;
        }

        if (startsWith(type, "[\"container\"")) {
            decodeContainer(type, reader, path, out, context);
            return;
        }

        if (startsWith(type, "[\"array\"")) {
            decodeArray(type, reader, path, out, context);
            return;
        }

        if (startsWith(type, "[\"mapper\"")) {
            decodeMapper(type, reader, path, out, context);
            return;
        }

        if (startsWith(type, "[\"switch\"")) {
            decodeSwitch(type, reader, path, out, context);
            return;
        }

        if (startsWith(type, "[\"option\"")) {
            decodeOption(type, reader, path, out, context);
            return;
        }

        if (startsWith(type, "[\"pstring\"")) {
            decodePString(type, reader, path, out, context);
            return;
        }

        if (startsWith(type, "[\"buffer\"")) {
            decodeBuffer(type, reader, path, out, context);
            return;
        }

        if (startsWith(type, "[\"encapsulated\"")) {
            decodeEncapsulated(type, reader, path, out, context);
            return;
        }

        if (type.rfind("[\"bitflags\"", 0) == 0) {
            decodeBitflags(type, reader, path, out, context);
            return;
        }

                if (type == "[\"enum_size_based_on_values_len\"]" || type == "enum_size_based_on_values_len") {
            auto valuesLen = context.get("values_len");
            if (valuesLen.empty()) {
                throw std::runtime_error("enum_size_based_on_values_len missing values_len");
            }

            uint64_t n = 0;
            try { n = std::stoull(valuesLen); } catch (...) { n = 0; }

            std::string enumType = "int";
            if (n <= 0xff) enumType = "byte";
            else if (n <= 0xffff) enumType = "short";

            ProtoDefField field;
            field.path = path;
            field.type = "enum_size_based_on_values_len";
            field.value = enumType;
            field.offset = reader.offset();
            field.size = 0;

            out.push_back(field);
            context.set(path, field.value);
            context.set("_enum_type", field.value);
            return;
        }

        throw std::runtime_error("ProtoDefDecoder unsupported type json: " + type);
    }

private:
    static std::string enumSizeBasedOnValuesLen(const ProtoDefContext& context) {
        std::string raw = context.get("values_len");

        if (raw.empty()) {
            // nested fallback: ищем любой ключ, который заканчивается на .values_len
            // Пока context публично не отдаёт все keys, поэтому честно падаем.
            throw std::runtime_error("enum_size_based_on_values_len missing values_len");
        }

        auto slash = raw.find('/');
        if (slash != std::string::npos) {
            raw = raw.substr(0, slash);
        }

        uint64_t n = static_cast<uint64_t>(std::stoull(raw));

        if (n <= 0xffULL) {
            return "byte";
        }

        if (n <= 0xffffULL) {
            return "short";
        }

        return "int";
    }


    static std::string uint128ToString(unsigned __int128 value) {
        if (value == 0) {
            return "0";
        }

        std::string out;
        while (value > 0) {
            int digit = static_cast<int>(value % 10);
            out.push_back(static_cast<char>('0' + digit));
            value /= 10;
        }

        std::reverse(out.begin(), out.end());
        return out;
    }

    static std::string readVarUInt128String(ProtoDefReader& reader) {
        unsigned __int128 result = 0;
        int shift = 0;

        for (int i = 0; i < 19; ++i) {
            uint8_t byte = reader.u8();

            unsigned __int128 part = static_cast<unsigned __int128>(byte & 0x7f);
            result |= (part << shift);

            if ((byte & 0x80) == 0) {
                return uint128ToString(result);
            }

            shift += 7;
            if (shift >= 128) {
                throw std::runtime_error("varint128 too long");
            }
        }

        throw std::runtime_error("varint128 too long");
    }


    static std::string readShortString(ProtoDefReader& reader) {
        uint16_t len = reader.u16le();
        std::string out;
        out.reserve(len);
        for (uint16_t i = 0; i < len; ++i) {
            out.push_back(static_cast<char>(reader.u8()));
        }
        return out;
    }

    static void skipLittleNbtPayloadOnly(ProtoDefReader& reader) {
        uint8_t tag = reader.u8();
        if (tag == 0) return;
        skipNbtPayload(reader, tag);
    }


    static void skipNbtPayload(ProtoDefReader& reader, uint8_t tag) {
        switch (tag) {
            case 0: return;
            case 1: reader.u8(); return;
            case 2: reader.u16le(); return;
            case 3: reader.zigzag32(); return;
            case 4: reader.zigzag64(); return;
            case 5: reader.skip(4); return;
            case 6: reader.skip(8); return;

            case 7: {
                int32_t n = reader.zigzag32();
                if (n < 0) throw std::runtime_error("negative nbt byteArray length");
                reader.skip(static_cast<std::size_t>(n));
                return;
            }

            case 8: {
                reader.string();
                return;
            }

            case 9: {
                uint8_t inner = reader.u8();
                int32_t n = reader.zigzag32();
                if (n < 0) throw std::runtime_error("negative nbt list length");
                for (int32_t i = 0; i < n; ++i) {
                    skipNbtPayload(reader, inner);
                }
                return;
            }

            case 10: {
                while (true) {
                    std::size_t before = reader.offset();
                    uint8_t inner = reader.u8();
                    if (inner == 0) return;

                    reader.rewindTo(before);
                    skipNativeNbt(reader);
                }
            }

            case 11: {
                int32_t n = reader.zigzag32();
                if (n < 0) throw std::runtime_error("negative nbt intArray length");
                for (int32_t i = 0; i < n; ++i) {
                    reader.zigzag32();
                }
                return;
            }

            case 12: {
                int32_t n = reader.zigzag32();
                if (n < 0) throw std::runtime_error("negative nbt longArray length");
                for (int32_t i = 0; i < n; ++i) {
                    reader.zigzag64();
                }
                return;
            }

            default:
                throw std::runtime_error("unsupported nbt tag: " + std::to_string(tag));
        }
    }

    static void skipNativeNbt(ProtoDefReader& reader) {
        uint8_t tag = reader.u8();
        if (tag == 0) return;

        // prismarine-nbt littleVarint: type + tagName + payload
        reader.string();

        skipNbtPayload(reader, tag);
    }


    void decodeEncapsulated(
        const std::string& encapsulatedJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        auto lengthType = readJsonStringField(encapsulatedJson, "lengthType").value_or("varint");

        const std::size_t start = reader.offset();

        int64_t length = 0;
        if (lengthType == "u8") {
            length = reader.u8();
        } else if (lengthType == "u16" || lengthType == "lu16") {
            length = reader.u16le();
        } else if (lengthType == "u32" || lengthType == "lu32") {
            length = reader.u32le();
        } else if (lengthType == "varint" || lengthType == "varuint") {
            length = reader.varuint32();
        } else if (lengthType == "li32") {
            length = reader.i32le();
        } else {
            throw std::runtime_error("encapsulated unsupported lengthType: " + lengthType);
        }

        if (length < 0) {
            throw std::runtime_error("encapsulated negative length");
        }

        reader.skip(static_cast<std::size_t>(length));

        ProtoDefField field;
        field.path = path.empty() ? "$encapsulated" : path;
        field.type = "encapsulated<" + lengthType + ">";
        field.value = "<encapsulated bytes:" + std::to_string(length) + ">";
        field.offset = start;
        field.size = reader.offset() - start;

        out.push_back(field);
        context.set(field.path, field.value);
    }


    void decodeBuffer(
        const std::string& bufferJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        auto countType = readJsonStringField(bufferJson, "countType").value_or("varint");

        const std::size_t start = reader.offset();

        int64_t count = 0;
        if (countType == "u8") {
            count = reader.u8();
        } else if (countType == "u16" || countType == "lu16" || countType == "li16") {
            count = reader.u16le();
        } else if (countType == "u32" || countType == "lu32") {
            count = reader.u32le();
        } else if (countType == "varint" || countType == "varuint") {
            count = reader.varuint32();
        } else if (countType == "li32") {
            count = reader.i32le();
        } else if (countType == "remaining") {
            count = reader.remaining();
        } else {
            throw std::runtime_error("buffer unsupported countType: " + countType);
        }

        if (count < 0) {
            throw std::runtime_error("buffer negative count");
        }

        reader.skip(static_cast<std::size_t>(count));

        ProtoDefField field;
        field.path = path.empty() ? "$buffer" : path;
        field.type = "buffer<" + countType + ">";
        field.value = "<Buffer bytes:" + std::to_string(count) + ">";
        field.offset = start;
        field.size = reader.offset() - start;

        out.push_back(field);
        context.set(field.path, field.value);
    }

    void decodePString(
        const std::string& stringJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        auto countType = readJsonStringField(stringJson, "countType").value_or("varint");
        auto encoding = readJsonStringField(stringJson, "encoding").value_or("utf8");

        const std::size_t start = reader.offset();

        int64_t count = 0;
        if (countType == "u8") {
            count = reader.u8();
        } else if (countType == "u16" || countType == "lu16") {
            count = reader.u16le();
        } else if (countType == "li16") {
            count = static_cast<int16_t>(reader.u16le());
        } else if (countType == "u32" || countType == "lu32") {
            count = reader.u32le();
        } else if (countType == "li32") {
            count = reader.i32le();
        } else if (countType == "varint" || countType == "varuint") {
            count = reader.varuint32();
        } else {
            throw std::runtime_error("pstring unsupported countType: " + countType);
        }

        if (count < 0) {
            throw std::runtime_error("pstring negative length");
        }

        std::string value;
        value.reserve(static_cast<std::size_t>(count));
        for (int64_t i = 0; i < count; ++i) {
            value.push_back(static_cast<char>(reader.u8()));
        }

        ProtoDefField field;
        field.path = path.empty() ? "$string" : path;
        field.type = encoding == "latin1" ? "pstring<latin1>" : "pstring";
        field.value = value;
        field.offset = start;
        field.size = reader.offset() - start;

        out.push_back(field);
        context.set(field.path, field.value);
    }


    void decodeOption(
        const std::string& optionJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        const std::size_t start = reader.offset();
        uint8_t present = 0;
        if (reader.remaining() < 1) {
            ProtoDefField field;
            field.path = path + ".$present";
            field.type = "option_present";
            field.value = "<missing:u8>";
            field.size = 0;
            out.push_back(field);
            return;
        }
        present = reader.u8();

        ProtoDefField flag;
        flag.path = path.empty() ? "$option" : path + ".$present";
        flag.type = "option_present";
        flag.value = present != 0 ? "true" : "false";
        flag.offset = start;
        flag.size = reader.offset() - start;
        out.push_back(flag);
        context.set(flag.path, flag.value);

        if (present == 0) {
            return;
        }

        auto innerType = readSecondElement(optionJson);
        if (!innerType.has_value()) {
            throw std::runtime_error("option inner type not found");
        }

        decode(*innerType, reader, path, out, context);
    }


    void decodeSwitch(
        const std::string& switchJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        auto compareToValue = readJsonStringField(switchJson, "compareToValue");

        std::string compareValue;
        if (compareToValue.has_value()) {
            compareValue = *compareToValue;
        } else {
            auto compareTo = readJsonStringField(switchJson, "compareTo");
            if (!compareTo.has_value()) {
                throw std::runtime_error("switch compareTo not found");
            }

            std::string comparePath = resolveComparePath(path, *compareTo);
            compareValue = context.get(comparePath);

            if (compareValue.empty() && compareTo->find("_enum_type") != std::string::npos) {
                compareValue = context.get("_enum_type");
            }

            if (compareValue.empty()) {
                compareValue = context.get(*compareTo);
            }

            if (compareValue.empty() && !path.empty()) {
                compareValue = context.get(path + "." + *compareTo);
            }
        }

        auto branch = findSwitchBranchType(switchJson, compareValue);
        if (!branch.has_value()) {
            branch = readJsonValueField(switchJson, "default");
        }

        if (!branch.has_value()) {
            ProtoDefField field;
            field.path = path.empty() ? "$switch" : path;
            field.type = "switch";
            field.value = "<no_branch:" + compareValue + ">";
            field.offset = reader.offset();
            field.size = 0;
            out.push_back(field);
            context.set(field.path, field.value);
            return;
        }

        decode(*branch, reader, path, out, context);
    }


    void decodeMapper(
        const std::string& mapperJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        auto baseType = readJsonStringField(mapperJson, "type");
        if (!baseType.has_value()) {
            throw std::runtime_error("mapper base type not found");
        }

        const std::size_t start = reader.offset();

        std::string numeric;
        if (*baseType == "u8") {
            uint8_t v = 0;
            if (!reader.tryU8(v)) {
                numeric = "<missing:u8>";
            } else {
                numeric = std::to_string(v);
            }
        } else if (*baseType == "u16" || *baseType == "lu16") {
            numeric = std::to_string(reader.u16le());
        } else if (*baseType == "u32" || *baseType == "lu32") {
            numeric = std::to_string(reader.u32le());
        } else if (*baseType == "i32" || *baseType == "li32") {
            numeric = std::to_string(reader.i32le());
        } else if (*baseType == "varint" || *baseType == "varuint") {
            numeric = std::to_string(reader.varuint32());
        } else if (*baseType == "varint64" || *baseType == "varuint64") {
            numeric = std::to_string(reader.varuint64());
        } else if (*baseType == "zigzag32") {
            numeric = std::to_string(reader.zigzag32());
        } else {
            throw std::runtime_error("mapper unsupported base type: " + *baseType);
        }

        std::string mapped = readMapperValue(mapperJson, numeric).value_or(numeric);

        ProtoDefField field;
        field.path = path.empty() ? "$value" : path;
        field.type = "mapper<" + *baseType + ">";
        field.value = numeric + "/" + mapped;
        field.offset = start;
        field.size = reader.offset() - start;

        out.push_back(field);

        // В context кладём именно mapped, как protodef switch сравнивает по enum name.
        context.set(field.path, mapped);
    }


    void decodeArray(
        const std::string& arrayJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        auto countRef = readJsonStringField(arrayJson, "count");
        auto countType = readJsonStringField(arrayJson, "countType").value_or("varint");
        auto itemType = readJsonValueField(arrayJson, "type");

        if (!itemType.has_value()) {
            throw std::runtime_error("array item type not found");
        }

        const std::size_t start = reader.offset();

        int64_t count = 0;
        bool countWasRead = false;

        if (countRef.has_value()) {
            std::string countPath = resolveComparePath(path, *countRef);
            std::string raw = context.get(countPath);
            if (raw.empty()) {
                raw = context.get(*countRef);
            }
            if (raw.empty()) {
                auto dot = countPath.find_last_of('.');
                if (dot != std::string::npos && dot + 1 < countPath.size()) {
                    raw = context.get(countPath.substr(dot + 1));
                }
            }
            if (raw.empty()) {
                throw std::runtime_error("array count field not found: " + countPath + " at path " + path);
            }

            auto slash = raw.find('/');
            if (slash != std::string::npos) {
                raw = raw.substr(0, slash);
            }

            count = std::stoll(raw);
        } else {
            countWasRead = true;

            if (countType == "u8") {
                if (reader.remaining() < 1) {
                    count = 0;
                } else {
                    count = reader.u8();
                }
            } else if (countType == "u16" || countType == "lu16" || countType == "li16") {
                count = reader.u16le();
            } else if (countType == "varint" || countType == "varuint") {
                count = reader.varuint32();
            } else if (countType == "li32") {
                count = reader.i32le();
            } else if (countType == "u32" || countType == "lu32") {
                count = reader.u32le();
            } else {
                throw std::runtime_error("array unsupported countType: " + countType);
            }
        }

        if (count < 0) {
            throw std::runtime_error("array negative count");
        }

        ProtoDefField countField;
        countField.path = path.empty() ? "$count" : path + ".$count";
        countField.type = countRef.has_value()
            ? "array_count_ref<" + *countRef + ">"
            : "array_count<" + countType + ">";
        countField.value = std::to_string(count);
        countField.offset = start;
        countField.size = countWasRead ? reader.offset() - start : 0;
        out.push_back(countField);
        context.set(countField.path, countField.value);

        for (int64_t i = 0; i < count; ++i) {
            std::string childPath = path + "[" + std::to_string(i) + "]";
            try {
                decode(*itemType, reader, childPath, out, context);
            } catch (const std::exception& e) {
                throw std::runtime_error("at " + childPath + ": " + e.what());
            }
        }
    }

    void decodeContainer(
        const std::string& containerJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        auto fields = readSecondElement(containerJson);
        if (!fields.has_value()) {
            throw std::runtime_error("container fields array not found");
        }
        if (!startsWith(trim(*fields), "[")) {
            throw std::runtime_error("container fields value is not an array");
        }

        std::size_t pos = 0;
        while (true) {
            auto objStart = fields->find('{', pos);
            if (objStart == std::string::npos) break;

            auto objEnd = findMatching(*fields, objStart, '{', '}');
            if (objEnd == std::string::npos) {
                throw std::runtime_error("container field object not closed");
            }

            std::string fieldObj = fields->substr(objStart, objEnd - objStart + 1);

            auto name = readJsonStringField(fieldObj, "name");
            auto type = readJsonValueField(fieldObj, "type");
            bool anon = readJsonBoolField(fieldObj, "anon").value_or(false);

            if (type.has_value()) {
                std::string childPath = path;

                if (!anon && name.has_value()) {
                    childPath = path.empty() ? *name : path + "." + *name;
                }

                try {
                    decode(*type, reader, childPath, out, context);
                } catch (const std::exception& e) {
                    throw std::runtime_error("at " + (childPath.empty() ? std::string("$container") : childPath) + ": " + e.what());
                }
            }

            pos = objEnd + 1;
        }
    }

    void decodeBitflags(
        const std::string& bitflagsJson,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        auto baseType = readJsonStringField(bitflagsJson, "type").value_or("lu32");

        ProtoDefField field;
        field.path = path;
        field.type = "bitflags";
        field.offset = reader.offset();

        uint64_t value = 0;
        if (baseType == "u8") value = reader.u8();
        else if (baseType == "u16" || baseType == "lu16" || baseType == "li16") value = reader.u16le();
        else if (baseType == "u32" || baseType == "lu32" || baseType == "li32") value = reader.u32le();
        else if (baseType == "lu64" || baseType == "li64") value = reader.readU64LE();
        else value = reader.varuint32();

        field.value = std::to_string(value);
        field.size = reader.offset() - field.offset;

        out.push_back(field);
        context.set(path, field.value);
    }

    void decodeTypeName(
        const std::string& typeName,
        ProtoDefReader& reader,
        const std::string& path,
        std::vector<ProtoDefField>& out,
        ProtoDefContext& context
    ) const {
        const std::size_t start = reader.offset();

        ProtoDefField field;
        field.path = path.empty() ? "$value" : path;
        field.type = typeName;
        field.offset = start;

        if (typeName == "void") {
            field.value = "<void>";
        } else if (typeName == "string") {
            field.value = reader.string();
        } else if (typeName == "ShortString") {
            field.value = readShortString(reader);
        } else if (typeName == "bool") {
            field.value = reader.boolean() ? "true" : "false";
        } else if (typeName == "uuid") {
            std::string hex = "";
            static const char* digits = "0123456789abcdef";
            for (int i = 0; i < 16; ++i) {
                uint8_t b = reader.u8();
                hex.push_back(digits[(b >> 4) & 0x0f]);
                hex.push_back(digits[b & 0x0f]);
            }
            field.value = hex;
        } else if (typeName == "u8" || typeName == "byte") {
            if (reader.remaining() < 1) {
                field.value = "<missing:u8>";
            } else {
                field.value = std::to_string(reader.u8());
            }
        } else if (typeName == "i8") {
            field.value = std::to_string(static_cast<int8_t>(reader.u8()));
        } else if (typeName == "u16" || typeName == "lu16" || typeName == "li16") {
            field.value = std::to_string(reader.u16le());
        } else if (typeName == "u32" || typeName == "lu32") {
            field.value = std::to_string(reader.u32le());
        } else if (typeName == "i32" || typeName == "li32") {
            field.value = std::to_string(reader.i32le());
        } else if (typeName == "u64" || typeName == "lu64") {
            field.value = std::to_string(reader.u64le());
        } else if (typeName == "i64" || typeName == "li64") {
            field.value = std::to_string(reader.i64le());
        } else if (
            typeName == "varuint64" ||
            typeName == "varlong" ||
            typeName == "entity_runtime_id" ||
            typeName == "actor_runtime_id" ||
            typeName == "runtime_entity_id"
        ) {
            field.value = std::to_string(reader.varuint64());
        } else if (typeName == "varint64") {
            field.value = std::to_string(reader.varint64());
        } else if (typeName == "varint" || typeName == "varuint") {
            if (
                path.find("runtime_id") != std::string::npos ||
                path.find("runtime_entity_id") != std::string::npos ||
                path.find("actor_runtime_id") != std::string::npos ||
                path.find("entity_runtime_id") != std::string::npos
            ) {
                field.value = std::to_string(reader.varuint64());
            } else {
                field.value = std::to_string(reader.varuint32());
            }
        } else if (typeName == "zigzag32") {
            field.value = std::to_string(reader.zigzag32());
        } else if (typeName == "zigzag64") {
            field.value = std::to_string(reader.zigzag64());
        } else if (typeName == "varint128") {
            field.value = readVarUInt128String(reader);
        } else if (typeName == "enum_size_based_on_values_len") {
            field.value = enumSizeBasedOnValuesLen(context);
        } else if (typeName == "f32" || typeName == "lf32") {
            field.value = std::to_string(reader.readF32LE());
        } else if (typeName == "f64" || typeName == "lf64") {
            field.value = std::to_string(reader.readF64LE());
        } else if (typeName == "vec3i") {
            field.value =
                std::to_string(reader.zigzag32()) + "," +
                std::to_string(reader.zigzag32()) + "," +
                std::to_string(reader.zigzag32());
        } else if (typeName == "vec3f") {
            field.value =
                std::to_string(reader.readF32LE()) + "," +
                std::to_string(reader.readF32LE()) + "," +
                std::to_string(reader.readF32LE());
        } else if (typeName == "native" || typeName == "nbt") {
            skipNativeNbt(reader);
            field.value = "<native>";
        } else if (typeName == "lnbt") {
            skipLittleNbtPayloadOnly(reader);
            field.value = "<lnbt>";
        } else {
            if (resolver_) {
                auto resolved = resolver_(typeName);
                if (resolved.has_value()) {
                    decode(*resolved, reader, path, out, context);
                    return;
                }
            }

            auto generated = bedrock::generatedProtocolTypeJson(typeName);
            if (generated.has_value()) {
                decode(*generated, reader, path, out, context);
                return;
            }

            throw std::runtime_error("ProtoDefDecoder unknown primitive type: " + typeName);
        }

        field.size = reader.offset() - start;

        out.push_back(field);
        context.set(field.path, field.value);
    }

    static std::optional<std::string> readSecondElement(const std::string& json) {
        auto firstComma = json.find(',');
        if (firstComma == std::string::npos) return std::nullopt;

        std::size_t a = firstComma + 1;
        while (a < json.size() && std::isspace(static_cast<unsigned char>(json[a]))) {
            ++a;
        }

        if (a >= json.size()) return std::nullopt;

        if (json[a] == '"') {
            auto b = json.find('"', a + 1);
            if (b == std::string::npos) return std::nullopt;
            return json.substr(a, b - a + 1);
        }

        if (json[a] == '[') {
            auto b = findMatching(json, a, '[', ']');
            if (b == std::string::npos) return std::nullopt;
            return json.substr(a, b - a + 1);
        }

        if (json[a] == '{') {
            auto b = findMatching(json, a, '{', '}');
            if (b == std::string::npos) return std::nullopt;
            return json.substr(a, b - a + 1);
        }

        return std::nullopt;
    }


    static std::string resolveComparePath(
        const std::string& currentPath,
        const std::string& compareTo
    ) {
        if (compareTo.rfind("../", 0) == 0) {
            std::string base = currentPath;
            auto dot = base.rfind('.');
            if (dot != std::string::npos) {
                base = base.substr(0, dot);
            } else {
                base.clear();
            }

            std::string rest = compareTo.substr(3);
            return base.empty() ? rest : base + "." + rest;
        }

        if (currentPath.empty()) {
            return compareTo;
        }

        auto dot = currentPath.rfind('.');
        if (dot == std::string::npos) {
            return compareTo;
        }

        return currentPath.substr(0, dot + 1) + compareTo;
    }

    static std::optional<std::string> findSwitchBranchType(
        const std::string& switchJson,
        const std::string& key
    ) {
        auto fields = readJsonValueField(switchJson, "fields");
        if (!fields.has_value()) return std::nullopt;

        for (const auto& candidate : switchLookupKeys(key)) {
            auto branch = readJsonValueField(*fields, candidate);
            if (branch.has_value()) return branch;
        }

        return std::nullopt;
    }

    static std::vector<std::string> switchLookupKeys(const std::string& value) {
        std::vector<std::string> keys;

        keys.push_back(value);

        auto slash = value.find('/');
        if (slash != std::string::npos && slash + 1 < value.size()) {
            keys.push_back(value.substr(slash + 1));
        }

        // protodef supports fields keys like "/TypeName"
        keys.push_back("/" + value);

        if (slash != std::string::npos && slash + 1 < value.size()) {
            keys.push_back("/" + value.substr(slash + 1));
        }

        return keys;
    }


    static std::optional<std::string> readMapperValue(
        const std::string& mapperJson,
        const std::string& numeric
    ) {
        auto mappings = readJsonValueField(mapperJson, "mappings");
        if (!mappings.has_value()) return std::nullopt;

        auto mapped = readJsonStringField(*mappings, numeric);
        if (mapped.has_value()) return mapped;

        auto quoted = readJsonValueField(*mappings, numeric);
        if (quoted.has_value() && isJsonString(trim(*quoted))) {
            return unquote(trim(*quoted));
        }

        return std::nullopt;
    }


    static std::optional<std::string> findSecondArray(const std::string& json) {
        auto first = json.find('[');
        if (first == std::string::npos) return std::nullopt;

        auto second = json.find('[', first + 1);
        if (second == std::string::npos) return std::nullopt;

        auto end = findMatching(json, second, '[', ']');
        if (end == std::string::npos) return std::nullopt;

        return json.substr(second, end - second + 1);
    }

    static std::optional<bool> readJsonBoolField(
        const std::string& json,
        const std::string& key
    ) {
        auto value = readJsonValueField(json, key);
        if (!value.has_value()) return std::nullopt;

        auto normalized = trim(*value);
        if (normalized == "true") return true;
        if (normalized == "false") return false;

        return std::nullopt;
    }


    static std::optional<std::string> readJsonStringField(
        const std::string& json,
        const std::string& key
    ) {
        auto value = readJsonValueField(json, key);
        if (!value.has_value()) return std::nullopt;

        auto normalized = trim(*value);
        if (!isJsonString(normalized)) return std::nullopt;
        return unquote(normalized);
    }

    static std::optional<std::string> readJsonValueField(
        const std::string& json,
        const std::string& key
    ) {
        std::string object = trim(json);
        auto objStart = object.find('{');
        if (objStart == std::string::npos) return std::nullopt;

        auto objEnd = findMatching(object, objStart, '{', '}');
        if (objEnd == std::string::npos) return std::nullopt;

        std::size_t pos = objStart + 1;
        while (pos < objEnd) {
            while (pos < objEnd && (std::isspace(static_cast<unsigned char>(object[pos])) || object[pos] == ',')) {
                ++pos;
            }

            if (pos >= objEnd) break;
            if (object[pos] != '"') {
                ++pos;
                continue;
            }

            auto keyEnd = findStringEnd(object, pos);
            if (keyEnd == std::string::npos || keyEnd >= objEnd) return std::nullopt;

            std::string currentKey = object.substr(pos + 1, keyEnd - pos - 1);
            pos = keyEnd + 1;

            while (pos < objEnd && std::isspace(static_cast<unsigned char>(object[pos]))) {
                ++pos;
            }

            if (pos >= objEnd || object[pos] != ':') return std::nullopt;
            ++pos;

            while (pos < objEnd && std::isspace(static_cast<unsigned char>(object[pos]))) {
                ++pos;
            }

            if (pos >= objEnd) return std::nullopt;

            auto valueEnd = findJsonValueEnd(object, pos, objEnd);
            if (valueEnd == std::string::npos) return std::nullopt;

            if (currentKey == key) {
                return object.substr(pos, valueEnd - pos);
            }

            pos = valueEnd;
        }

        return std::nullopt;
    }

    static std::size_t findStringEnd(const std::string& s, std::size_t open) {
        bool esc = false;
        for (std::size_t i = open + 1; i < s.size(); ++i) {
            if (esc) {
                esc = false;
                continue;
            }

            if (s[i] == '\\') {
                esc = true;
                continue;
            }

            if (s[i] == '"') {
                return i;
            }
        }

        return std::string::npos;
    }

    static std::size_t findJsonValueEnd(
        const std::string& s,
        std::size_t valueStart,
        std::size_t objectEnd
    ) {
        if (valueStart >= objectEnd) return std::string::npos;

        if (s[valueStart] == '"') {
            auto end = findStringEnd(s, valueStart);
            return end == std::string::npos ? std::string::npos : end + 1;
        }

        if (s[valueStart] == '[') {
            auto end = findMatching(s, valueStart, '[', ']');
            return end == std::string::npos ? std::string::npos : end + 1;
        }

        if (s[valueStart] == '{') {
            auto end = findMatching(s, valueStart, '{', '}');
            return end == std::string::npos ? std::string::npos : end + 1;
        }

        std::size_t pos = valueStart;
        while (pos < objectEnd && s[pos] != ',') {
            ++pos;
        }

        while (pos > valueStart && std::isspace(static_cast<unsigned char>(s[pos - 1]))) {
            --pos;
        }

        return pos;
    }

    static std::size_t findMatching(
        const std::string& s,
        std::size_t open,
        char left,
        char right
    ) {
        int depth = 0;
        bool inString = false;
        bool esc = false;

        for (std::size_t i = open; i < s.size(); ++i) {
            char c = s[i];

            if (inString) {
                if (esc) {
                    esc = false;
                } else if (c == '\\') {
                    esc = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }

            if (c == '"') {
                inString = true;
                continue;
            }

            if (c == left) ++depth;
            if (c == right) {
                --depth;
                if (depth == 0) return i;
            }
        }

        return std::string::npos;
    }

    static std::string trim(const std::string& s) {
        std::size_t a = 0;
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
            ++a;
        }

        std::size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
            --b;
        }

        return s.substr(a, b - a);
    }

    static bool startsWith(
        const std::string& s,
        const std::string& prefix
    ) {
        return s.rfind(prefix, 0) == 0;
    }

    static bool isJsonString(const std::string& s) {
        return s.size() >= 2 && s.front() == '"' && s.back() == '"';
    }

    static std::string unquote(const std::string& s) {
        if (!isJsonString(s)) return s;
        return s.substr(1, s.size() - 2);
    }

private:
    TypeResolver resolver_;
};

}
