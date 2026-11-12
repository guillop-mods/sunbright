#include "Hooks.h"
#include "Config.h"
#include "SunController.h"

namespace Helios::Hooks
{
    namespace
    {
        using PlayerUpdateFn = void (*)(RE::PlayerCharacter* a_this, float a_delta);

        inline PlayerUpdateFn       g_originalPlayerUpdate{ nullptr };
        inline std::atomic<bool>    g_vtableHookInstalled{ false };
        inline std::atomic<bool>    g_taskInFlight{ false };
        inline std::atomic<bool>    g_hotkeyWasDown{ false };
        inline std::jthread         g_fallbackTicker;
        inline std::chrono::steady_clock::time_point g_lastTickTime{ std::chrono::steady_clock::now() };

        void CheckDiagnosticHotkeyOnMainThread()
        {
            const auto& diag = Config::GetSingleton()->GetDiagnostics();
            if (diag.dumpHotkeyVK == 0) {
                return;
            }

            // Solo responde si Starfield tiene foco o esta procesando entrada activa
            const bool isDown = (::GetAsyncKeyState(static_cast<int>(diag.dumpHotkeyVK)) & 0x8000) != 0;
            const bool wasDown = g_hotkeyWasDown.exchange(isDown);

            if (isDown && !wasDown) {
                // Recarga el INI por si el usuario ajusto offsets antes de pulsar la tecla
                Config::GetSingleton()->Reload();
                SunController::GetSingleton()->TriggerSkyDump("Hotkey presionada");
            }
        }

        void ProcessFrameTickOnMainThread(RE::PlayerCharacter* a_player, float a_deltaSeconds)
        {
            CheckDiagnosticHotkeyOnMainThread();

            RE::Sky* sky = SunController::GetSkySingleton();
            if (!sky) {
                return;
            }

            SunController::GetSingleton()->OnPlayerFrameTick(a_player, sky, a_deltaSeconds);
        }

        // Hook directo en RE::PlayerCharacter::VTABLE[0]
        void PlayerUpdateThunk(RE::PlayerCharacter* a_this, float a_delta)
        {
            if (g_originalPlayerUpdate) {
                g_originalPlayerUpdate(a_this, a_delta);
            }
            ProcessFrameTickOnMainThread(a_this, a_delta);
        }

        // Sink nativo de eventos de entrada mapeado en CommonLibSF (si BSInputDeviceManager esta disponible)
        class InputDiagnosticSink final : public RE::BSTEventSink<RE::InputEvent*>
        {
        public:
            static InputDiagnosticSink* GetSingleton() noexcept
            {
                static InputDiagnosticSink instance;
                return &instance;
            }

            RE::BSEventNotifyControl ProcessEvent(
                RE::InputEvent* const* a_event,
                RE::BSTEventSource<RE::InputEvent*>* a_source) override
            {
                (void)a_source;
                if (!a_event || !*a_event) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                // Cuando no hay hook de vtable activo, procesamos la deteccion de tecla
                // directamente desde el flujo de eventos de entrada de Creation Engine 2.
                if (!g_vtableHookInstalled.load()) {
                    CheckDiagnosticHotkeyOnMainThread();
                }

                return RE::BSEventNotifyControl::kContinue;
            }
        };

        void RegisterMappedEventSinks()
        {
            if constexpr (requires {
                RE::BSInputDeviceManager::GetSingleton();
                RE::BSInputDeviceManager::GetSingleton()->AddEventSink(InputDiagnosticSink::GetSingleton());
            }) {
                if (auto* inputMgr = RE::BSInputDeviceManager::GetSingleton()) {
                    inputMgr->AddEventSink(InputDiagnosticSink::GetSingleton());
                    spdlog::info("Sink de entrada registrado en RE::BSInputDeviceManager.");
                }
            }
        }

