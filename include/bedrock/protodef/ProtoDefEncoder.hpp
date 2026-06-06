#pragma once

#include <bedrock/protodef/ProtoDefValue.hpp>
#include <bedrock/protodef/ProtoDefWriter.hpp>
#include <bedrock/generated/GeneratedProtocolTypes.hpp>

#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace bedrock {

class ProtoDefEncoder {
public:
    using TypeResolver = std::function<std::optional<std::string>(const std::string&)>;

    ProtoDefEncoder() = default;

    explicit ProtoDefEncoder(TypeResolver resolver)
        : resolver_(std::move(resolver)) {}

    void encode(
        const std::string& typeJson,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        const std::string type = trim(typeJson);

        if (isJsonString(type)) {
            encodeTypeName(unquote(type), value, writer);
            return;
        }

        if (startsWith(type, "[\"container\"")) {
            encodeContainer(type, value, writer);
            return;
        }

        if (startsWith(type, "[\"mapper\"")) {
            encodeMapper(type, value, writer);
            return;
        }

        if (startsWith(type, "[\"switch\"")) {
            encodeSwitch(type, value, writer);
            return;
        }

        if (startsWith(type, "[\"array\"")) {
            encodeArray(type, value, writer);
            return;
        }

        if (startsWith(type, "[\"option\"")) {
            encodeOption(type, value, writer);
            return;
        }

        if (startsWith(type, "[\"buffer\"")) {
            encodeBuffer(type, value, writer);
            return;
        }

        if (startsWith(type, "[\"encapsulated\"")) {
            encodeEncapsulated(type, value, writer);
            return;
        }

        throw std::runtime_error("ProtoDefEncoder unsupported type json: " + type);
    }

private:


    void encodeEncapsulated(
        const std::string& encapsulatedJson,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        auto innerType = readJsonValueField(encapsulatedJson, "type");
        if (!innerType.has_value()) {
            throw std::runtime_error("encapsulated encode inner type not found");
        }

        ProtoDefWriter innerWriter;
        encode(*innerType, value, innerWriter);

        auto payload = innerWriter.take();

        auto lengthType =
            readJsonStringField(encapsulatedJson, "lengthType")
                .value_or("varint");

        writeCount(lengthType, payload.size(), writer);
        writer.bytes(payload);
    }


    void encodeBuffer(
        const std::string& bufferJson,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        if (value.kind != ProtoDefValue::Kind::Bytes) {
            throw std::runtime_error("buffer encode expects bytes");
        }

        auto countRef = readJsonStringField(bufferJson, "count");

        if (!countRef.has_value()) {
            auto countType =
                readJsonStringField(bufferJson, "countType")
                    .value_or("varint");

            writeCount(
                countType,
                value.bytesValue.size(),
                writer
            );
        }

        writer.bytes(
            value.bytesValue.data(),
            value.bytesValue.size()
        );
    }


    void encodeOption(
        const std::string& optionJson,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        if (value.kind == ProtoDefValue::Kind::Null) {
            writer.u8(0);
            return;
        }

        writer.u8(1);

        auto innerType = readSecondElement(optionJson);
        if (!innerType.has_value()) {
            throw std::runtime_error("option encode inner type not found");
        }

        encode(*innerType, value, writer);
    }


    void encodeArray(
        const std::string& arrayJson,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        if (value.kind != ProtoDefValue::Kind::Array) {
            throw std::runtime_error("array encode expects array");
        }

        auto itemType = readJsonValueField(arrayJson, "type");
        if (!itemType.has_value()) {
            throw std::runtime_error("array encode item type not found");
        }

        // count:"field" означает, что count уже закодирован отдельным полем.
        auto countRef = readJsonStringField(arrayJson, "count");
        if (!countRef.has_value()) {
            auto countType = readJsonStringField(arrayJson, "countType").value_or("varint");
            writeCount(countType, value.arrayValue.size(), writer);
        }

        for (const auto& item : value.arrayValue) {
            encode(*itemType, item, writer);
        }
    }

