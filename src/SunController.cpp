#include "SunController.h"
#include "SkyDiagnostics.h"

#include <cctype>

namespace Helios
{
    namespace
    {
        [[nodiscard]] bool ContainsCaseInsensitive(std::string_view a_haystack, std::string_view a_needle) noexcept
        {
            if (a_needle.empty()) {
                return false;
            }

            const auto it = std::search(
                a_haystack.begin(), a_haystack.end(),
                a_needle.begin(), a_needle.end(),
                [](char a_ch1, char a_ch2) {
                    return std::tolower(static_cast<unsigned char>(a_ch1)) ==
                           std::tolower(static_cast<unsigned char>(a_ch2));
                });

            return it != a_haystack.end();
        }
    }

    SunController* SunController::GetSingleton() noexcept
    {
        static SunController instance;
        return &instance;
    }

    void SunController::ResetState() noexcept
    {
        std::scoped_lock lock(_mutex);
        _smoothedState         = Math::SunAttenuationResult{};
        _hasCachedStar         = false;
        _cachedCellFormID      = 0;
        _cachedStarPosition    = RE::NiPoint3{};
        _accumulatedTimeMs     = 0.0f;
        _dumpedOnSpaceEntry    = false;
        _capturedBaseLight     = false;
        _baseDirectionalFade   = 1.0f;
        _capturedBaseGlare     = false;
        _baseGlareAlpha        = 1.0f;
        _lastWrittenGlareAlpha = -1.0f;
        _capturedBaseOcclusion = false;
        _baseOcclusion         = 0.0f;
        _lastWrittenOcclusion  = -1.0f;
        _activeInSpace.store(false);
    }

    Math::SunAttenuationResult SunController::GetCurrentState() const noexcept
    {
        std::scoped_lock lock(_mutex);
        return _smoothedState;
    }

    void SunController::TriggerSkyDump(const char* a_reason)
    {
        auto* sky = RE::Sky::GetSingleton();
        const auto& diag = Config::GetSingleton()->GetDiagnostics();
        Diagnostics::DumpSkyPadPointers(
            sky,
            diag.dumpPointerCount,
            diag.inspectSunCandidates,
            a_reason ? a_reason : "Manual");
    }

    bool SunController::IsPlayerInSpaceCell(
        RE::PlayerCharacter* a_player,
        RE::TESObjectCELL*& a_outCell,
        RE::TESObjectREFR*& a_outShip) const
    {
        a_outCell = nullptr;
        a_outShip = nullptr;

        if (!a_player) {
            return false;
        }

        RE::TESObjectCELL* parentCell = a_player->GetParentCell();
        if (!parentCell) {
            return false;
        }

        RE::TESObjectREFR* shipRef = nullptr;
        if constexpr (requires(RE::PlayerCharacter* p) { p->GetSpaceship(false); }) {
            shipRef = a_player->GetSpaceship(false);
        } else if constexpr (requires(RE::PlayerCharacter* p) { p->GetSpaceship(); }) {
            shipRef = a_player->GetSpaceship();
        }

        const auto* cfg = Config::GetSingleton();
        if (cfg->GetGeneral().enableInSpaceOnly) {
            if (parentCell->IsInterior()) {
                return false;
            }
        }

        a_outCell = parentCell;
        a_outShip = shipRef ? shipRef : static_cast<RE::TESObjectREFR*>(a_player);
        return true;
    }

