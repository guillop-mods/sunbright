#pragma once

#include "Config.h"

namespace Helios::Math
{
    struct SunAttenuationResult
    {
        float distance{ 0.0f };
        float normalizedIrradiance{ 1.0f };   // [0.0..1.0] energia relativa respecto a d0
        float occlusionAmount{ 0.0f };        // [0.0..maxOcclusion] 0 = sin oclusion, >0 = sol mas tenue
        float glareAlpha{ 1.0f };             // 1.0 - occlusionAmount (clamped a minSunGlareAlpha)
        float sunDiskScale{ 1.0f };           // Multiplicador del tamano aparente del disco solar
        float directionalLightMult{ 1.0f };   // Multiplicador de intensidad para NiDirectionalLight
    };

    [[nodiscard]] inline float ComputeEuclideanDistance(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs) noexcept
    {
        const float dx = a_lhs.x - a_rhs.x;
        const float dy = a_lhs.y - a_rhs.y;
        const float dz = a_lhs.z - a_rhs.z;
        return std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    }

    [[nodiscard]] inline float Clamp01(float a_value) noexcept
    {
        return std::clamp(a_value, 0.0f, 1.0f);
    }

    [[nodiscard]] inline float Smoothstep(float a_edge0, float a_edge1, float a_x) noexcept
    {
        if (a_edge1 <= a_edge0) {
            return a_x >= a_edge1 ? 1.0f : 0.0f;
        }
        const float t = Clamp01((a_x - a_edge0) / (a_edge1 - a_edge0));
        return t * t * (3.0f - 2.0f * t);
    }

    [[nodiscard]] inline float Lerp(float a_a, float a_b, float a_t) noexcept
    {
        return a_a + (a_b - a_a) * Clamp01(a_t);
    }

    // Ley del Inverso del Cuadrado normalizada a la distancia de referencia d0:
    // I(d) = 1 / (1 + k * ((d - d0) / d0)^2) para d > d0, e I(d) = 1.0 para d <= d0.
    [[nodiscard]] inline float ComputeInverseSquareIrradiance(
        float a_distance,
        float a_referenceDistance,
        float a_kFactor) noexcept
    {
        const float safeRef = (std::max)(a_referenceDistance, 1.0f);
        if (a_distance <= safeRef) {
            return 1.0f;
        }

        const float deltaRatio = (a_distance - safeRef) / safeRef;
        const float k = (std::max)(a_kFactor, 0.01f);
        return Clamp01(1.0f / (1.0f + k * (deltaRatio * deltaRatio)));
    }

    [[nodiscard]] inline SunAttenuationResult ComputeDynamicSunState(
        float a_distance,
        const AttenuationSettings& a_cfg) noexcept
    {
        SunAttenuationResult out{};
        out.distance = (std::max)(a_distance, 0.0f);

        const float refDist = (std::max)(a_cfg.referenceDistance, 1.0f);
        const float maxDist = (std::max)(a_cfg.maxFalloffDistance, refDist + 1.0f);
        const float maxOcc  = std::clamp(a_cfg.maxOcclusionAmount, 0.0f, 0.99f);

        float irradiance = 1.0f;

        switch (a_cfg.mode) {
        case AttenuationMode::InverseSquare:
            {
                irradiance = ComputeInverseSquareIrradiance(out.distance, refDist, a_cfg.inverseSquareFactor);
                break;
            }
        case AttenuationMode::Smoothstep:
            {
                const float falloffT = Smoothstep(refDist, maxDist, out.distance);
                irradiance = 1.0f - falloffT;
                break;
            }
        case AttenuationMode::Hybrid:
        default:
            {
                // Combina la fisica del inverso del cuadrado cerca de la estrella con
                // un cierre suave (smoothstep) hacia maxFalloffDistance para evitar colas asintoticas.
                const float invSq   = ComputeInverseSquareIrradiance(out.distance, refDist, a_cfg.inverseSquareFactor);
                const float smoothT = Smoothstep(refDist, maxDist, out.distance);
                irradiance = Lerp(invSq, 1.0f - smoothT, smoothT * 0.65f);
                break;
            }
        }

        out.normalizedIrradiance = Clamp01(irradiance);

        // Mayor distancia -> menor irradiancia -> mayor valor de oclusion global
        out.occlusionAmount = std::clamp((1.0f - out.normalizedIrradiance) * maxOcc, 0.0f, maxOcc);

        // Glare Alpha: 1.0 en proximidad solar, cae hacia minSunGlareAlpha en espacio profundo
        const float rawGlareAlpha = 1.0f - out.occlusionAmount;
        out.glareAlpha = std::clamp(rawGlareAlpha, a_cfg.minSunGlareAlpha, 1.0f);

        // Tamano aparente del disco solar (sun disk / billboard scale)
        out.sunDiskScale = Lerp(a_cfg.minSunDiskScale, 1.0f, out.normalizedIrradiance);

        // Luz direccional proyectada sobre la nave y asteroides cercanos
        out.directionalLightMult = Lerp(a_cfg.minDirectionalLightScale, 1.0f, out.normalizedIrradiance);

        return out;
    }
}
