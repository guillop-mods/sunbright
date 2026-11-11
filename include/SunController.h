#pragma once

#include "Config.h"
#include "MathUtils.h"

namespace Helios
{
    class SunController final
    {
    public:
        static SunController* GetSingleton() noexcept;

        // Invocado desde el hook de PlayerCharacter::VTABLE o desde TaskInterface en el hilo principal
        void OnPlayerFrameTick(RE::PlayerCharacter* a_player, RE::Sky* a_sky, float a_deltaSeconds);

        // Solicita un volcado RTTI inmediato de RE::Sky::pad[0xEB8] en el hilo principal
        void TriggerSkyDump(const char* a_reason);

        void ResetState() noexcept;

        [[nodiscard]] Math::SunAttenuationResult GetCurrentState() const noexcept;
        [[nodiscard]] bool IsActiveInSpace() const noexcept { return _activeInSpace.load(); }

    private:
        SunController() = default;
        ~SunController() = default;
        SunController(const SunController&) = delete;
        SunController& operator=(const SunController&) = delete;

        [[nodiscard]] bool IsPlayerInSpaceCell(
            RE::PlayerCharacter* a_player,
            RE::TESObjectCELL*& a_outCell,
            RE::TESObjectREFR*& a_outShip) const;

        [[nodiscard]] std::optional<RE::NiPoint3> ResolveCentralStarPosition(
            RE::TESObjectCELL* a_spaceCell,
            RE::Sky* a_sky,
            const RE::NiPoint3& a_shipPos);

        void ApplyModulationToSky(RE::Sky* a_sky, const Math::SunAttenuationResult& a_state);
        void RestoreNativeSkyDefaults(RE::Sky* a_sky);

        mutable std::mutex         _mutex;
        Math::SunAttenuationResult _smoothedState{};
        bool                       _hasCachedStar{ false };
        std::uint32_t              _cachedCellFormID{ 0 };
        RE::NiPoint3               _cachedStarPosition{};
        float                      _accumulatedTimeMs{ 0.0f };
        std::atomic<bool>          _activeInSpace{ false };

        // Control de volcado diagnostico automatico (1 vez por sesion al entrar en espacio)
        bool _dumpedOnSpaceEntry{ false };

        // Lineas base capturadas para evitar decaimiento acumulativo entre frames
        bool  _capturedBaseLight{ false };
        float _baseDirectionalFade{ 1.0f };
        bool  _capturedBaseGlare{ false };
        float _baseGlareAlpha{ 1.0f };
        float _lastWrittenGlareAlpha{ -1.0f };
        bool  _capturedBaseOcclusion{ false };
        float _baseOcclusion{ 0.0f };
        float _lastWrittenOcclusion{ -1.0f };
    };
}
