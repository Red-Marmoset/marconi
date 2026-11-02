// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Virtual channels: the chunking every static channel's messages travel in (MS-RDPBCGR 2.2.6.1, 3.1.5.2), and the
// dynamic channels carried inside the "drdynvc" static channel (MS-RDPEDYC), which the graphics pipeline rides on.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        using Bytes = std::vector<uint8_t>;

        // CHANNEL_DEF options for the channels Marconi asks for in its client data.
        constexpr uint32_t kChannelOptionInitialized = 0x80000000;
        constexpr uint32_t kChannelOptionEncryptRdp = 0x40000000;
        constexpr uint32_t kChannelOptionCompressRdp = 0x00800000;
        constexpr const char* kDrdynvcChannelName = "drdynvc";

        // The largest piece of a static channel message the client sends (the default VCChunkSize).
        constexpr size_t kChannelChunkSize = 1600;

        // Puts together a static channel's messages from the chunks they arrive in. One per channel.
        class ChannelReassembler
        {
        public:
            // Takes one Send Data payload for the channel (a CHANNEL_PDU_HEADER and a chunk). True when that chunk
            // finishes a message, which is then in `message`. False with `error` (and `message` untouched) if the
            // chunk is malformed; with an empty `error` it just isn't the last chunk.
            bool Add(const Bytes& payload, Bytes& message, std::string& error);

        private:
            Bytes m_pending;
            uint32_t m_expected = 0;
            bool m_inMessage = false;
        };

        // Splits a static channel message into Send Data payloads (CHANNEL_PDU_HEADER plus up to kChannelChunkSize
        // bytes each), first and last flagged.
        std::vector<Bytes> BuildChannelChunks(const Bytes& message);

        // DRDYNVC PDU commands (the top 4 bits of the header byte).
        enum class DvcCommand : uint8_t
        {
            Create = 0x01,
            DataFirst = 0x02,
            Data = 0x03,
            Close = 0x04,
            Capability = 0x05,
            DataFirstCompressed = 0x06,
            DataCompressed = 0x07,
            SoftSyncRequest = 0x08,
            SoftSyncResponse = 0x09,
        };

        // One DRDYNVC message from the server, after the static channel has reassembled it.
        struct DvcPdu
        {
            DvcCommand command = DvcCommand::Capability;
            uint32_t channelId = 0;   // Create, DataFirst, Data, Close
            uint16_t version = 0;     // Capability: the highest version the server speaks
            std::string channelName;  // Create
            uint32_t totalLength = 0; // DataFirst: the size of the whole message the data starts
            Bytes data;               // DataFirst, Data
        };

        // Parses a DRDYNVC message. False with `error` if malformed.
        bool ParseDvcPdu(const Bytes& message, DvcPdu& out, std::string& error);

        // The client's DRDYNVC messages (each a whole static channel message, to be chunked for sending).
        Bytes BuildDvcCapabilityResponse(uint16_t version);
        Bytes BuildDvcCreateResponse(uint32_t channelId, int32_t creationStatus);
        Bytes BuildDvcClose(uint32_t channelId);
        // `data` for a dynamic channel: one Data PDU, or a DataFirst and Data PDUs when it doesn't fit one chunk.
        std::vector<Bytes> BuildDvcData(uint32_t channelId, const Bytes& data);

        // Puts together dynamic channel messages split into DataFirst and Data PDUs, for every channel.
        class DvcReassembler
        {
        public:
            // Takes a DataFirst or Data PDU. True when it finishes a message for `pdu.channelId`, which is then in
            // `message`. False with `error` set if the pieces don't add up; false with no error while waiting.
            bool Add(const DvcPdu& pdu, Bytes& message, std::string& error);
            void Forget(uint32_t channelId) { m_pending.erase(channelId); }

        private:
            struct Pending
            {
                uint32_t expected = 0;
                Bytes data;
            };
            std::map<uint32_t, Pending> m_pending;
        };
    } // namespace Rdp
} // namespace Marconi
