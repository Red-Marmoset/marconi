// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Remote Desktop client's fast-path output and its "drdynvc" channel: fast-path updates put back together
// from fragments, the dynamic channels, and the graphics pipeline riding on one of them.

#include <MarconiCore/Rdp/RdpClient.h>

#include <MarconiCore/Rdp/Connect.h>
#include <MarconiCore/Rdp/Gfx.h>
#include <MarconiCore/Rdp/Pointer.h>

#include <Log/Log.h>

#include <algorithm>

namespace Marconi
{
    namespace Rdp
    {
        void Client::HandleFastPath(const Bytes& payload)
        {
            std::vector<FastPathUpdate> updates;
            std::string error;
            if (!ParseFastPathUpdates(payload, updates, error))
            {
                LOG_WARNING("Rdp", "Ignoring a fast-path update the server sent: %s", error.c_str());
                return;
            }
            constexpr size_t kMaxPieces = 8 * 1024 * 1024; // what the Multifragment Update capability allows
            for (FastPathUpdate& update : updates)
            {
                switch (update.fragmentation)
                {
                    case 0: // whole
                        HandleFastPathUpdate(update);
                        break;
                    case 2: // the first piece
                        m_fastPathPieces = std::move(update.data);
                        m_fastPathInPieces = true;
                        break;
                    case 3: // a middle piece
                    case 1: // the last piece
                    {
                        if (!m_fastPathInPieces || m_fastPathPieces.size() + update.data.size() > kMaxPieces)
                        {
                            LOG_WARNING("Rdp", "Ignoring a piece of a fast-path update that doesn't fit together");
                            m_fastPathInPieces = false;
                            m_fastPathPieces.clear();
                            break;
                        }
                        m_fastPathPieces.insert(m_fastPathPieces.end(), update.data.begin(), update.data.end());
                        if (update.fragmentation == 1)
                        {
                            FastPathUpdate whole;
                            whole.code = update.code;
                            whole.data = std::move(m_fastPathPieces);
                            m_fastPathPieces.clear();
                            m_fastPathInPieces = false;
                            if (InterpretFastPathUpdate(whole, error))
                            {
                                HandleFastPathUpdate(whole);
                            }
                            else
                            {
                                LOG_WARNING("Rdp", "Ignoring a fast-path update the server sent: %s", error.c_str());
                            }
                        }
                        break;
                    }
                }
            }
        }

        void Client::HandleFastPathUpdate(const FastPathUpdate& update)
        {
            std::string error;
            switch (update.type)
            {
                case FastPathUpdateType::Bitmap:
                    ApplyBitmapUpdate(update.rects);
                    break;
                case FastPathUpdateType::Palette:
                    m_palette = update.palette;
                    break;
                case FastPathUpdateType::Pointer:
                {
                    PointerUpdate pointer;
                    if (ParseFastPathPointer(update.code, update.data, pointer, error))
                    {
                        ApplyPointerUpdate(pointer);
                    }
                    else
                    {
                        LOG_WARNING("Rdp", "Ignoring a pointer update the server sent: %s", error.c_str());
                    }
                    break;
                }
                case FastPathUpdateType::Other:
                    break;
            }
        }

        void Client::HandleDrdynvc(const Bytes& payload, DisconnectInfo& info, bool& finished)
        {
            Bytes message;
            std::string error;
            if (!m_drdynvcChunks.Add(payload, message, error))
            {
                if (!error.empty())
                {
                    LOG_WARNING("Rdp", "Ignoring a dynamic channel message: %s", error.c_str());
                }
                return;
            }
            DvcPdu pdu;
            if (!ParseDvcPdu(message, pdu, error))
            {
                LOG_WARNING("Rdp", "Ignoring a dynamic channel message: %s", error.c_str());
                return;
            }
            HandleDynamicMessage(pdu, info, finished);
        }

