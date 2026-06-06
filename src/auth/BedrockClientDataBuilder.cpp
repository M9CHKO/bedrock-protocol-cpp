#include <bedrock/auth/BedrockClientDataBuilder.hpp>

#include <string>

namespace bedrock {

static std::string escapeJson(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string BedrockClientDataBuilder::buildClassicSkinClientData(
    const BedrockClientDataOptions& options
) {
    const std::string skinData(64 * 32 * 4, 'A');

    return
        "{"
        "\"AnimatedImageData\":[{"
            "\"AnimationExpression\":0,"
            "\"Frames\":0,"
            "\"Image\":\"\","
            "\"ImageHeight\":0,"
            "\"ImageWidth\":0,"
            "\"Type\":0"
        "}],"
        "\"ArmSize\":\"wide\","
        "\"CapeData\":\"\","
        "\"CapeId\":\"\","
        "\"CapeImageHeight\":0,"
        "\"CapeImageWidth\":0,"
        "\"CapeOnClassicSkin\":false,"
        "\"ClientRandomId\":1,"
        "\"CompatibleWithClientSideChunkGen\":false,"
        "\"CurrentInputMode\":1,"
        "\"DefaultInputMode\":1,"
        "\"DeviceId\":\"" + escapeJson(options.deviceId) + "\","
        "\"DeviceModel\":\"CppBot\","
        "\"DeviceOS\":7,"
        "\"GameVersion\":\"" + escapeJson(options.gameVersion) + "\","
        "\"GraphicsMode\":0,"
        "\"GuiScale\":-1,"
        "\"IsEditorMode\":false,"
        "\"LanguageCode\":\"en_GB\","
        "\"MaxViewDistance\":0,"
        "\"MemoryTier\":0,"
        "\"OverrideSkin\":false,"
        "\"PersonaPieces\":[{"
            "\"IsDefault\":false,"
            "\"PackId\":\"\","
            "\"PieceId\":\"\","
            "\"PieceType\":\"\","
            "\"ProductId\":\"\","
            "\"Type\":\"\""
        "}],"
        "\"PersonaSkin\":false,"
        "\"PieceTintColors\":[{"
            "\"Colors\":[],"
            "\"PieceType\":\"\""
        "}],"
        "\"PlatformOfflineId\":\"\","
        "\"PlatformOnlineId\":\"\","
        "\"PlatformType\":0,"
        "\"PlayFabId\":\"\","
        "\"PremiumSkin\":false,"
        "\"SelfSignedId\":\"" + escapeJson(options.deviceId) + "\","
        "\"ServerAddress\":\"" + escapeJson(options.serverAddress) + "\","
        "\"SkinAnimationData\":\"\","
        "\"SkinColor\":\"#0\","
        "\"SkinData\":\"" + skinData + "\","
        "\"SkinGeometryData\":\"{\\\"geometry\\\":{\\\"default\\\":\\\"geometry.humanoid.custom\\\"}}\","
        "\"SkinGeometryDataEngineVersion\":\"1.12.0\","
        "\"SkinId\":\"Standard_Custom\","
        "\"SkinImageHeight\":32,"
        "\"SkinImageWidth\":64,"
        "\"SkinResourcePatch\":\"{\\\"geometry\\\":{\\\"default\\\":\\\"geometry.humanoid.custom\\\"}}\","
        "\"TrustedSkin\":false,"
        "\"ThirdPartyName\":\"" + escapeJson(options.displayName) + "\","
        "\"UIProfile\":0"
        "}";
}

} // namespace bedrock
