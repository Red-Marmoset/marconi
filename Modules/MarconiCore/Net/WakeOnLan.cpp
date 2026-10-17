// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Wake-on-LAN: waking a sleeping computer on the local network with its hardware (MAC) address

#include <MarconiCore/Net/WakeOnLan.h>

#include "SocketPlatform.h"

#include <cctype>

namespace Marconi
{
    namespace Net
    {
        bool ParseMacAddress(const std::string& text, MacAddress& out)
        {
            std::string digits;
            for (char c : text)
            {
                if (std::isxdigit(static_cast<unsigned char>(c)))
                {
                    digits.push_back(c);
                }
                else if (c != ':' && c != '-' && c != '.' && c != ' ')
                {
                    return false;
                }
            }
            if (digits.size() != 12)
            {
                return false;
            }
            for (size_t i = 0; i < 6; ++i)
            {
                out[i] = static_cast<uint8_t>(std::stoul(digits.substr(i * 2, 2), nullptr, 16));
            }
            return true;
        }

        std::string FormatMacAddress(const MacAddress& mac)
        {
            static const char* digits = "0123456789ABCDEF";
            std::string text;
            for (size_t i = 0; i < mac.size(); ++i)
            {
                if (i > 0)
                {
                    text.push_back(':');
                }
                text.push_back(digits[mac[i] >> 4]);
                text.push_back(digits[mac[i] & 15]);
            }
            return text;
        }

        std::vector<uint8_t> BuildMagicPacket(const MacAddress& mac)
        {
            std::vector<uint8_t> packet(6, 0xFF);
            for (int i = 0; i < 16; ++i)
            {
                packet.insert(packet.end(), mac.begin(), mac.end());
            }
            return packet;
        }

        bool SendWakeOnLan(const std::string& macText, std::string& error)
        {
            MacAddress mac;
            if (!ParseMacAddress(macText, mac))
            {
                error = "'" + macText + "' isn't a hardware (MAC) address: expected six pairs like AA:BB:CC:DD:EE:FF";
                return false;
            }
            Platform::EnsureInitialised();
            intptr_t handle = static_cast<intptr_t>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
            if (handle == Platform::kInvalidSocket)
            {
                error = "Could not open a network socket: " + Platform::ErrorString(Platform::LastError());
                return false;
            }
            int on = 1;
#if defined(_WIN32)
            setsockopt(static_cast<SOCKET>(handle), SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
#else
            setsockopt(static_cast<int>(handle), SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
#endif
            std::vector<uint8_t> packet = BuildMagicPacket(mac);
            sockaddr_in address = {};
            address.sin_family = AF_INET;
            address.sin_port = htons(9);
            address.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            bool sent = false;
            for (int attempt = 0; attempt < 3; ++attempt) // a few copies: UDP may drop one
            {
#if defined(_WIN32)
                int result = sendto(static_cast<SOCKET>(handle), reinterpret_cast<const char*>(packet.data()),
                                    static_cast<int>(packet.size()), 0, reinterpret_cast<sockaddr*>(&address),
                                    sizeof(address));
#else
                ptrdiff_t result = sendto(static_cast<int>(handle), packet.data(), packet.size(), 0,
                                          reinterpret_cast<sockaddr*>(&address), sizeof(address));
#endif
                sent = sent || result == static_cast<decltype(result)>(packet.size());
            }
            if (!sent)
            {
                error = "Could not send the wake-up packet: " + Platform::ErrorString(Platform::LastError());
            }
            Platform::CloseSocket(handle);
            return sent;
        }
    } // namespace Net
} // namespace Marconi