    std::optional<RE::NiPoint3> SunController::ResolveCentralStarPosition(
        RE::TESObjectCELL* a_spaceCell,
        RE::Sky* a_sky,
        const RE::NiPoint3& a_shipPos)
    {
        if (!a_spaceCell) {
            return std::nullopt;
        }

        const std::uint32_t cellFormID = a_spaceCell->GetFormID();
        if (_hasCachedStar && _cachedCellFormID == cellFormID) {
            return _cachedStarPosition;
        }

        const auto& spaceCfg = Config::GetSingleton()->GetSpaceDetection();
        std::optional<RE::NiPoint3> foundStarPos;

        a_spaceCell->ForEachReference([&](RE::TESObjectREFR* a_ref) -> RE::BSContainer::ForEachResult {
            if (!a_ref) {
                return RE::BSContainer::ForEachResult::kContinue;
            }

            const auto* baseObj = a_ref->GetBaseObject();
            const char* editorID = baseObj ? baseObj->GetFormEditorID() : nullptr;

            if (editorID) {
                if (ContainsCaseInsensitive(editorID, spaceCfg.starEditorIDToken) ||
                    ContainsCaseInsensitive(editorID, spaceCfg.sunEditorIDToken)) {
                    foundStarPos = a_ref->GetPosition();
                    return RE::BSContainer::ForEachResult::kStop;
                }
            }

            return RE::BSContainer::ForEachResult::kContinue;
        });

        // Respaldo heliocentrico cuando el sol vive en el skybox de RE::Sky sin TESObjectREFR fisico
        if (!foundStarPos.has_value() && a_sky) {
            (void)a_shipPos;
            foundStarPos = RE::NiPoint3{ 0.0f, 0.0f, 0.0f };
        }

        if (foundStarPos.has_value()) {
            _hasCachedStar      = true;
            _cachedCellFormID   = cellFormID;
            _cachedStarPosition = *foundStarPos;
        }

        return foundStarPos;
    }

    void SunController::OnPlayerFrameTick(RE::PlayerCharacter* a_player, RE::Sky* a_sky, float a_deltaSeconds)
    {
        const auto* cfg = Config::GetSingleton();
        const auto& general = cfg->GetGeneral();
        const auto& diag    = cfg->GetDiagnostics();

        RE::Sky* sky = a_sky ? a_sky : RE::Sky::GetSingleton();
        if (!sky) {
            return;
        }

        if (!general.enabled) {
            _activeInSpace.store(false);
            RestoreNativeSkyDefaults(sky);
            return;
        }

        RE::PlayerCharacter* player = a_player ? a_player : RE::PlayerCharacter::GetSingleton();
        RE::TESObjectCELL* spaceCell  = nullptr;
        RE::TESObjectREFR* playerShip = nullptr;

        if (!IsPlayerInSpaceCell(player, spaceCell, playerShip)) {
            _activeInSpace.store(false);
            RestoreNativeSkyDefaults(sky);
            return;
        }

        _activeInSpace.store(true);

        std::scoped_lock lock(_mutex);

        // Volcado automatico unico al entrar en celda espacial si bDumpSkyOnEnterSpace=true
        if (diag.dumpSkyOnEnterSpace && !_dumpedOnSpaceEntry) {
            _dumpedOnSpaceEntry = true;
            Diagnostics::DumpSkyPadPointers(
                sky,
                diag.dumpPointerCount,
                diag.inspectSunCandidates,
                "Entrada en celda espacial");
        }

        // Control de frecuencia de muestreo de distancia (por defecto ~16 ms)
        _accumulatedTimeMs += (std::max)(a_deltaSeconds, 0.0f) * 1000.0f;
        if (_accumulatedTimeMs >= general.updateIntervalMs) {
            _accumulatedTimeMs = 0.0f;

            RE::NiPoint3 shipPos{};
            if constexpr (requires(RE::TESObjectREFR* r) { r->GetPosition(); }) {
                shipPos = playerShip->GetPosition();
            } else if constexpr (requires(RE::TESObjectREFR* r) { r->data.location; }) {
                shipPos = playerShip->data.location;
            }
            const auto starPosOpt = ResolveCentralStarPosition(spaceCell, sky, shipPos);
            if (starPosOpt.has_value()) {
                const float distance = Math::ComputeEuclideanDistance(shipPos, *starPosOpt);
                const Math::SunAttenuationResult targetState =
                    Math::ComputeDynamicSunState(distance, cfg->GetAttenuation());

                const float alpha = general.temporalSmoothing;
                _smoothedState.distance             = targetState.distance;
                _smoothedState.normalizedIrradiance = Math::Lerp(_smoothedState.normalizedIrradiance, targetState.normalizedIrradiance, alpha);
                _smoothedState.occlusionAmount      = Math::Lerp(_smoothedState.occlusionAmount, targetState.occlusionAmount, alpha);
                _smoothedState.glareAlpha           = Math::Lerp(_smoothedState.glareAlpha, targetState.glareAlpha, alpha);
                _smoothedState.sunDiskScale         = Math::Lerp(_smoothedState.sunDiskScale, targetState.sunDiskScale, alpha);
                _smoothedState.directionalLightMult = Math::Lerp(_smoothedState.directionalLightMult, targetState.directionalLightMult, alpha);

                if (general.debugLog) {
                    logger::debug(
                        "[Helios] Dist={:.1f} | Irr={:.3f} | Occ={:.3f} | Glare={:.3f} | Disk={:.3f} | Light={:.3f}",
                        _smoothedState.distance,
                        _smoothedState.normalizedIrradiance,
                        _smoothedState.occlusionAmount,
                        _smoothedState.glareAlpha,
                        _smoothedState.sunDiskScale,
                        _smoothedState.directionalLightMult);
                }
            }
        }

        ApplyModulationToSky(sky, _smoothedState);
    }

