#include "SkyDiagnostics.h"

#include <cctype>

namespace Helios::Diagnostics
{
    namespace
    {
        // Estructuras estandar ABI MSVC x64 para RTTI (signature == 1)
        struct MsvcCompleteObjectLocator64
        {
            std::uint32_t signature;         // 1 en x64 (COL_SIG_REV1)
            std::uint32_t offset;            // offset de esta vtable dentro de la clase completa
            std::uint32_t cdOffset;          // constructor displacement offset
            std::int32_t  pTypeDescriptor;   // RVA relativo a imageBase
            std::int32_t  pClassDescriptor;  // RVA relativo a imageBase
            std::int32_t  pSelf;             // RVA de este COL relativo a imageBase
        };

        struct MsvcTypeDescriptor64
        {
            std::uintptr_t pVFTable;
            std::uintptr_t spare;
            char           name[1];
        };

        [[nodiscard]] bool IsReadableProtection(DWORD a_protect) noexcept
        {
            if (a_protect & (PAGE_GUARD | PAGE_NOACCESS)) {
                return false;
            }
            const DWORD baseProtect = a_protect & 0xFF;
            return baseProtect == PAGE_READONLY ||
                   baseProtect == PAGE_READWRITE ||
                   baseProtect == PAGE_WRITECOPY ||
                   baseProtect == PAGE_EXECUTE_READ ||
                   baseProtect == PAGE_EXECUTE_READWRITE ||
                   baseProtect == PAGE_EXECUTE_WRITECOPY;
        }

        [[nodiscard]] bool IsWritableProtection(DWORD a_protect) noexcept
        {
            if (a_protect & (PAGE_GUARD | PAGE_NOACCESS)) {
                return false;
            }
            const DWORD baseProtect = a_protect & 0xFF;
            return baseProtect == PAGE_READWRITE ||
                   baseProtect == PAGE_WRITECOPY ||
                   baseProtect == PAGE_EXECUTE_READWRITE ||
                   baseProtect == PAGE_EXECUTE_WRITECOPY;
        }

        [[nodiscard]] bool IsAddressRangeAccessible(std::uintptr_t a_addr, std::size_t a_size, bool a_requireWrite = false) noexcept
        {
            if (a_addr < 0x10000 || a_addr > 0x00007FFFFFFFFFFFULL) {
                return false;
            }

            MEMORY_BASIC_INFORMATION mbi{};
            if (::VirtualQuery(reinterpret_cast<LPCVOID>(a_addr), &mbi, sizeof(mbi)) == 0) {
                return false;
            }

            if (mbi.State != MEM_COMMIT) {
                return false;
            }

            const bool protOk = a_requireWrite
                ? IsWritableProtection(mbi.Protect)
                : IsReadableProtection(mbi.Protect);
            if (!protOk) {
                return false;
            }

            const auto regionStart = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            const auto regionEnd   = regionStart + mbi.RegionSize;
            return a_addr >= regionStart && (a_addr + a_size) <= regionEnd;
        }

