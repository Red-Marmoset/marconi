// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Runs the Extended Clipboard exchange with a server: capabilities, offering what we have, fetching what it has

#include <MarconiCore/Rfb/ClipboardChannel.h>

#include <MarconiCore/Rfb/ExtendedClipboard.h>

#include <Log/Log.h>

#include <map>

namespace Marconi
{
    namespace Rfb
    {
        namespace
        {
            using namespace ExtendedClipboard;

            constexpr uint32_t kOurFormats = Format::Text | Format::Dib;
            constexpr uint32_t kOurActions = Action::Request | Action::Peek | Action::Notify | Action::Provide;
            constexpr size_t kMaxProvideBytes = ClipboardChannel::kMaxTextBytes + ClipboardChannel::kMaxDibBytes + 64;
        } // namespace

        ClipboardChannel::ClipboardChannel(SendFunction send, ContentHandler onContent)
            : m_send(std::move(send)), m_onContent(std::move(onContent))
        {
        }

        void ClipboardChannel::Reset()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_active = false;
            m_serverFormats = 0;
            m_pending = ClipboardContent();
        }

        bool ClipboardChannel::IsActive() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_active;
        }

        bool ClipboardChannel::AcceptsImages() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_active && (m_serverFormats & Format::Dib) != 0;
        }

        uint32_t ClipboardChannel::FormatsOf(const ClipboardContent& content) const
        {
            uint32_t formats = 0;
            if (!content.utf8Text.empty())
            {
                formats |= Format::Text;
            }
            if (!content.dib.empty())
            {
                formats |= Format::Dib;
            }
            return formats & m_serverFormats;
        }

        void ClipboardChannel::SendProvide(uint32_t formats)
        {
            std::map<uint32_t, Bytes> data;
            if ((formats & Format::Text) != 0)
            {
                data[Format::Text] = TextToWire(m_pending.utf8Text);
            }
            if ((formats & Format::Dib) != 0)
            {
                data[Format::Dib] = m_pending.dib;
            }
            if (data.empty())
            {
                return;
            }
            Bytes message = WrapClientMessage(BuildProvide(data));
            LOG_INFO("Rfb", "Sent our clipboard to the server (%zu bytes, formats 0x%x)", message.size(), formats);
            m_send(message);
        }

        void ClipboardChannel::HandleServerMessage(const Bytes& body)
        {
            Message message = Parse(body, kMaxProvideBytes);
            ClipboardContent delivered;
            bool haveContent = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (message.Has(Action::Caps))
                {
                    m_serverFormats = 0;
                    for (const auto& entry : message.maxSizes)
                    {
                        if (entry.second > 0)
                        {
                            m_serverFormats |= entry.first;
                        }
                    }
                    m_serverFormats &= kOurFormats;
                    m_active = (m_serverFormats & Format::Text) != 0;
                    LOG_INFO("Rfb", "The server supports the extended clipboard (formats 0x%x); %s", message.GetFormats(),
                             m_active ? "text travels as UTF-8" : "but not for text, so it isn't used");
                    std::map<uint32_t, uint32_t> ours = {{Format::Text, kMaxTextBytes}, {Format::Dib, kMaxDibBytes}};
                    m_send(WrapClientMessage(BuildCaps(ours, kOurActions)));
                    return;
                }
                if (!m_active)
                {
                    return;
                }
                if (message.Has(Action::Notify))
                {
                    uint32_t wanted = message.GetFormats() & m_serverFormats;
                    if (wanted != 0)
                    {
                        m_send(WrapClientMessage(BuildRequest(wanted)));
                    }
                }
                else if (message.Has(Action::Request))
                {
                    SendProvide(message.GetFormats() & FormatsOf(m_pending));
                }
                else if (message.Has(Action::Peek))
                {
                    m_send(WrapClientMessage(BuildNotify(FormatsOf(m_pending))));
                }
                else if (message.Has(Action::Provide))
                {
                    auto text = message.data.find(Format::Text);
                    if (text != message.data.end())
                    {
                        delivered.utf8Text = TextFromWire(text->second);
                    }
                    auto dib = message.data.find(Format::Dib);
                    if (dib != message.data.end())
                    {
                        delivered.dib = std::move(dib->second);
                    }
                    haveContent = !delivered.IsEmpty();
                    LOG_INFO("Rfb", "The server sent its clipboard (%zu characters, %zu image bytes)",
                             delivered.utf8Text.size(), delivered.dib.size());
                }
            }
            if (haveContent)
            {
                m_onContent(delivered);
            }
        }

        bool ClipboardChannel::Offer(const ClipboardContent& content)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_active)
            {
                return false;
            }
            uint32_t formats = FormatsOf(content);
            if (formats == 0)
            {
                return false;
            }
            m_pending = content;
            m_send(WrapClientMessage(BuildNotify(formats)));
            return true;
        }

        bool ClipboardChannel::RequestFromServer()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_active)
            {
                return false;
            }
            m_send(WrapClientMessage(BuildRequest(m_serverFormats)));
            return true;
        }
    } // namespace Rfb
} // namespace Marconi