    void SunController::ApplyModulationToSky(RE::Sky* a_sky, const Math::SunAttenuationResult& a_state)
    {
        if (!a_sky) {
            return;
        }

        const auto& offsets = Config::GetSingleton()->GetSkyOffsets();

        // Resuelve el objeto base de trabajo: si uSunPtrOffset > 0, lee el puntero a Sun dentro de Sky::pad.
        // De lo contrario usa la direccion base de RE::Sky directamente.
        std::uintptr_t targetObjAddr = reinterpret_cast<std::uintptr_t>(a_sky);
        if (offsets.sunPtrOffset > 0) {
            std::uint64_t sunPtr = 0;
            if (!Diagnostics::TryReadUInt64(targetObjAddr + offsets.sunPtrOffset, sunPtr) || sunPtr == 0) {
                return;
            }
            targetObjAddr = static_cast<std::uintptr_t>(sunPtr);
        }

        // 1. Modulacion de Sun Glare Alpha (protegiendo eclipses nativos y evitando decaimiento acumulativo)
        if (offsets.sunGlareAlphaOffset > 0) {
            const std::uintptr_t glareAddr = targetObjAddr + offsets.sunGlareAlphaOffset;
            float currentGlare = 1.0f;
            if (Diagnostics::TryReadFloat(glareAddr, currentGlare)) {
                if (!_capturedBaseGlare || std::abs(currentGlare - _lastWrittenGlareAlpha) > 0.002f) {
                    _baseGlareAlpha    = std::clamp(currentGlare, 0.0f, 1.0f);
                    _capturedBaseGlare = true;
                }
                const float outGlare = std::clamp(_baseGlareAlpha * a_state.glareAlpha, 0.0f, 1.0f);
                if (Diagnostics::TryWriteFloat(glareAddr, outGlare)) {
                    _lastWrittenGlareAlpha = outGlare;
                }
            }
        }

        // 2. Modulacion multiplicativa de Oclusion Global
        if (offsets.sunOcclusionOffset > 0) {
            const std::uintptr_t occAddr = targetObjAddr + offsets.sunOcclusionOffset;
            float currentOcc = 0.0f;
            if (Diagnostics::TryReadFloat(occAddr, currentOcc)) {
                if (!_capturedBaseOcclusion || std::abs(currentOcc - _lastWrittenOcclusion) > 0.002f) {
                    _baseOcclusion         = std::clamp(currentOcc, 0.0f, 1.0f);
                    _capturedBaseOcclusion = true;
                }
                const float outOcc = std::clamp(
                    1.0f - ((1.0f - _baseOcclusion) * (1.0f - a_state.occlusionAmount)),
                    0.0f,
                    1.0f);
                if (Diagnostics::TryWriteFloat(occAddr, outOcc)) {
                    _lastWrittenOcclusion = outOcc;
                }
            }
        }

        // 3. Escala de nodos visuales del disco solar y resplandor (NiAVObject / NiBillboardNode)
        auto scaleNodeAtOffset = [&](std::uint32_t a_nodeOffset) {
            if (a_nodeOffset == 0) {
                return;
            }
            std::uint64_t nodePtr = 0;
            if (!Diagnostics::TryReadUInt64(targetObjAddr + a_nodeOffset, nodePtr) || nodePtr == 0) {
                return;
            }
            if constexpr (requires(RE::NiAVObject* obj) { obj->local.scale; }) {
                auto* avObj = reinterpret_cast<RE::NiAVObject*>(static_cast<std::uintptr_t>(nodePtr));
                avObj->local.scale = a_state.sunDiskScale;
            }
        };

        scaleNodeAtOffset(offsets.sunBaseNodeOffset);
        scaleNodeAtOffset(offsets.sunGlareNodeOffset);

        // 4. Luz direccional (NiDirectionalLight) proyectada sobre la nave
        if (offsets.sunDirectionalLightOffset > 0) {
            std::uint64_t lightPtr = 0;
            if (Diagnostics::TryReadUInt64(targetObjAddr + offsets.sunDirectionalLightOffset, lightPtr) && lightPtr != 0) {
                if constexpr (requires(RE::NiLight* l) { l->GetLightRuntimeData().fade; }) {
                    auto* light = reinterpret_cast<RE::NiLight*>(static_cast<std::uintptr_t>(lightPtr));
                    if (!_capturedBaseLight) {
                        _baseDirectionalFade = light->GetLightRuntimeData().fade;
                        _capturedBaseLight   = true;
                    }
                    light->GetLightRuntimeData().fade = _baseDirectionalFade * a_state.directionalLightMult;
                }
            }
        }
    }