        void StartTaskInterfaceLoop()
        {
            const auto* taskInterface = SFSE::GetTaskInterface();
            if (!taskInterface) {
                spdlog::error("No se pudo obtener SFSE::GetTaskInterface() para el modo de respaldo.");
                return;
            }

            g_lastTickTime = std::chrono::steady_clock::now();

            g_fallbackTicker = std::jthread([](std::stop_token a_stopToken) {
                std::mutex              waitMutex;
                std::condition_variable_any waitCv;

                while (!a_stopToken.stop_requested()) {
                    const float intervalMs = Config::GetSingleton()->GetGeneral().updateIntervalMs;
                    const auto  sleepDur   = std::chrono::milliseconds(
                        static_cast<long long>(std::clamp(intervalMs, 8.0f, 250.0f)));

                    {
                        std::unique_lock lock(waitMutex);
                        waitCv.wait_for(lock, a_stopToken, sleepDur, [&]() {
                            return a_stopToken.stop_requested();
                        });
                    }

                    if (a_stopToken.stop_requested()) {
                        break;
                    }

                    bool expected = false;
                    if (!g_taskInFlight.compare_exchange_strong(expected, true)) {
                        continue;
                    }

                    if (const auto* task = SFSE::GetTaskInterface()) {
                        task->AddTask([]() {
                            const auto now = std::chrono::steady_clock::now();
                            const float dt = std::chrono::duration<float>(now - g_lastTickTime).count();
                            g_lastTickTime = now;

                            RE::PlayerCharacter* player = nullptr;
                            if constexpr (requires { RE::PlayerCharacter::GetSingleton(); }) {
                                player = RE::PlayerCharacter::GetSingleton();
                            }

                            ProcessFrameTickOnMainThread(player, dt);
                            g_taskInFlight.store(false);
                        });
                    } else {
                        g_taskInFlight.store(false);
                    }
                }
            });

            spdlog::info("Bucle de actualizacion en hilo principal iniciado via SFSE::GetTaskInterface()->AddTask().");
        }
    }

    bool Install()
    {
        RegisterMappedEventSinks();

        const auto& hookCfg = Config::GetSingleton()->GetHooks();

        // 1. Hook principal sobre la vtable de RE::PlayerCharacter (ya mapeada por SF_RTTI_VTABLE)
        if (hookCfg.playerUpdateVfuncIdx > 0) {
            std::uintptr_t originalAddr = 0;

            if constexpr (requires { RE::PlayerCharacter::VTABLE[0]; }) {
                REL::Relocation<std::uintptr_t> playerVtbl{ RE::PlayerCharacter::VTABLE[0] };
                originalAddr = playerVtbl.write_vfunc(
                    hookCfg.playerUpdateVfuncIdx,
                    reinterpret_cast<std::uintptr_t>(&PlayerUpdateThunk));
            } else if constexpr (requires { RE::VTABLE::PlayerCharacter[0]; }) {
                REL::Relocation<std::uintptr_t> playerVtbl{ RE::VTABLE::PlayerCharacter[0] };
                originalAddr = playerVtbl.write_vfunc(
                    hookCfg.playerUpdateVfuncIdx,
                    reinterpret_cast<std::uintptr_t>(&PlayerUpdateThunk));
            }

            if (originalAddr != 0) {
                g_originalPlayerUpdate = reinterpret_cast<PlayerUpdateFn>(originalAddr);
                g_vtableHookInstalled.store(true);

                spdlog::info(
                    "Hook instalado en RE::PlayerCharacter::VTABLE[0] slot 0x{:X} (original=0x{:016X}).",
                    hookCfg.playerUpdateVfuncIdx,
                    originalAddr);
                return true;
            }
        }

        // 2. Respaldo automatico sin indices hardcoded: TaskInterface + BSInputDeviceManager
        if (hookCfg.useTaskFallbackWhenUnset) {
            spdlog::info(
                "uPlayerUpdateVfuncIdx=0: activando respaldo seguro con SFSE::GetTaskInterface() y SunController::GetSkySingleton().");
            StartTaskInterfaceLoop();
            return true;
        }

        spdlog::warn("No se instalo ningun hook de actualizacion (uPlayerUpdateVfuncIdx=0 y respaldo desactivado).");
        return false;
    }

    void Shutdown()
    {
        if (g_fallbackTicker.joinable()) {
            g_fallbackTicker.request_stop();
            g_fallbackTicker.join();
        }
    }
}
