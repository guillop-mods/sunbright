#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace Helios::Diagnostics
{
    struct RttiResolution
    {
        bool        isReadablePtr{ false };
        bool        hasMsvcRtti{ false };
        std::string mangledName;
        std::string cleanName;
        std::uintptr_t vtableAddress{ 0 };
        std::uint32_t  vtableOffset{ 0 };
    };

    // Inspecciona de forma segura (VirtualQuery + SEH) si una direccion corresponde
    // a un objeto polimorfico con RTTI de MSVC x64 (CompleteObjectLocator -> TypeDescriptor).
    [[nodiscard]] RttiResolution ResolveObjectRtti(std::uintptr_t a_candidatePtr) noexcept;

    // Lectura segura de punteros y flotantes dentro de bloques no mapeados (como Sky::pad[0xEB8])
    [[nodiscard]] bool TryReadUInt64(std::uintptr_t a_address, std::uint64_t& a_outValue) noexcept;
    [[nodiscard]] bool TryReadFloat(std::uintptr_t a_address, float& a_outValue) noexcept;
    [[nodiscard]] bool TryWriteFloat(std::uintptr_t a_address, float a_value) noexcept;

    // Vuelca en el log los primeros a_slotCount punteros de 8 bytes dentro de RE::Sky::pad[0xEB8]
    // junto con sus nombres RTTI si son objetos validos, o decodificacion float si son escalares.
    void DumpSkyPadPointers(
        RE::Sky* a_sky,
        std::size_t a_slotCount = 64,
        bool a_inspectSunCandidates = true,
        const char* a_reason = "Manual");
}