    void SunController::RestoreNativeSkyDefaults(RE::Sky* a_sky)
    {
        if (!a_sky) {
            return;
        }

        const auto& offsets = Config::GetSingleton()->GetSkyOffsets();
        std::uintptr_t targetObjAddr = reinterpret_cast<std::uintptr_t>(a_sky);
        if (offsets.sunPtrOffset > 0) {
            std::uint64_t sunPtr = 0;
            if (!Diagnostics::TryReadUInt64(targetObjAddr + offsets.sunPtrOffset, sunPtr) || sunPtr == 0) {
                return;
            }
            targetObjAddr = static_cast<std::uintptr_t>(sunPtr);
        }

        if (offsets.sunGlareAlphaOffset > 0 && _capturedBaseGlare) {
            Diagnostics::TryWriteFloat(targetObjAddr + offsets.sunGlareAlphaOffset, _baseGlareAlpha);
            _capturedBaseGlare     = false;
            _lastWrittenGlareAlpha = -1.0f;
        }
        if (offsets.sunOcclusionOffset > 0 && _capturedBaseOcclusion) {
            Diagnostics::TryWriteFloat(targetObjAddr + offsets.sunOcclusionOffset, _baseOcclusion);
            _capturedBaseOcclusion = false;
            _lastWrittenOcclusion  = -1.0f;
        }

        auto resetNodeAtOffset = [&](std::uint32_t a_nodeOffset) {
            if (a_nodeOffset == 0) {
                return;
            }
            std::uint64_t nodePtr = 0;
            if (!Diagnostics::TryReadUInt64(targetObjAddr + a_nodeOffset, nodePtr) || nodePtr == 0) {
                return;
            }
            if constexpr (requires(RE::NiAVObject* obj) { obj->local.scale; }) {
                auto* avObj = reinterpret_cast<RE::NiAVObject*>(static_cast<std::uintptr_t>(nodePtr));
                avObj->local.scale = 1.0f;
            }
        };

        resetNodeAtOffset(offsets.sunBaseNodeOffset);
        resetNodeAtOffset(offsets.sunGlareNodeOffset);

        if (offsets.sunDirectionalLightOffset > 0 && _capturedBaseLight) {
            std::uint64_t lightPtr = 0;
            if (Diagnostics::TryReadUInt64(targetObjAddr + offsets.sunDirectionalLightOffset, lightPtr) && lightPtr != 0) {
                if constexpr (requires(RE::NiLight* l) { l->GetLightRuntimeData().fade; }) {
                    auto* light = reinterpret_cast<RE::NiLight*>(static_cast<std::uintptr_t>(lightPtr));
                    light->GetLightRuntimeData().fade = _baseDirectionalFade;
                }
            }
            _capturedBaseLight = false;
        }
    }
}