    void writeCount(
        const std::string& countType,
        std::size_t count,
        ProtoDefWriter& writer
    ) const {
        if (countType == "u8" || countType == "lu8" || countType == "li8") {
            writer.u8(static_cast<uint8_t>(count));
            return;
        }

        if (countType == "u16" || countType == "lu16" || countType == "li16") {
            writer.u16le(static_cast<uint16_t>(count));
            return;
        }

        if (countType == "u32" || countType == "lu32" || countType == "li32") {
            writer.u32le(static_cast<uint32_t>(count));
            return;
        }

        if (countType == "varint" || countType == "varuint") {
            writer.varuint32(static_cast<uint32_t>(count));
            return;
        }

        throw std::runtime_error("array encode unsupported countType: " + countType);
    }


    void encodeSwitch(
        const std::string& switchJson,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        if (value.kind != ProtoDefValue::Kind::Object) {
            throw std::runtime_error("switch encode expects object context");
        }

        std::string compareValue;

        auto compareToValue = readJsonStringField(switchJson, "compareToValue");
        if (compareToValue.has_value()) {
            compareValue = *compareToValue;
        } else {
            auto compareTo = readJsonStringField(switchJson, "compareTo");
            if (!compareTo.has_value()) {
                throw std::runtime_error("switch encode compareTo not found");
            }

            const ProtoDefValue* compareField = value.get(*compareTo);
            if (!compareField) {
                throw std::runtime_error("switch encode missing compare field: " + *compareTo);
            }

            compareValue = valueToSwitchKey(*compareField);
        }

        auto branch = findSwitchBranchType(switchJson, compareValue);
        if (!branch.has_value()) {
            branch = readJsonValueField(switchJson, "default");
        }

        if (!branch.has_value()) {
            throw std::runtime_error("switch encode no branch for: " + compareValue);
        }

        if (const ProtoDefValue* directValue = value.get("$value")) {
            encode(*branch, *directValue, writer);
        } else {
            encode(*branch, value, writer);
        }
    }


    void encodeMapper(
        const std::string& mapperJson,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        auto baseType = readJsonStringField(mapperJson, "type");
        if (!baseType.has_value()) {
            throw std::runtime_error("mapper encode base type not found");
        }

        uint64_t numeric = 0;

        if (value.kind == ProtoDefValue::Kind::String) {
            auto found = findMapperNumericByName(mapperJson, value.stringValue);
            if (!found.has_value()) {
                throw std::runtime_error("mapper encode unknown mapped value: " + value.stringValue);
            }
            numeric = *found;
        } else {
            numeric = asUInt(value);
        }

        encodeTypeName(*baseType, ProtoDefValue::uinteger(numeric), writer);
    }


    void encodeContainer(
        const std::string& containerJson,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        if (value.kind != ProtoDefValue::Kind::Object) {
            throw std::runtime_error("container encode expects object");
        }

        auto fields = findSecondArray(containerJson);
        if (!fields.has_value()) {
            throw std::runtime_error("container fields array not found");
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

            if (!type.has_value()) {
                pos = objEnd + 1;
                continue;
            }

            if (anon) {
                encode(*type, value, writer);
            } else {
                if (!name.has_value()) {
                    throw std::runtime_error("container field missing name");
                }

                std::string normalizedType = trim(*type);

                if (startsWith(normalizedType, "[\"switch\"")) {
                    ProtoDefValue switchContext = value;

                    if (const ProtoDefValue* child = value.get(*name)) {
                        switchContext.objectValue["$value"] = *child;
                    }

                    encode(*type, switchContext, writer);
                } else {
                    const ProtoDefValue* child = value.get(*name);
                    if (!child) {
                        throw std::runtime_error("container missing field: " + *name);
                    }

                    encode(*type, *child, writer);
                }
            }

            pos = objEnd + 1;
        }
    }

