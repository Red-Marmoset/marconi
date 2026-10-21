// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The background server runner

#include "ServerController.h"

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
                m_systemInput = Server::CreateSystemInput();
                m_screen = m_systemScreen.get();
                m_input = m_systemInput.get();
                m_servedDescription = "this computer's screen";
                return;
            }

            // No capture on this computer yet: serve a moving test pattern and log the input that comes in.
            LOG_INFO("Server", "No screen capture on this computer yet (%s); serving a test pattern.",
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

        bool ServerController::ChangePort(uint16_t port, std::string& error)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            StopLocked();
            m_settings.port = port;
            return StartLocked(error);
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