        // Helpers aislados para SEH (sin objetos C++ con destructores en el mismo stack frame)
        bool SehReadUInt64(std::uintptr_t a_addr, std::uint64_t* a_out) noexcept
        {
            __try {
                *a_out = *reinterpret_cast<const volatile std::uint64_t*>(a_addr);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool SehReadUInt32(std::uintptr_t a_addr, std::uint32_t* a_out) noexcept
        {
            __try {
                *a_out = *reinterpret_cast<const volatile std::uint32_t*>(a_addr);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool SehWriteUInt32(std::uintptr_t a_addr, std::uint32_t a_val) noexcept
        {
            __try {
                *reinterpret_cast<volatile std::uint32_t*>(a_addr) = a_val;
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool SehReadBytes(std::uintptr_t a_addr, void* a_dst, std::size_t a_size) noexcept
        {
            __try {
                std::memcpy(a_dst, reinterpret_cast<const void*>(a_addr), a_size);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        [[nodiscard]] std::string CleanMsvcMangledName(std::string_view a_mangled)
        {
            // Formato tipico MSVC: ".?AVSun@RE@@" o ".?AVNiDirectionalLight@@" o ".?AU..."
            std::string_view s = a_mangled;
            if (s.starts_with(".?AV") || s.starts_with(".?AU")) {
                s.remove_prefix(4);
            } else if (s.starts_with(".?")) {
                s.remove_prefix(2);
            }

            if (s.ends_with("@@")) {
                s.remove_suffix(2);
            }

            // Los namespaces en MSVC van invertidos separados por '@': Class@Namespace -> Namespace::Class
            std::vector<std::string_view> parts;
            while (!s.empty()) {
                const auto pos = s.find('@');
                if (pos == std::string_view::npos) {
                    parts.push_back(s);
                    break;
                }
                if (pos > 0) {
                    parts.push_back(s.substr(0, pos));
                }
                s.remove_prefix(pos + 1);
            }

            if (parts.empty()) {
                return std::string(a_mangled);
            }

            std::string result;
            for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
                if (!result.empty()) {
                    result += "::";
                }
                result.append(*it);
            }
            return result;
        }

        [[nodiscard]] bool ContainsCaseInsensitive(std::string_view a_haystack, std::string_view a_needle) noexcept
        {
            const auto it = std::search(
                a_haystack.begin(), a_haystack.end(),
                a_needle.begin(), a_needle.end(),
                [](char a_c1, char a_c2) {
                    return std::tolower(static_cast<unsigned char>(a_c1)) ==
                           std::tolower(static_cast<unsigned char>(a_c2));
                });
            return it != a_haystack.end();
        }

        [[nodiscard]] bool IsSunOrLightCandidate(std::string_view a_name) noexcept
        {
            return ContainsCaseInsensitive(a_name, "Sun") ||
                   ContainsCaseInsensitive(a_name, "DirectionalLight") ||
                   ContainsCaseInsensitive(a_name, "NiLight") ||
                   ContainsCaseInsensitive(a_name, "Billboard") ||
                   ContainsCaseInsensitive(a_name, "Glare") ||
                   ContainsCaseInsensitive(a_name, "Sky");
        }

        void DumpChildObjectPointers(std::uintptr_t a_objAddr, std::string_view a_parentName, std::size_t a_childSlots)
        {
            for (std::size_t j = 0; j < a_childSlots; ++j) {
                const std::size_t childOff = j * sizeof(std::uintptr_t);
                std::uint64_t rawChild = 0;
                if (!TryReadUInt64(a_objAddr + childOff, rawChild)) {
                    break;
                }

                if (rawChild == 0) {
                    continue;
                }

                const auto rtti = ResolveObjectRtti(static_cast<std::uintptr_t>(rawChild));
                if (rtti.hasMsvcRtti) {
                    logger::info(
                        "    [Child {:02d} | {}+0x{:03X}] 0x{:016X} -> RTTI: {} ({})",
                        j,
                        a_parentName,
                        childOff,
                        rawChild,
                        rtti.cleanName,
                        rtti.mangledName);
                } else if (!rtti.isReadablePtr) {
                    float f0 = 0.0f;
                    float f1 = 0.0f;
                    const std::uint32_t lo = static_cast<std::uint32_t>(rawChild & 0xFFFFFFFFULL);
                    const std::uint32_t hi = static_cast<std::uint32_t>((rawChild >> 32) & 0xFFFFFFFFULL);
                    std::memcpy(&f0, &lo, sizeof(float));
                    std::memcpy(&f1, &hi, sizeof(float));
                    if (std::isfinite(f0) && std::isfinite(f1) && std::abs(f0) <= 1e6f && std::abs(f1) <= 1e6f) {
                        logger::info(
                            "    [Child {:02d} | {}+0x{:03X}] 0x{:016X} -> scalar (f0={:.4f}, f1={:.4f})",
                            j,
                            a_parentName,
                            childOff,
                            rawChild,
                            f0,
                            f1);
                    }
                }
            }
        }
    }

    bool TryReadUInt64(std::uintptr_t a_address, std::uint64_t& a_outValue) noexcept
    {
        if (!IsAddressRangeAccessible(a_address, sizeof(std::uint64_t), false)) {
            return false;
        }
        return SehReadUInt64(a_address, &a_outValue);
    }

    bool TryReadFloat(std::uintptr_t a_address, float& a_outValue) noexcept
    {
        if (!IsAddressRangeAccessible(a_address, sizeof(float), false)) {
            return false;
        }
        std::uint32_t bits = 0;
        if (!SehReadUInt32(a_address, &bits)) {
            return false;
        }
        std::memcpy(&a_outValue, &bits, sizeof(float));
        return std::isfinite(a_outValue);
    }

    bool TryWriteFloat(std::uintptr_t a_address, float a_value) noexcept
    {
        if (!std::isfinite(a_value) || !IsAddressRangeAccessible(a_address, sizeof(float), true)) {
            return false;
        }
        std::uint32_t bits = 0;
        std::memcpy(&bits, &a_value, sizeof(float));
        return SehWriteUInt32(a_address, bits);
    }

    RttiResolution ResolveObjectRtti(std::uintptr_t a_candidatePtr) noexcept
    {
        RttiResolution res{};

        // Descarta enteros pequenos o direcciones no alineadas a 8 bytes en x64
        if (a_candidatePtr < 0x10000 || (a_candidatePtr & 0x7ULL) != 0 || a_candidatePtr > 0x00007FFFFFFFFFFFULL) {
            return res;
        }

        if (!IsAddressRangeAccessible(a_candidatePtr, sizeof(std::uintptr_t), false)) {
            return res;
        }

        res.isReadablePtr = true;

        std::uint64_t vtablePtr = 0;
        if (!TryReadUInt64(a_candidatePtr, vtablePtr)) {
            return res;
        }

        if (vtablePtr < 0x10000 || (vtablePtr & 0x7ULL) != 0) {
            return res;
        }

        // En MSVC x64, vtable[-1] apunta al CompleteObjectLocator
        const std::uintptr_t colSlotAddr = static_cast<std::uintptr_t>(vtablePtr) - sizeof(std::uintptr_t);
        if (!IsAddressRangeAccessible(colSlotAddr, sizeof(std::uintptr_t) * 2, false)) {
            return res;
        }

        std::uint64_t colAddr = 0;
        if (!TryReadUInt64(colSlotAddr, colAddr)) {
            return res;
        }

        if (!IsAddressRangeAccessible(static_cast<std::uintptr_t>(colAddr), sizeof(MsvcCompleteObjectLocator64), false)) {
            return res;
        }

        MsvcCompleteObjectLocator64 col{};
        if (!SehReadBytes(static_cast<std::uintptr_t>(colAddr), &col, sizeof(col))) {
            return res;
        }

        // Validacion estricta x64 RTTI: signature == 1 y colAddr - pSelf == modulo base
        if (col.signature != 1 || col.pSelf <= 0 || col.pTypeDescriptor <= 0) {
            return res;
        }

        const std::uintptr_t imageBase = static_cast<std::uintptr_t>(colAddr) - static_cast<std::uintptr_t>(col.pSelf);

        HMODULE modHandle = nullptr;
        if (!::GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(colAddr),
                &modHandle) ||
            reinterpret_cast<std::uintptr_t>(modHandle) != imageBase) {
            return res;
        }

        const std::uintptr_t typeDescAddr = imageBase + static_cast<std::uintptr_t>(col.pTypeDescriptor);
        if (!IsAddressRangeAccessible(typeDescAddr, offsetof(MsvcTypeDescriptor64, name) + 8, false)) {
            return res;
        }

        char rawNameBuf[192]{};
        if (!SehReadBytes(typeDescAddr + offsetof(MsvcTypeDescriptor64, name), rawNameBuf, sizeof(rawNameBuf) - 1)) {
            return res;
        }
        rawNameBuf[sizeof(rawNameBuf) - 1] = '\0';

        // Las cadenas TypeDescriptor de MSVC comienzan por ".?" (ej. ".?AVSun@@")
        if (rawNameBuf[0] != '.' || rawNameBuf[1] != '?') {
            return res;
        }

        const std::size_t nameLen = std::strlen(rawNameBuf);
        if (nameLen < 4) {
            return res;
        }

        res.hasMsvcRtti   = true;
        res.vtableAddress = static_cast<std::uintptr_t>(vtablePtr);
        res.vtableOffset  = col.offset;
        res.mangledName   = std::string(rawNameBuf, nameLen);
        res.cleanName     = CleanMsvcMangledName(res.mangledName);
        return res;
    }

    void DumpSkyPadPointers(
        RE::Sky* a_sky,
        std::size_t a_slotCount,
        bool a_inspectSunCandidates,
        const char* a_reason)
    {
        if (!a_sky) {
            logger::warn("[SkyDiag] RE::Sky::GetSingleton() devolvio nullptr (motivo: {}).", a_reason);
            return;
        }

        const std::uintptr_t skyBase   = reinterpret_cast<std::uintptr_t>(a_sky);
        const std::size_t    padStart  = offsetof(RE::Sky, pad);
        const std::size_t    windOff   = offsetof(RE::Sky, windSpeed);
        const std::size_t    alignedStart = (padStart + sizeof(std::uintptr_t) - 1) & ~(sizeof(std::uintptr_t) - 1);
        const std::size_t    maxSlots  = (windOff > alignedStart)
            ? (windOff - alignedStart) / sizeof(std::uintptr_t)
            : 0;
        const std::size_t    count     = (std::min)(a_slotCount, maxSlots);

        logger::info("=========================================================================");
        logger::info(
            "[SkyDiag] Dump de RE::Sky* = 0x{:016X} | Motivo: {} | pad=0x{:X}..0x{:X} (alignedStart=0x{:X}, windSpeed={:.3f})",
            skyBase,
            a_reason,
            padStart,
            windOff,
            alignedStart,
            a_sky->windSpeed);
        logger::info("[SkyDiag] Inspeccionando los primeros {} punteros de 8 bytes dentro de Sky::pad[0xEB8]:", count);

        std::size_t rttiFound = 0;

        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t offset = alignedStart + (i * sizeof(std::uintptr_t));
            std::uint64_t rawValue = 0;

            if (!TryReadUInt64(skyBase + offset, rawValue)) {
                logger::warn("  [Slot {:02d} | Sky+0x{:03X}] <lectura fallida>", i, offset);
                continue;
            }

            if (rawValue == 0) {
                logger::info("  [Slot {:02d} | Sky+0x{:03X}] 0x0000000000000000 | nullptr", i, offset);
                continue;
            }

            const RttiResolution rtti = ResolveObjectRtti(static_cast<std::uintptr_t>(rawValue));
            if (rtti.hasMsvcRtti) {
                ++rttiFound;
                const bool candidate = IsSunOrLightCandidate(rtti.cleanName);
                logger::info(
                    "  [Slot {:02d} | Sky+0x{:03X}] 0x{:016X} | RTTI: {} ({}) [vtblOff=0x{:X}]{}",
                    i,
                    offset,
                    rawValue,
                    rtti.cleanName,
                    rtti.mangledName,
                    rtti.vtableOffset,
                    candidate ? "  <<< CANDIDATO SUN/LIGHT" : "");

                if (a_inspectSunCandidates && candidate) {
                    DumpChildObjectPointers(static_cast<std::uintptr_t>(rawValue), rtti.cleanName, 32);
                }
            } else if (rtti.isReadablePtr) {
                logger::info(
                    "  [Slot {:02d} | Sky+0x{:03X}] 0x{:016X} | [Ptr legible sin RTTI MSVC]",
                    i,
                    offset,
                    rawValue);
            } else {
                float f0 = 0.0f;
                float f1 = 0.0f;
                const std::uint32_t lo = static_cast<std::uint32_t>(rawValue & 0xFFFFFFFFULL);
                const std::uint32_t hi = static_cast<std::uint32_t>((rawValue >> 32) & 0xFFFFFFFFULL);
                std::memcpy(&f0, &lo, sizeof(float));
                std::memcpy(&f1, &hi, sizeof(float));
                logger::info(
                    "  [Slot {:02d} | Sky+0x{:03X}] 0x{:016X} | data (f0={:.4f}, f1={:.4f})",
                    i,
                    offset,
                    rawValue,
                    std::isfinite(f0) ? f0 : 0.0f,
                    std::isfinite(f1) ? f1 : 0.0f);
            }
        }

        logger::info("[SkyDiag] Fin de volcado: {} objetos con RTTI valido encontrados en {} slots.", rttiFound, count);
        logger::info("=========================================================================");
    }
}