    void encodeTypeName(
        const std::string& typeName,
        const ProtoDefValue& value,
        ProtoDefWriter& writer
    ) const {
        if (typeName == "void") {
            return;
        }

        if (typeName == "string") {
            writer.string(asString(value));
            return;
        }

        if (typeName == "ShortString") {
            writer.shortString(asString(value));
            return;
        }

        if (typeName == "native" || typeName == "nbt") {
            encodeNativeNbt(value, writer, true);
            return;
        }

        if (typeName == "lnbt") {
            encodeNativeNbt(value, writer, false);
            return;
        }

        if (typeName == "bool") {
            writer.boolValue(asBool(value));
            return;
        }

        if (typeName == "u8" || typeName == "lu8" || typeName == "li8") {
            writer.u8(static_cast<uint8_t>(asUInt(value)));
            return;
        }

        if (typeName == "u16" || typeName == "lu16" || typeName == "li16") {
            writer.u16le(static_cast<uint16_t>(asUInt(value)));
            return;
        }

        if (typeName == "u32" || typeName == "lu32") {
            writer.u32le(static_cast<uint32_t>(asUInt(value)));
            return;
        }

        if (typeName == "lu64") {
            writer.u64le(static_cast<uint64_t>(asUInt(value)));
            return;
        }

        if (typeName == "i32" || typeName == "li32") {
            writer.u32le(static_cast<uint32_t>(asInt(value)));
            return;
        }

        if (typeName == "li64") {
            writer.u64le(static_cast<uint64_t>(asInt(value)));
            return;
        }

        if (typeName == "varint" || typeName == "varuint") {
            writer.varuint32(static_cast<uint32_t>(asUInt(value)));
            return;
        }

        if (typeName == "zigzag32") {
            writer.zigzag32(static_cast<int32_t>(asInt(value)));
            return;
        }

        if (typeName == "zigzag64") {
            writer.zigzag64(static_cast<int64_t>(asInt(value)));
            return;
        }

        if (typeName == "varint128") {
            writer.varuint128(static_cast<unsigned __int128>(asUInt(value)));
            return;
        }

        if (typeName == "lf32" || typeName == "f32") {
            writer.f32le(static_cast<float>(asDouble(value)));
            return;
        }

        if (typeName == "lf64" || typeName == "f64") {
            writer.f64le(static_cast<double>(asDouble(value)));
            return;
        }

        if (resolver_) {
            auto resolved = resolver_(typeName);
            if (resolved.has_value()) {
                encode(*resolved, value, writer);
                return;
            }
        }

        auto generated = bedrock::generatedProtocolTypeJson(typeName);
        if (generated.has_value()) {
            encode(*generated, value, writer);
            return;
        }

        throw std::runtime_error("ProtoDefEncoder unknown primitive type: " + typeName);
    }


    static const ProtoDefValue* requiredField(
        const ProtoDefValue& object,
        const std::string& key
    ) {
        const ProtoDefValue* v = object.get(key);
        if (!v) {
            throw std::runtime_error("missing object field: " + key);
        }
        return v;
    }

    static std::string nbtTagName(const ProtoDefValue& node) {
        return asString(*requiredField(node, "tag"));
    }

    static uint8_t nbtTagIdByName(const std::string& tag) {
        if (tag == "end") return 0;
        if (tag == "byte") return 1;
        if (tag == "short") return 2;
        if (tag == "int") return 3;
        if (tag == "long") return 4;
        if (tag == "float") return 5;
        if (tag == "double") return 6;
        if (tag == "byteArray") return 7;
        if (tag == "string") return 8;
        if (tag == "list") return 9;
        if (tag == "compound") return 10;
        if (tag == "intArray") return 11;
        if (tag == "longArray") return 12;

        throw std::runtime_error("unknown nbt tag: " + tag);
    }

    static void writeNbtName(
        ProtoDefWriter& writer,
        const std::string& name
    ) {
        writer.string(name);
    }