        void Client::HandleDynamicMessage(const DvcPdu& pdu, DisconnectInfo& info, bool& finished)
        {
            constexpr int32_t kStatusUnsuccessful = static_cast<int32_t>(0xC0000001);
            switch (pdu.command)
            {
                case DvcCommand::Capability:
                {
                    // Version 3 adds compressed channel data, which Marconi doesn't decode: answer 2 at most.
                    uint16_t version = std::clamp<uint16_t>(pdu.version, 1, 2);
                    LOG_INFO("Rdp", "Dynamic channels offered (version %u); answering version %u", pdu.version,
                             version);
                    SendDrdynvc(BuildDvcCapabilityResponse(version));
                    return;
                }
                case DvcCommand::Create:
                {
                    bool isGfx = pdu.channelName == kGfxChannelName && !m_gfxOpen && m_gfx;
                    SendDrdynvc(BuildDvcCreateResponse(pdu.channelId, isGfx ? 0 : kStatusUnsuccessful));
                    if (!isGfx)
                    {
                        LOG_INFO("Rdp", "Declined the dynamic channel \"%s\"", pdu.channelName.c_str());
                        return;
                    }
                    m_gfxChannelId = pdu.channelId;
                    m_gfxOpen = true;
                    m_zgfx = std::make_unique<ZgfxDecompressor>();
                    LOG_INFO("Rdp", "Graphics pipeline channel open (%u); advertising capabilities", pdu.channelId);
                    for (const Bytes& data : BuildDvcData(m_gfxChannelId, BuildGfxCapsAdvertise()))
                    {
                        SendDrdynvc(data);
                    }
                    return;
                }
                case DvcCommand::DataFirst:
                case DvcCommand::Data:
                {
                    Bytes message;
                    std::string error;
                    if (!m_dvcPieces.Add(pdu, message, error))
                    {
                        if (!error.empty())
                        {
                            LOG_WARNING("Rdp", "Ignoring dynamic channel data: %s", error.c_str());
                        }
                        return;
                    }
                    if (m_gfxOpen && pdu.channelId == m_gfxChannelId)
                    {
                        HandleGfxMessage(message, info, finished);
                    }
                    return;
                }
                case DvcCommand::Close:
                    m_dvcPieces.Forget(pdu.channelId);
                    SendDrdynvc(BuildDvcClose(pdu.channelId));
                    if (m_gfxOpen && pdu.channelId == m_gfxChannelId)
                    {
                        LOG_INFO("Rdp", "The server closed the graphics pipeline channel");
                        m_gfxOpen = false;
                        m_zgfx.reset();
                        m_gfx->Reset();
                    }
                    return;
                default:
                    return;
            }
        }

        void Client::HandleGfxMessage(const Bytes& message, DisconnectInfo& info, bool& finished)
        {
            Bytes plain;
            std::vector<GfxPdu> pdus;
            std::string error;
            GfxRenderer::Result result;
            if (!m_zgfx->Decompress(message.data(), message.size(), plain, error) ||
                !ParseGfxPdus(plain, pdus, error) || !m_gfx->Handle(pdus, result, error))
            {
                LOG_WARNING("Rdp", "Graphics pipeline failure: %s", error.c_str());
                info.error = "The picture from the other computer couldn't be read (" + error + ").";
                finished = true;
                return;
            }
            if (!result.changed.empty())
            {
                ++m_statUpdates;
                m_statRectangles += static_cast<int>(result.changed.size());
                m_listener.OnFramebufferUpdated(result.changed);
            }
            for (const Bytes& reply : result.replies)
            {
                for (const Bytes& data : BuildDvcData(m_gfxChannelId, reply))
                {
                    SendDrdynvc(data);
                }
            }
        }

        bool Client::SendDrdynvc(const Bytes& message)
        {
            std::lock_guard<std::mutex> lock(m_sendMutex);
            std::shared_ptr<Connection> connection;
            {
                std::lock_guard<std::mutex> connectionLock(m_connectionMutex);
                connection = m_connection;
            }
            if (!connection || m_drdynvcChannel == 0)
            {
                return false;
            }
            std::string error;
            return connection->SendChannelMessage(m_drdynvcChannel, message, error);
        }
    } // namespace Rdp
} // namespace Marconi
