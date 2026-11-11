#pragma once

#include <cstdint>
#include <string>

namespace Helios
{
    enum class AttenuationMode : std::int32_t
    {
        InverseSquare = 0,
        Smoothstep    = 1,
        Hybrid        = 2
    };

    struct GeneralSettings
    {
        bool  enabled{ true };
        float updateIntervalMs{ 16.0f };     // ~60 Hz por defecto para suavidad orbital
        float temporalSmoothing{ 0.14f };    // Amortiguamiento exponencial entre fotogramas
        bool  enableInSpaceOnly{ true };     // Activo solo en celdas espaciales / exteriores
        bool  debugLog{ false };
    };

    struct AttenuationSettings
    {
        AttenuationMode mode{ AttenuationMode::Hybrid };
        float referenceDistance{ 2500.0f };       // d0: distancia donde el brillo comienza a caer (unidades CE2)
        float maxFalloffDistance{ 85000.0f };     // dMax: distancia de atenuacion maxima
        float inverseSquareFactor{ 1.25f };       // k: sensibilidad de ley del inverso del cuadrado
        float maxOcclusionAmount{ 0.88f };        // Oclusion maxima [0.0..0.98] para no apagar la estrella por completo
        float minSunGlareAlpha{ 0.12f };          // Alpha minimo del halo/glare del sol
        float minSunDiskScale{ 0.32f };           // Escala minima del disco solar en lejania
        float minDirectionalLightScale{ 0.18f };  // Multiplicador minimo de luz direccional sobre la nave
    };

    struct SpaceDetectionSettings
    {
        std::string starEditorIDToken{ "Star" };
        std::string sunEditorIDToken{ "Sun" };
        std::string starKeywordEditorID{ "LocTypeStar" };
        float       fallbackSystemScale{ 50000.0f };
    };

    struct HookSettings
    {
        // Indice de funcion virtual en RE::PlayerCharacter::VTABLE[0] (ej. Actor::Update).
        // Si es 0, el plugin usa el modo seguro sin vtable hardcoded: TaskInterface + BSInputDeviceManager.
        std::uint32_t playerUpdateVfuncIdx{ 0 };
        bool          useTaskFallbackWhenUnset{ true };
    };

    struct SkyOffsetSettings
    {
        // Offsets dentro de RE::Sky::pad[0xEB8] y dentro del objeto Sun descubierto via RTTI dump.
        // Valor 0 = no calibrado todavia (solo se calculan metricas y se registra diagnostico).
        std::uint32_t sunPtrOffset{ 0 };              // Offset en RE::Sky hacia Sun*
        std::uint32_t sunBaseNodeOffset{ 0 };         // Offset en Sun hacia NiBillboardNode/NiNode*
        std::uint32_t sunGlareNodeOffset{ 0 };        // Offset en Sun hacia glare NiNode*
        std::uint32_t sunDirectionalLightOffset{ 0 }; // Offset en Sun (o Sky si sunPtrOffset==0) hacia NiDirectionalLight*
        std::uint32_t sunGlareAlphaOffset{ 0 };       // Offset float dentro de Sun (o Sky) para Sun Glare Alpha
        std::uint32_t sunOcclusionOffset{ 0 };        // Offset float dentro de Sun (o Sky) para oclusion global
    };

    struct DiagnosticSettings
    {
        bool          dumpSkyPointersOnLoad{ false };   // Vuelca pad[0xEB8] al cargar partida (kPostLoadGame)
        bool          dumpSkyOnEnterSpace{ true };      // Vuelca una vez al detectar la primera celda espacial
        std::uint32_t dumpHotkeyVK{ 0x78 };             // Tecla virtual Windows (0x78 = VK_F9, 0 = desactivado)
        std::uint32_t dumpPointerCount{ 64 };           // Numero de punteros de 8 bytes a inspeccionar en RE::Sky
        bool          inspectSunCandidates{ true };     // Inspecciona 32 punteros hijos de cualquier candidato Sun/Light
    };

    class Config final
    {
    public:
        static Config* GetSingleton() noexcept;

        void Load();
        void Reload();

        [[nodiscard]] const GeneralSettings&        GetGeneral() const noexcept { return _general; }
        [[nodiscard]] const AttenuationSettings&    GetAttenuation() const noexcept { return _attenuation; }
        [[nodiscard]] const SpaceDetectionSettings& GetSpaceDetection() const noexcept { return _spaceDetection; }
        [[nodiscard]] const HookSettings&           GetHooks() const noexcept { return _hooks; }
        [[nodiscard]] const SkyOffsetSettings&      GetSkyOffsets() const noexcept { return _skyOffsets; }
        [[nodiscard]] const DiagnosticSettings&     GetDiagnostics() const noexcept { return _diagnostics; }

    private:
        Config() = default;
        ~Config() = default;
        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        GeneralSettings        _general{};
        AttenuationSettings    _attenuation{};
        SpaceDetectionSettings _spaceDetection{};
        HookSettings           _hooks{};
        SkyOffsetSettings      _skyOffsets{};
        DiagnosticSettings     _diagnostics{};
    };
}