    static void encodeNativeNbt(
        const ProtoDefValue& value,
        ProtoDefWriter& writer,
        bool withRootName
    ) {
        if (value.kind == ProtoDefValue::Kind::Bytes) {
            writer.bytes(value.bytesValue);
            return;
        }

        if (value.kind == ProtoDefValue::Kind::Null) {
            writer.u8(0);
            return;
        }

        if (value.kind != ProtoDefValue::Kind::Object) {
            throw std::runtime_error("nbt encode expects object/bytes/null");
        }

        std::string tag = nbtTagName(value);
        uint8_t tagId = nbtTagIdByName(tag);

        writer.u8(tagId);

        if (tagId == 0) {
            return;
        }

        if (withRootName) {
            std::string name;
            if (const ProtoDefValue* n = value.get("name")) {
                name = asString(*n);
            }
            writeNbtName(writer, name);
        }

        const ProtoDefValue& payload = *requiredField(value, "value");
        encodeNbtPayload(tagId, payload, writer);
    }

    static void encodeNbtPayload(
        uint8_t tagId,
        const ProtoDefValue& payload,
        ProtoDefWriter& writer
    ) {
        switch (tagId) {
            case 0:
                return;

            case 1:
                writer.u8(static_cast<uint8_t>(asInt(payload)));
                return;

            case 2:
                writer.u16le(static_cast<uint16_t>(asInt(payload)));
                return;

            case 3:
                writer.zigzag32(static_cast<int32_t>(asInt(payload)));
                return;

            case 4:
                writer.zigzag64(static_cast<int64_t>(asInt(payload)));
                return;

            case 5:
                writer.f32le(static_cast<float>(asDouble(payload)));
                return;

            case 6:
                writer.f64le(static_cast<double>(asDouble(payload)));
                return;

            case 7: {
                if (payload.kind != ProtoDefValue::Kind::Bytes) {
                    throw std::runtime_error("nbt byteArray expects bytes");
                }

                writer.zigzag32(static_cast<int32_t>(payload.bytesValue.size()));
                writer.bytes(payload.bytesValue);
                return;
            }

            case 8:
                writer.string(asString(payload));
                return;

            case 9: {
                if (payload.kind != ProtoDefValue::Kind::Object) {
                    throw std::runtime_error("nbt list expects object");
                }

                std::string childTagName = asString(*requiredField(payload, "childTag"));
                uint8_t childTag = nbtTagIdByName(childTagName);

                const ProtoDefValue& values = *requiredField(payload, "value");
                if (values.kind != ProtoDefValue::Kind::Array) {
                    throw std::runtime_error("nbt list value expects array");
                }

                writer.u8(childTag);
                writer.zigzag32(static_cast<int32_t>(values.arrayValue.size()));

                for (const auto& item : values.arrayValue) {
                    encodeNbtPayload(childTag, item, writer);
                }

                return;
            }

            case 10: {
                if (payload.kind != ProtoDefValue::Kind::Object) {
                    throw std::runtime_error("nbt compound expects object");
                }

                for (const auto& kv : payload.objectValue) {
                    const std::string& childName = kv.first;
                    const ProtoDefValue& child = kv.second;

                    if (child.kind != ProtoDefValue::Kind::Object) {
                        throw std::runtime_error("nbt compound child expects object: " + childName);
                    }

                    std::string childTagName = nbtTagName(child);
                    uint8_t childTag = nbtTagIdByName(childTagName);

                    writer.u8(childTag);
                    if (childTag == 0) {
                        continue;
                    }

                    writeNbtName(writer, childName);
                    encodeNbtPayload(
                        childTag,
                        *requiredField(child, "value"),
                        writer
                    );
                }

                writer.u8(0);
                return;
            }

            case 11: {
                if (payload.kind != ProtoDefValue::Kind::Array) {
                    throw std::runtime_error("nbt intArray expects array");
                }

                writer.zigzag32(static_cast<int32_t>(payload.arrayValue.size()));
                for (const auto& item : payload.arrayValue) {
                    writer.zigzag32(static_cast<int32_t>(asInt(item)));
                }
                return;
            }

            case 12: {
                if (payload.kind != ProtoDefValue::Kind::Array) {
                    throw std::runtime_error("nbt longArray expects array");
                }

                writer.zigzag32(static_cast<int32_t>(payload.arrayValue.size()));
                for (const auto& item : payload.arrayValue) {
                    writer.zigzag64(static_cast<int64_t>(asInt(item)));
                }
                return;
            }

            default:
                throw std::runtime_error("unsupported nbt tag id");
        }
    }


