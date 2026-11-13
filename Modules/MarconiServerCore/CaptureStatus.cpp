// Copyright (c) 2026 Red Marmoset Studios Ltd.
// See CaptureStatus.h

#include "CaptureStatus.h"

#include <mutex>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            std::mutex g_mutex;
            std::function<std::string()> g_provider;
        } // namespace

        std::string GetCaptureStatus()
        {
            std::function<std::string()> provider;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                provider = g_provider;
            }
            return provider ? provider() : std::string();
        }

        void SetCaptureStatusProvider(std::function<std::string()> provider)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_provider = std::move(provider);
        }
    } // namespace Server
} // namespace Marconi
