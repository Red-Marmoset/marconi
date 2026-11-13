// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The session helper; see SessionHost.h

#include "SessionHost.h"

#include "ControlPipe.h"
#include "DesktopSwitch.h"
#include "IdentityStore.h"
#include "Listener.h"
#include "ListenerControlHost.h"
#include "LoggingInputSink.h"
#include "ServerLog.h"
#include "ServiceConfig.h"
#include "SystemInput.h"
#include "SystemScreen.h"
#include "TestPatternScreen.h"

#include <MarconiCore/Version.h>

#include <Log/Log.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            std::atomic<bool> g_stopRequested{false};

            void OnSignal(int)
            {
                g_stopRequested = true; // only sets a flag; the monitor thread does the stopping
            }
        } // namespace

        int RunSessionHost(const SessionHostOptions& options)
        {
            std::string dataDirectory = options.dataDirectory.empty() ? GetDefaultDataDirectory()
                                                                      : options.dataDirectory;
            // One log file per run in <data folder>/logs. The service starts this helper without a console, so
            // without the file nothing would say why it stopped. If the folder can't be written, carry on without.
            ServerLogFile logFile;
            logFile.Open(GetLogDirectory(dataDirectory), "MarconiServerSession");
            LOG_INFO("Service", "Session helper %s starting for console session %u", GetVersionString(),
                     static_cast<unsigned>(options.consoleSession));
            Settings settings;
            settings.serverVersion = GetVersionString();
            std::string error;
            if (!LoadServiceConfig(dataDirectory, settings, error))
            {
                printf("Could not read the settings in %s: %s\n", dataDirectory.c_str(), error.c_str());
                LOG_WARNING("Service", "Could not read the settings in %s: %s", dataDirectory.c_str(), error.c_str());
                return 1;
            }
            if (options.portOverride >= 0)
            {
                settings.port = static_cast<uint16_t>(options.portOverride);
            }
            if (!LoadOrCreateIdentity(GetCertificatePath(dataDirectory), GetPrivateKeyPath(dataDirectory),
                                      settings.serverName, settings, error))
            {
                printf("Could not load or make the server's certificate in %s: %s\n", dataDirectory.c_str(),
                       error.c_str());
                LOG_WARNING("Service", "Could not load or make the server's certificate in %s: %s",
                            dataDirectory.c_str(), error.c_str());
                return 1;
            }

            // The screen and input: this computer's own, or the test pattern when asked for.
            std::unique_ptr<ScreenSource> systemScreen;
            std::unique_ptr<InputSink> systemInput;
            std::unique_ptr<TestPatternScreen> pattern;
            LoggingInputSink logging;
            ScreenSource* screen = nullptr;
            InputSink* input = &logging;
            if (!options.useTestPattern)
            {
                systemScreen = CreateSystemScreen(error);
                if (!systemScreen)
                {
                    printf("Could not capture this computer's screen: %s\n", error.c_str());
                    LOG_WARNING("Service", "Could not capture this computer's screen: %s", error.c_str());
                    return 3;
                }
                screen = systemScreen.get();
                systemInput = CreateSystemInput();
                if (systemInput)
                {
                    input = systemInput.get();
                }
            }
            else
            {
                pattern = std::make_unique<TestPatternScreen>(options.testPatternWidth, options.testPatternHeight);
                screen = pattern.get();
            }

            Listener listener(*screen, *input, settings);
            if (!listener.Start(error))
            {
                printf("Could not start listening on port %u: %s\n", static_cast<unsigned>(settings.port),
                       error.c_str());
                LOG_WARNING("Service", "Could not start listening on port %u: %s", static_cast<unsigned>(settings.port),
                            error.c_str());
                return 4;
            }

            // Changing the password from the tray icon: store it in the settings file (so it survives a restart) and
            // switch the live login check to it.
            std::mutex configMutex;
            ListenerControlHost host(
                listener, settings, options.consoleSession,
                [&](const std::string& hash, std::string& text) {
                    std::lock_guard<std::mutex> lock(configMutex);
                    Settings saved;
                    std::string saveError;
                    if (!LoadServiceConfig(dataDirectory, saved, saveError))
                    {
                        text = "Could not read the settings: " + saveError;
                        return Control::ResultCode::Failed;
                    }
                    saved.passwordHash = hash;
                    if (!SaveServiceConfig(dataDirectory, saved, saveError))
                    {
                        text = "Could not save the password: " + saveError;
                        return Control::ResultCode::Failed;
                    }
                    listener.SetPasswordHash(hash);
                    LOG_INFO("Service", "The access password was changed from the tray icon");
                    return Control::ResultCode::Ok;
                },
                []() { return GetInputDesktopName(); });

            Control::ControlPipeServer pipe(host, settings.serverVersion);
            std::string pipeError;
            if (!pipe.Start(pipeError))
            {
                // Not fatal: viewers can still connect; only the tray icon's status is missing.
                LOG_INFO("Service", "No control pipe for the tray icon: %s", pipeError.c_str());
            }

            std::signal(SIGINT, OnSignal);
            std::signal(SIGTERM, OnSignal);
            printf("Marconi Server %s serving %s on port %u (session %u)\n", GetVersionString(),
                   options.useTestPattern ? "a test pattern" : "this computer's screen",
                   static_cast<unsigned>(listener.GetPort()), static_cast<unsigned>(options.consoleSession));

            // A monitor thread tells the tray icon when a viewer comes or goes, and turns a signal into a stop.
            std::atomic<bool> done{false};
            std::thread monitor([&]() {
                uint32_t lastViewer = 0;
                std::string lastDesktop;
                while (!done)
                {
                    if (g_stopRequested)
                    {
                        listener.Stop();
                        break;
                    }
                    ViewerSnapshot viewer;
                    uint32_t current = listener.GetViewer(viewer) ? viewer.id : 0;
                    std::string desktop = GetInputDesktopName();
                    if (current != lastViewer || desktop != lastDesktop)
                    {
                        lastViewer = current;
                        lastDesktop = desktop;
                        pipe.NotifyChanged();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
            });

            listener.Run();
            done = true;
            monitor.join();
            pipe.Stop();
            LOG_INFO("Service", "Session helper stopped");
            return 0;
        }
    } // namespace Server
} // namespace Marconi