    static bool asBool(const ProtoDefValue& v) {
        if (v.kind == ProtoDefValue::Kind::Bool) return v.boolValue;
        if (v.kind == ProtoDefValue::Kind::Int) return v.intValue != 0;
        if (v.kind == ProtoDefValue::Kind::UInt) return v.uintValue != 0;
        throw std::runtime_error("expected bool-compatible value");
    }

    static int64_t asInt(const ProtoDefValue& v) {
        if (v.kind == ProtoDefValue::Kind::Int) return v.intValue;
        if (v.kind == ProtoDefValue::Kind::UInt) return static_cast<int64_t>(v.uintValue);
        if (v.kind == ProtoDefValue::Kind::Bool) return v.boolValue ? 1 : 0;
        throw std::runtime_error("expected int-compatible value");
    }

    static uint64_t asUInt(const ProtoDefValue& v) {
        if (v.kind == ProtoDefValue::Kind::UInt) return v.uintValue;
        if (v.kind == ProtoDefValue::Kind::Int) return static_cast<uint64_t>(v.intValue);
        if (v.kind == ProtoDefValue::Kind::Bool) return v.boolValue ? 1 : 0;
        throw std::runtime_error("expected uint-compatible value");
    }

    static double asDouble(const ProtoDefValue& v) {
        if (v.kind == ProtoDefValue::Kind::Int) return static_cast<double>(v.intValue);
        if (v.kind == ProtoDefValue::Kind::UInt) return static_cast<double>(v.uintValue);
        if (v.kind == ProtoDefValue::Kind::Bool) return v.boolValue ? 1.0 : 0.0;
        throw std::runtime_error("expected numeric value");
    }

    static const std::string& asString(const ProtoDefValue& v) {
        if (v.kind != ProtoDefValue::Kind::String) {
            throw std::runtime_error("expected string value");
        }
        return v.stringValue;
    }

    static std::string valueToSwitchKey(const ProtoDefValue& value) {
        if (value.kind == ProtoDefValue::Kind::String) {
            return value.stringValue;
        }

        if (value.kind == ProtoDefValue::Kind::UInt) {
            return std::to_string(value.uintValue);
        }

        if (value.kind == ProtoDefValue::Kind::Int) {
            return std::to_string(value.intValue);
        }

        if (value.kind == ProtoDefValue::Kind::Bool) {
            return value.boolValue ? "true" : "false";
        }

        throw std::runtime_error("switch compare value unsupported kind");
    }

