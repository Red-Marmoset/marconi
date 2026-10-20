// Copyright (c) 2026 Red Marmoset Studios Ltd.
// RDP milestone 1: the security negotiation over a byte link, before any TLS upgrade

#include <MarconiCore/Rdp/Security.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            // Reads exactly `length` bytes, looping over short reads. False if the stream ends or errors first.
            bool ReadFully(const SecurityLink& link, uint8_t* buffer, size_t length)
            {
                size_t got = 0;
                while (got < length)
                {
                    ptrdiff_t n = link.receive(buffer + got, length - got);
                    if (n <= 0)
                    {
                        return false;
                    }
                    got += static_cast<size_t>(n);
                }
                return true;
            }
        } // namespace

        bool NegotiateSecurity(const SecurityLink& link, uint32_t requestedProtocols, const std::string& cookie,
                               NegotiationResponse& out, std::string& error)
        {
            Bytes request = BuildConnectionRequest(requestedProtocols, cookie);
            if (!link.sendAll(request.data(), request.size()))
            {
                error = "Couldn't send the RDP connection request";
                return false;
            }
            uint8_t header[4];
            if (!ReadFully(link, header, sizeof(header)))
            {
                error = "The server closed the connection without answering the RDP request";
                return false;
            }
            size_t total = TpktLength(header);
            if (total < 7 || total > 4096) // a Connection Confirm is tiny; anything large is wrong
            {
                error = "The server's RDP reply had an implausible length";
                return false;
            }
            Bytes packet(header, header + 4);
            packet.resize(total);
            if (!ReadFully(link, packet.data() + 4, total - 4))
            {
                error = "The server's RDP reply was cut short";
                return false;
            }
            return ParseConnectionConfirm(packet, out, error);
        }
    } // namespace Rdp
} // namespace Marconi
