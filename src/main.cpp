#include "Config.h"
#include "Hooks.h"
#include "SunController.h"

namespace
{
    void InitializeLogging()
    {
        auto path = logger::log_directory();
        if (!path) {
            return;
        }

        *path /= "DynamicSunAttenuation.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log  = std::make_shared<spdlog::logger>("global_log", std::move(sink));

        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    }

    void OnSFSEMessage(SFSE::MessagingInterface::Message* a_msg)
    {
        if (!a_msg) {
            return;
        }

        switch (a_msg->type) {
        case SFSE::MessagingInterface::kPostLoad:
            Helios::Config::GetSingleton()->Load();
            if (Helios::Config::GetSingleton()->GetGeneral().debugLog) {
                spdlog::set_level(spdlog::level::debug);
                spdlog::flush_on(spdlog::level::debug);
            }
            break;

        case SFSE::MessagingInterface::kPostDataLoad:
            Helios::Hooks::Install();
            break;

        case SFSE::MessagingInterface::kPreLoadGame:
        case SFSE::MessagingInterface::kNewGame:
            Helios::SunController::GetSingleton()->ResetState();
            break;

        case SFSE::MessagingInterface::kPostLoadGame:
            if (Helios::Config::GetSingleton()->GetDiagnostics().dumpSkyPointersOnLoad) {
                Helios::SunController::GetSingleton()->TriggerSkyDump("kPostLoadGame (INI)");
            }
            break;

        default:
            break;
        }
    }
}

SFSE_EXPORT constinit auto SFSEPlugin_Version = []() noexcept {
    SFSE::PluginVersionData data{};

    data.PluginVersion(REL::Version{ 1, 1, 0, 0 });
    data.PluginName("DynamicSunAttenuation");
    data.AuthorName("Starfield Modding Community");
    data.UsesAddressLibrary(true);
    data.IsLayoutDependent(true);
    data.CompatibleVersions({ SFSE::RUNTIME_LATEST });

    return data;
}();

SFSEPluginLoad(const SFSE::LoadInterface* a_sfse)
{
    SFSE::Init(a_sfse);
    InitializeLogging();

    logger::info("DynamicSunAttenuation v1.1.0 inicializado.");

    if (const auto* messaging = SFSE::GetMessagingInterface()) {
        messaging->RegisterListener(OnSFSEMessage);
    } else {
        logger::error("No se pudo obtener SFSE::MessagingInterface.");
        return false;
    }

    return true;
}