    static std::optional<std::string> findSwitchBranchType(
        const std::string& switchJson,
        const std::string& key
    ) {
        std::string fieldsNeedle = "\"fields\"";
        auto f = switchJson.find(fieldsNeedle);
        if (f == std::string::npos) return std::nullopt;

        auto objStart = switchJson.find('{', f + fieldsNeedle.size());
        if (objStart == std::string::npos) return std::nullopt;

        auto objEnd = findMatching(switchJson, objStart, '{', '}');
        if (objEnd == std::string::npos) return std::nullopt;

        std::string obj = switchJson.substr(objStart, objEnd - objStart + 1);

        for (const auto& candidate : switchLookupKeys(key)) {
            std::string quoted = "\"" + candidate + "\"";
            auto k = obj.find(quoted);
            if (k == std::string::npos) continue;

            auto colon = obj.find(':', k + quoted.size());
            if (colon == std::string::npos) continue;

            std::size_t a = colon + 1;
            while (a < obj.size() && std::isspace(static_cast<unsigned char>(obj[a]))) {
                ++a;
            }

            if (a >= obj.size()) continue;

            if (obj[a] == '"') {
                auto b = obj.find('"', a + 1);
                if (b == std::string::npos) continue;
                return obj.substr(a, b - a + 1);
            }

            if (obj[a] == '[') {
                auto b = findMatching(obj, a, '[', ']');
                if (b == std::string::npos) continue;
                return obj.substr(a, b - a + 1);
            }

            if (obj[a] == '{') {
                auto b = findMatching(obj, a, '{', '}');
                if (b == std::string::npos) continue;
                return obj.substr(a, b - a + 1);
            }
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

        keys.push_back("/" + value);

        if (slash != std::string::npos && slash + 1 < value.size()) {
            keys.push_back("/" + value.substr(slash + 1));
        }

        return keys;
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


    static std::optional<uint64_t> findMapperNumericByName(
        const std::string& mapperJson,
        const std::string& mappedName
    ) {
        std::string mappingsNeedle = "\"mappings\"";
        auto m = mapperJson.find(mappingsNeedle);
        if (m == std::string::npos) return std::nullopt;

        auto objStart = mapperJson.find('{', m + mappingsNeedle.size());
        if (objStart == std::string::npos) return std::nullopt;

        auto objEnd = findMatching(mapperJson, objStart, '{', '}');
        if (objEnd == std::string::npos) return std::nullopt;

        std::string obj = mapperJson.substr(objStart, objEnd - objStart + 1);

        std::size_t pos = 0;
        while (true) {
            auto k1 = obj.find('"', pos);
            if (k1 == std::string::npos) break;

            auto k2 = obj.find('"', k1 + 1);
            if (k2 == std::string::npos) break;

            std::string key = obj.substr(k1 + 1, k2 - k1 - 1);

            auto colon = obj.find(':', k2 + 1);
            if (colon == std::string::npos) break;

            auto v1 = obj.find('"', colon + 1);
            if (v1 == std::string::npos) break;

            auto v2 = obj.find('"', v1 + 1);
            if (v2 == std::string::npos) break;

            std::string val = obj.substr(v1 + 1, v2 - v1 - 1);

            if (val == mappedName) {
                return static_cast<uint64_t>(std::stoull(key));
            }

            pos = v2 + 1;
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

    static std::optional<std::string> readJsonStringField(
        const std::string& json,
        const std::string& key
    ) {
        std::string needle = "\"" + key + "\"";
        auto p = json.find(needle);
        if (p == std::string::npos) return std::nullopt;

        auto colon = json.find(':', p + needle.size());
        if (colon == std::string::npos) return std::nullopt;

        auto q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos) return std::nullopt;

        auto q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos) return std::nullopt;

        return json.substr(q1 + 1, q2 - q1 - 1);
    }

    static std::optional<bool> readJsonBoolField(
        const std::string& json,
        const std::string& key
    ) {
        std::string needle = "\"" + key + "\"";
        auto p = json.find(needle);
        if (p == std::string::npos) return std::nullopt;

        auto colon = json.find(':', p + needle.size());
        if (colon == std::string::npos) return std::nullopt;

        std::size_t a = colon + 1;
        while (a < json.size() && std::isspace(static_cast<unsigned char>(json[a]))) {
            ++a;
        }

        if (json.compare(a, 4, "true") == 0) return true;
        if (json.compare(a, 5, "false") == 0) return false;

        return std::nullopt;
    }

    static std::optional<std::string> readJsonValueField(
        const std::string& json,
        const std::string& key
    ) {
        std::string needle = "\"" + key + "\"";
        auto p = json.find(needle);
        if (p == std::string::npos) return std::nullopt;

        auto colon = json.find(':', p + needle.size());
        if (colon == std::string::npos) return std::nullopt;

        std::size_t a = colon + 1;
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
        while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;

        std::size_t b = s.size();
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;

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
