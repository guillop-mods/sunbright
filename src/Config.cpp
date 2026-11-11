#include "Config.h"

namespace Helios
{
    namespace
    {
        constexpr auto kDefaultIniPath = "Data/SFSE/Plugins/DynamicSunAttenuation.ini";

        [[nodiscard]] AttenuationMode ParseAttenuationMode(std::int32_t a_raw) noexcept
        {
            switch (a_raw) {
            case 0:
                return AttenuationMode::InverseSquare;
            case 1:
                return AttenuationMode::Smoothstep;
            case 2:
            default:
                return AttenuationMode::Hybrid;
            }
        }

        [[nodiscard]] std::uint32_t ParseHexOrDecUint32(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, std::uint32_t a_default)
        {
            const char* raw = a_ini.GetValue(a_section, a_key, nullptr);
            if (!raw || raw[0] == '\0') {
                return a_default;
            }

            try {
                std::size_t idx = 0;
                const unsigned long parsed = std::stoul(raw, &idx, 0);
                return static_cast<std::uint32_t>(parsed);
            } catch (...) {
                return a_default;
            }
        }
    }

    Config* Config::GetSingleton() noexcept
    {
        static Config instance;
        return &instance;
    }

    void Config::Load()
    {
        CSimpleIniA ini;
        ini.SetUnicode();

        const SI_Error rc = ini.LoadFile(kDefaultIniPath);
        if (rc < 0) {
            logger::warn("No se encontro '{}'. Se utilizaran los valores por defecto.", kDefaultIniPath);
            return;
        }

        // [General]
        _general.enabled           = ini.GetBoolValue("General", "bEnabled", _general.enabled);
        _general.updateIntervalMs  = static_cast<float>(ini.GetDoubleValue("General", "fUpdateIntervalMs", _general.updateIntervalMs));
        _general.temporalSmoothing = static_cast<float>(ini.GetDoubleValue("General", "fTemporalSmoothing", _general.temporalSmoothing));
        _general.enableInSpaceOnly = ini.GetBoolValue("General", "bEnableInSpaceOnly", _general.enableInSpaceOnly);
        _general.debugLog          = ini.GetBoolValue("General", "bDebugLog", _general.debugLog);

        _general.updateIntervalMs  = std::clamp(_general.updateIntervalMs, 4.0f, 1000.0f);
        _general.temporalSmoothing = std::clamp(_general.temporalSmoothing, 0.01f, 1.0f);

        // [Attenuation]
        const auto rawMode = static_cast<std::int32_t>(ini.GetLongValue("Attenuation", "iAttenuationMode", static_cast<long>(_attenuation.mode)));
        _attenuation.mode  = ParseAttenuationMode(rawMode);

        _attenuation.referenceDistance        = static_cast<float>(ini.GetDoubleValue("Attenuation", "fReferenceDistance", _attenuation.referenceDistance));
        _attenuation.maxFalloffDistance       = static_cast<float>(ini.GetDoubleValue("Attenuation", "fMaxFalloffDistance", _attenuation.maxFalloffDistance));
        _attenuation.inverseSquareFactor      = static_cast<float>(ini.GetDoubleValue("Attenuation", "fInverseSquareFactor", _attenuation.inverseSquareFactor));
        _attenuation.maxOcclusionAmount       = static_cast<float>(ini.GetDoubleValue("Attenuation", "fMaxOcclusionAmount", _attenuation.maxOcclusionAmount));
        _attenuation.minSunGlareAlpha         = static_cast<float>(ini.GetDoubleValue("Attenuation", "fMinSunGlareAlpha", _attenuation.minSunGlareAlpha));
        _attenuation.minSunDiskScale          = static_cast<float>(ini.GetDoubleValue("Attenuation", "fMinSunDiskScale", _attenuation.minSunDiskScale));
        _attenuation.minDirectionalLightScale = static_cast<float>(ini.GetDoubleValue("Attenuation", "fMinDirectionalLightScale", _attenuation.minDirectionalLightScale));

        _attenuation.referenceDistance        = (std::max)(_attenuation.referenceDistance, 10.0f);
        _attenuation.maxFalloffDistance       = (std::max)(_attenuation.maxFalloffDistance, _attenuation.referenceDistance + 100.0f);
        _attenuation.inverseSquareFactor      = std::clamp(_attenuation.inverseSquareFactor, 0.01f, 25.0f);
        _attenuation.maxOcclusionAmount       = std::clamp(_attenuation.maxOcclusionAmount, 0.0f, 0.98f);
        _attenuation.minSunGlareAlpha         = std::clamp(_attenuation.minSunGlareAlpha, 0.01f, 1.0f);
        _attenuation.minSunDiskScale          = std::clamp(_attenuation.minSunDiskScale, 0.05f, 1.0f);
        _attenuation.minDirectionalLightScale = std::clamp(_attenuation.minDirectionalLightScale, 0.02f, 1.0f);

        // [SpaceDetection]
        _spaceDetection.starEditorIDToken   = ini.GetValue("SpaceDetection", "sStarEditorIDToken", _spaceDetection.starEditorIDToken.c_str());
        _spaceDetection.sunEditorIDToken    = ini.GetValue("SpaceDetection", "sSunEditorIDToken", _spaceDetection.sunEditorIDToken.c_str());
        _spaceDetection.starKeywordEditorID = ini.GetValue("SpaceDetection", "sStarKeywordEditorID", _spaceDetection.starKeywordEditorID.c_str());
        _spaceDetection.fallbackSystemScale = static_cast<float>(ini.GetDoubleValue("SpaceDetection", "fFallbackSystemScale", _spaceDetection.fallbackSystemScale));

        // [Hooks]
        _hooks.playerUpdateVfuncIdx     = ParseHexOrDecUint32(ini, "Hooks", "uPlayerUpdateVfuncIdx", _hooks.playerUpdateVfuncIdx);
        _hooks.useTaskFallbackWhenUnset = ini.GetBoolValue("Hooks", "bUseTaskFallbackWhenUnset", _hooks.useTaskFallbackWhenUnset);

        // [SkyOffsets]
        _skyOffsets.sunPtrOffset              = ParseHexOrDecUint32(ini, "SkyOffsets", "uSunPtrOffset", _skyOffsets.sunPtrOffset);
        _skyOffsets.sunBaseNodeOffset         = ParseHexOrDecUint32(ini, "SkyOffsets", "uSunBaseNodeOffset", _skyOffsets.sunBaseNodeOffset);
        _skyOffsets.sunGlareNodeOffset        = ParseHexOrDecUint32(ini, "SkyOffsets", "uSunGlareNodeOffset", _skyOffsets.sunGlareNodeOffset);
        _skyOffsets.sunDirectionalLightOffset = ParseHexOrDecUint32(ini, "SkyOffsets", "uSunDirectionalLightOffset", _skyOffsets.sunDirectionalLightOffset);
        _skyOffsets.sunGlareAlphaOffset       = ParseHexOrDecUint32(ini, "SkyOffsets", "uSunGlareAlphaOffset", _skyOffsets.sunGlareAlphaOffset);
        _skyOffsets.sunOcclusionOffset        = ParseHexOrDecUint32(ini, "SkyOffsets", "uSunOcclusionOffset", _skyOffsets.sunOcclusionOffset);

        // [Diagnostics]
        _diagnostics.dumpSkyPointersOnLoad = ini.GetBoolValue("Diagnostics", "bDumpSkyPointersOnLoad", _diagnostics.dumpSkyPointersOnLoad);
        _diagnostics.dumpSkyOnEnterSpace   = ini.GetBoolValue("Diagnostics", "bDumpSkyOnEnterSpace", _diagnostics.dumpSkyOnEnterSpace);
        _diagnostics.dumpHotkeyVK          = ParseHexOrDecUint32(ini, "Diagnostics", "uDumpHotkeyVK", _diagnostics.dumpHotkeyVK);
        _diagnostics.dumpPointerCount      = std::clamp(
            ParseHexOrDecUint32(ini, "Diagnostics", "uDumpPointerCount", _diagnostics.dumpPointerCount),
            1u,
            static_cast<std::uint32_t>(0xEB8 / sizeof(std::uintptr_t)));
        _diagnostics.inspectSunCandidates  = ini.GetBoolValue("Diagnostics", "bInspectSunCandidates", _diagnostics.inspectSunCandidates);

        logger::info(
            "Configuracion cargada: mode={}, d0={:.1f}, dMax={:.1f}, playerVfuncIdx={}, dumpCount={}, hotkeyVK=0x{:X}",
            static_cast<int>(_attenuation.mode),
            _attenuation.referenceDistance,
            _attenuation.maxFalloffDistance,
            _hooks.playerUpdateVfuncIdx,
            _diagnostics.dumpPointerCount,
            _diagnostics.dumpHotkeyVK);
    }

    void Config::Reload()
    {
        Load();
    }
}
