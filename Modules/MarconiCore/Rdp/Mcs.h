// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The MCS layer (T.125) and the GCC conference wrapper (T.124) that RDP uses for its connection sequence: the
// Connect-Initial carrying the client data, the Connect-Response, then the small domain PDUs (erect domain, attach
// user, channel join) and the Send Data framing that the RDP PDUs ride inside. RDP milestone 3.

#pragma once

#include <MarconiCore/Rdp/ClientData.h>

#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        // Wraps `payload` in a TPKT header and an X.224 Data header: the transport every MCS PDU travels in once
        // the connection is confirmed. Reading one is FrameX224Data's inverse, done by the connection code which
        // owns the stream.
        Bytes FrameX224Data(const Bytes& payload);
        // Strips the 4-byte TPKT header and the 3-byte X.224 Data header from a complete TPKT frame. False if the
        // frame isn't well formed; `payload` is the MCS PDU inside.
        bool UnframeX224Data(const Bytes& frame, Bytes& payload);

        // The whole MCS Connect-Initial (BER) wrapping a GCC Conference Create Request (PER) whose user data is
        // `clientData` (BuildClientData's output). This is one X.224 Data payload.
        Bytes BuildMcsConnectInitial(const Bytes& clientData);

        // Pulls the GCC Conference Create Response's user data (the server data blocks) out of a Connect-Response.
        // False if the bytes aren't a Connect-Response carrying a conference-create response.
        bool ParseMcsConnectResponse(const Bytes& connectResponse, Bytes& serverUserData);

        // The tiny domain PDUs (no TPKT/X.224 here; the caller frames them).
        Bytes BuildErectDomainRequest();
        Bytes BuildAttachUserRequest();
        // The Attach User Confirm gives the user its channel id. False if the PDU isn't one, or it reported failure.
        bool ParseAttachUserConfirm(const Bytes& pdu, uint16_t& userChannelId);
        Bytes BuildChannelJoinRequest(uint16_t userChannelId, uint16_t channelId);
        // The Channel Join Confirm echoes the channel joined. False if not a confirm or it reported failure.
        bool ParseChannelJoinConfirm(const Bytes& pdu, uint16_t& channelId);

        // Send Data Request wraps an RDP PDU for a channel (client -> server). The matching Send Data Indication
        // (server -> client) is unwrapped by StripSendData, which returns the inner PDU and the channel it came on.
        Bytes BuildSendDataRequest(uint16_t userChannelId, uint16_t channelId, const Bytes& payload);
        bool StripSendData(const Bytes& pdu, uint16_t& channelId, Bytes& payload);
    } // namespace Rdp
} // namespace Marconi
