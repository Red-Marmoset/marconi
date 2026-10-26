// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Internal: the few places where Winsock and BSD sockets differ (implemented in SocketPlatform_<Platform>.cpp)

#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdint>
#include <string>

namespace Marconi
{
    namespace Net
    {
        namespace Platform
        {
            constexpr intptr_t kInvalidSocket = -1;

            void EnsureInitialised();
            void CloseSocket(intptr_t handle);
            void ShutdownSocket(intptr_t handle);
            bool SetNonBlocking(intptr_t handle, bool nonBlocking);
            void DisableSigPipe(intptr_t handle);
            // TCP keep-alive: after `idleSeconds` without traffic, probe every `intervalSeconds`, giving up (and
            // failing the connection) after `count` unanswered probes. Best effort: not every system takes every
            // setting.
            void EnableKeepAlive(intptr_t handle, int idleSeconds, int intervalSeconds, int count);
            int SendFlags();

            int LastError();
            bool IsConnectInProgress(int error);
            bool IsWouldBlock(int error); // a non-blocking send or receive that has nothing to do yet
            bool IsInterrupted(int error);
            std::string ErrorString(int error);

            // select()'s first argument: ignored on Windows, highest fd + 1 elsewhere.
            int SelectWidth(intptr_t handle);
        } // namespace Platform
    } // namespace Net
} // namespace Marconi
