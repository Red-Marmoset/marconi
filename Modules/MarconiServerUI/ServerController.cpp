// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The background server runner

#include "ServerController.h"

#include <MarconiServerCore/AllowList.h>
#include <MarconiServerCore/LoggingInputSink.h>
#include <MarconiServerCore/SystemInput.h>
#include <MarconiServerCore/SystemScreen.h>

#include <Log/Log.h>

namespace Marconi
{
    namespace ServerUI
    {
        ServerController::ServerController(const Server::Settings& settings) : m_settings(settings) {}

        ServerController::~ServerController() { Stop(); }

        void ServerController::ChooseScreenAndInput()
        {
            if (m_screen != nullptr)
            {
                return; // already chosen
            }

            std::string screenError;
            m_systemScreen = Server::CreateSystemScreen(screenError);
            if (m_systemScreen)
            {
                std::string inputError;
                m_systemInput = Server::CreateSystemInput(inputError);
                if (!m_systemInput)
                {
                    m_problem = "Viewers can see this screen but can't control it. " + inputError;
                    LOG_WARNING("Server", "No input control: %s", inputError.c_str());
                }
                m_screen = m_systemScreen.get();
                m_input = m_systemInput.get();
                m_servedDescription = "this computer's screen";
                return;
            }

            // No capture here: serve a moving test pattern and log the input that comes in, and keep the reason to
            // show in the tray menu.
            m_problem = "Can't share this computer's screen. " + screenError;
            {
                // Ask for the control permission now too (on a Mac this shows its prompt), so both can be given
                // in one go. The input made here is thrown away: a test pattern must not move the real mouse.
                std::string inputError;
                Server::CreateSystemInput(inputError);
                if (!inputError.empty() && inputError != screenError)
                {
                    m_problem += " " + inputError;
                }
            }
            LOG_WARNING("Server", "Can't share this computer's screen (%s); serving a test pattern.",
                        screenError.c_str());
            m_pattern = std::make_unique<Server::TestPatternScreen>(1280, 720);
            m_loggingInput = std::make_unique<Server::LoggingInputSink>();
            m_screen = m_pattern.get();
            m_input = m_loggingInput.get();
            m_servedDescription = "a test pattern";
        }

        bool ServerController::StartLocked(std::string& error)
        {
            ChooseScreenAndInput();
            m_listener = std::make_unique<Server::Listener>(*m_screen, *m_input, m_settings);
            if (m_systemScreen && !m_clipboard)
            {
                m_clipboard = Server::CreateSystemClipboard(); // the test pattern stands in for a computer that isn't there
            }
            m_listener->SetClipboard(m_clipboard.get());
            m_listener->SetNotifier(m_notifier);
            if (!m_listener->Start(error))
            {
                m_listener.reset();
                return false;
            }
            Server::Listener* listener = m_listener.get();
            m_thread = std::thread([listener]() { listener->Run(); });
            m_running = true;
            return true;
        }

        void ServerController::StopLocked()
        {
            if (!m_running)
            {
                return;
            }
            if (m_listener)
            {
                m_listener->Stop();
            }
            if (m_thread.joinable())
            {
                m_thread.join();
            }
            m_listener.reset();
            m_running = false;
        }

        bool ServerController::Start(std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_running)
            {
                return true;
            }
            return StartLocked(error);
        }

        void ServerController::Stop()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            StopLocked();
        }

        bool ServerController::IsRunning() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_running;
        }

        uint16_t ServerController::GetBoundPort() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_listener ? m_listener->GetPort() : m_settings.port;
        }

        std::string ServerController::GetFingerprint() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_listener ? m_listener->GetCertificateFingerprint() : std::string();
        }

        std::string ServerController::GetServedDescription() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_servedDescription;
        }

        bool ServerController::CheckScreenAndInputAgain(std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            StopLocked();
            m_listener.reset();
            m_screen = nullptr;
            m_input = nullptr;
            m_systemScreen.reset();
            m_systemInput.reset();
            m_pattern.reset();
            m_loggingInput.reset();
            m_problem.clear();
            return StartLocked(error);
        }

        std::string ServerController::GetProblem() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_problem;
        }

        bool ServerController::ChangePort(uint16_t port, std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            StopLocked();
            m_settings.port = port;
            return StartLocked(error);
        }

        bool ServerController::ChangeFilesEnabled(bool enabled, std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            StopLocked();
            m_settings.filesEnabled = enabled;
            return StartLocked(error);
        }

        bool ServerController::ChangeAllowedAddresses(const std::vector<std::string>& entries, std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            Server::AllowList check;
            if (!check.Set(entries, error))
            {
                return false;
            }
            m_settings.allowedAddresses = entries;
            if (m_listener)
            {
                return m_listener->SetAllowedAddresses(entries, error);
            }
            return true;
        }

        void ServerController::ChangeIdleMinutes(int minutes)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_settings.idleMinutes = minutes < 0 ? 0 : minutes;
            if (m_listener)
            {
                m_listener->SetIdleMinutes(m_settings.idleMinutes);
            }
        }

        void ServerController::SetNotifier(Server::ConnectionNotifier* notifier)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_notifier = notifier;
        }

        bool ServerController::ChangePasswordHash(const std::string& passwordHash, std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            StopLocked();
            m_settings.passwordHash = passwordHash;
            return StartLocked(error);
        }
    } // namespace ServerUI
} // namespace Marconi
