// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: reads the screen-sharing video stream from PipeWire. See PipeWireCapture.h.
//
// Formats are offered without a "modifier", which is how a PipeWire client says "shared memory, not GPU memory", so
// the desktop sends pictures in memory we can read (a memory block or file descriptor, mapped by PipeWire).
// Negotiation lets the desktop choose among several 32-bit and 24-bit RGB orders and any size; a new size arrives as
// a new format and is applied from the next picture.

#include "PipeWireCapture.h"

#include "PipeWireLite.h"

#include <Log/Log.h>

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace Marconi
{
    namespace Server
    {
        struct PipeWireCapture::Impl
        {
            const Pw::Api* api = nullptr;
            void* loop = nullptr;
            void* context = nullptr;
            void* core = nullptr;
            void* stream = nullptr;
            Pw::SpaHook hook;
            Pw::PwStreamEvents events;
            bool started = false;
            bool wantCursor = false;

            mutable std::mutex mutex;
            State state = State::Connecting;
            std::string failure;
            Pw::VideoFormat format;
            bool haveFormat = false;
            bool warnedUnreadable = false;

            std::vector<uint32_t> pixels;
            int32_t width = 0;
            int32_t height = 0;
            uint64_t sequence = 0;
            bool havePicture = false;

            // The pointer from the stream's metadata.
            Cursor cursorImage;
            bool cursorImageNew = false;
            bool cursorHidden = false;
            int32_t cursorX = 0;
            int32_t cursorY = 0;
            bool haveCursorPosition = false;

            static void OnStateChanged(void* data, int, int state, const char* error)
            {
                Impl* self = static_cast<Impl*>(data);
                std::lock_guard<std::mutex> lock(self->mutex);
                switch (state)
                {
                    case Pw::kStateError:
                        self->state = State::Failed;
                        self->failure = std::string("The screen-sharing stream stopped: ") +
                                        (error != nullptr ? error : "unknown reason");
                        break;
                    case Pw::kStateStreaming: self->state = State::Streaming; break;
                    case Pw::kStatePaused:
                        if (self->state != State::Failed)
                        {
                            self->state = State::Paused;
                        }
                        break;
                    case Pw::kStateUnconnected:
                        if (self->started && self->state != State::Failed && self->havePicture)
                        {
                            self->state = State::Failed;
                            self->failure = "The desktop ended the screen sharing.";
                        }
                        break;
                    default: break;
                }
            }

            static void OnParamChanged(void* data, uint32_t id, const Pw::SpaPod* param)
            {
                Impl* self = static_cast<Impl*>(data);
                if (param == nullptr || id != Pw::kParamFormat)
                {
                    return;
                }
                Pw::VideoFormat format;
                if (!Pw::ParseVideoFormat(param, format) || format.width == 0 || format.height == 0)
                {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(self->mutex);
                    self->format = format;
                    self->haveFormat = true;
                }
                // Say which kinds of memory and extra data we accept: shared memory only, and the pointer's image.
                const uint32_t bytesPerPixel = (format.format == Pw::kVideoRGB || format.format == Pw::kVideoBGR) ? 3 : 4;
                const int32_t stride = static_cast<int32_t>((format.width * bytesPerPixel + 15) & ~15u);
                Pw::PodBuilder buffers;
                buffers.BeginObject(Pw::kObjectParamBuffers, Pw::kParamBuffers);
                buffers.AddIntChoiceRange(Pw::kBuffersBuffers, 4, 2, 16);
                buffers.AddInt(Pw::kBuffersBlocks, 1);
                buffers.AddInt(Pw::kBuffersSize, stride * static_cast<int32_t>(format.height));
                buffers.AddInt(Pw::kBuffersStride, stride);
                buffers.AddInt(Pw::kBuffersDataType, (1 << Pw::kDataMemPtr) | (1 << Pw::kDataMemFd));
                const Pw::SpaPod* bufferPod = buffers.Finish();
                std::vector<uint8_t> bufferCopy(reinterpret_cast<const uint8_t*>(bufferPod),
                                                reinterpret_cast<const uint8_t*>(bufferPod) + 8 + bufferPod->size);

                Pw::PodBuilder meta;
                meta.BeginObject(Pw::kObjectParamMeta, Pw::kParamMeta);
                meta.AddId(Pw::kMetaType, Pw::kMetaCursor);
                const int32_t cursorBytes = static_cast<int32_t>(sizeof(Pw::SpaMetaCursor) + sizeof(Pw::SpaMetaBitmap) +
                                                                 256 * 256 * 4);
                meta.AddIntChoiceRange(Pw::kMetaSize, cursorBytes, static_cast<int32_t>(sizeof(Pw::SpaMetaCursor) +
                                                                                          sizeof(Pw::SpaMetaBitmap) + 64 * 64 * 4),
                                       cursorBytes);
                const Pw::SpaPod* metaPod = meta.Finish();

                const Pw::SpaPod* params[2] = {reinterpret_cast<const Pw::SpaPod*>(bufferCopy.data()), metaPod};
                self->api->streamUpdateParams(self->stream, params, self->wantCursor ? 2 : 1);
            }

            static void OnProcess(void* data)
            {
                Impl* self = static_cast<Impl*>(data);
                // Take the newest picture and hand the older ones straight back.
                Pw::PwBuffer* newest = nullptr;
                while (Pw::PwBuffer* next = self->api->streamDequeue(self->stream))
                {
                    if (newest != nullptr)
                    {
                        self->api->streamQueue(self->stream, newest);
                    }
                    newest = next;
                }
                if (newest == nullptr)
                {
                    return;
                }
                self->Handle(*newest->buffer);
                self->api->streamQueue(self->stream, newest);
            }

            void Handle(const Pw::SpaBuffer& buffer)
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (wantCursor)
                {
                    for (uint32_t i = 0; i < buffer.nMetas; ++i)
                    {
                        if (buffer.metas[i].type == Pw::kMetaCursor && buffer.metas[i].data != nullptr &&
                            buffer.metas[i].size >= sizeof(Pw::SpaMetaCursor))
                        {
                            ReadCursor(buffer.metas[i]);
                        }
                    }
                }
                if (buffer.nDatas == 0 || !haveFormat)
                {
                    return;
                }
                const Pw::SpaData& data = buffer.datas[0];
                if (data.chunk == nullptr || data.chunk->size == 0 || (data.chunk->flags & 1) != 0)
                {
                    return; // pointer-only update, or a damaged picture
                }
                const uint8_t* base = static_cast<const uint8_t*>(data.data);
                void* mapped = nullptr;
                size_t mappedSize = 0;
                if (base == nullptr && data.type == Pw::kDataMemFd && data.fd >= 0)
                {
                    mappedSize = data.maxsize + data.mapoffset;
                    mapped = mmap(nullptr, mappedSize, PROT_READ, MAP_PRIVATE, static_cast<int>(data.fd), 0);
                    if (mapped != MAP_FAILED)
                    {
                        base = static_cast<const uint8_t*>(mapped) + data.mapoffset;
                    }
                    else
                    {
                        mapped = nullptr;
                    }
                }
                if (base == nullptr)
                {
                    if (!warnedUnreadable)
                    {
                        warnedUnreadable = true;
                        failure = "The desktop is sharing the screen as GPU memory, which Marconi Server can't read "
                                  "yet.";
                        state = State::Failed;
                        LOG_WARNING("Server", "Screen-sharing stream gave GPU memory (type %u), not readable.",
                                    data.type);
                    }
                    return;
                }
                const int32_t stride = data.chunk->stride > 0
                                           ? data.chunk->stride
                                           : static_cast<int32_t>(format.width) *
                                                 ((format.format == Pw::kVideoRGB || format.format == Pw::kVideoBGR) ? 3 : 4);
                const uint8_t* rows = base + data.chunk->offset % std::max<uint32_t>(data.maxsize, 1);
                const size_t bytesPerPixel = (format.format == Pw::kVideoRGB || format.format == Pw::kVideoBGR) ? 3 : 4;
                const size_t needed = static_cast<size_t>(stride) * (format.height - 1) + format.width * bytesPerPixel;
                if (data.chunk->size >= needed || data.maxsize >= needed)
                {
                    if (ConvertRows(format.format, rows, stride, static_cast<int32_t>(format.width),
                                    static_cast<int32_t>(format.height), pixels))
                    {
                        width = static_cast<int32_t>(format.width);
                        height = static_cast<int32_t>(format.height);
                        havePicture = true;
                        ++sequence;
                        if (state == State::Connecting || state == State::Paused)
                        {
                            state = State::Streaming;
                        }
                    }
                }
                if (mapped != nullptr)
                {
                    munmap(mapped, mappedSize);
                }
            }

            // Called with the mutex held.
            void ReadCursor(const Pw::SpaMeta& meta)
            {
                const auto* cursor = static_cast<const Pw::SpaMetaCursor*>(meta.data);
                if (cursor->id == 0)
                {
                    return; // nothing new
                }
                cursorX = cursor->position.x;
                cursorY = cursor->position.y;
                haveCursorPosition = true;
                if (cursor->bitmapOffset < sizeof(Pw::SpaMetaCursor) ||
                    cursor->bitmapOffset + sizeof(Pw::SpaMetaBitmap) > meta.size)
                {
                    return; // same shape as before
                }
                const auto* bitmap = reinterpret_cast<const Pw::SpaMetaBitmap*>(
                    static_cast<const uint8_t*>(meta.data) + cursor->bitmapOffset);
                if (bitmap->format == 0)
                {
                    return; // no new bitmap information
                }
                Cursor image;
                image.hotspotX = cursor->hotspot.x;
                image.hotspotY = cursor->hotspot.y;
                if (bitmap->format != 0 && bitmap->offset >= sizeof(Pw::SpaMetaBitmap) && bitmap->width > 0 &&
                    bitmap->height > 0 && bitmap->width <= 512 && bitmap->height <= 512)
                {
                    const uint8_t* pixelData = reinterpret_cast<const uint8_t*>(bitmap) + bitmap->offset;
                    const size_t end = cursor->bitmapOffset + bitmap->offset +
                                       static_cast<size_t>(bitmap->stride) * (bitmap->height - 1) + bitmap->width * 4;
                    if (bitmap->stride >= static_cast<int32_t>(bitmap->width * 4) && end <= meta.size)
                    {
                        std::vector<uint32_t> argb;
                        if (ConvertRows(bitmap->format, pixelData, bitmap->stride,
                                        static_cast<int32_t>(bitmap->width), static_cast<int32_t>(bitmap->height),
                                        argb))
                        {
                            // ConvertRows drops alpha; read it back from the source for the cursor's cut-out.
                            image.width = static_cast<int32_t>(bitmap->width);
                            image.height = static_cast<int32_t>(bitmap->height);
                            image.argb.resize(argb.size());
                            for (uint32_t y = 0; y < bitmap->height; ++y)
                            {
                                const uint8_t* row = pixelData + static_cast<size_t>(y) * bitmap->stride;
                                for (uint32_t x = 0; x < bitmap->width; ++x)
                                {
                                    uint8_t alpha = AlphaOf(bitmap->format, row + x * 4);
                                    uint32_t rgb = argb[y * bitmap->width + x] & 0x00FFFFFFu;
                                    image.argb[y * bitmap->width + x] = (alpha >= 128 ? 0xFF000000u : 0u) | rgb;
                                }
                            }
                        }
                    }
                }
                // A bitmap with no pixels means the pointer is hidden.
                cursorImage = std::move(image);
                cursorImageNew = true;
            }

            static uint8_t AlphaOf(uint32_t format, const uint8_t* pixel)
            {
                switch (format)
                {
                    case Pw::kVideoBGRA:
                    case Pw::kVideoRGBA: return pixel[3];
                    case Pw::kVideoARGB:
                    case Pw::kVideoABGR: return pixel[0];
                    default: return 255; // formats with no alpha
                }
            }
        };

        bool PipeWireCapture::ConvertRows(uint32_t format, const uint8_t* source, int32_t stride, int32_t width,
                                          int32_t height, std::vector<uint32_t>& out)
        {
            if (width <= 0 || height <= 0)
            {
                return false;
            }
            out.resize(static_cast<size_t>(width) * height);
            for (int32_t y = 0; y < height; ++y)
            {
                const uint8_t* row = source + static_cast<size_t>(y) * stride;
                uint32_t* destination = out.data() + static_cast<size_t>(y) * width;
                switch (format)
                {
                    case Pw::kVideoBGRx:
                    case Pw::kVideoBGRA:
                        for (int32_t x = 0; x < width; ++x)
                        {
                            uint32_t v;
                            std::memcpy(&v, row + x * 4, 4);
                            destination[x] = v & 0x00FFFFFFu;
                        }
                        break;
                    case Pw::kVideoRGBx:
                    case Pw::kVideoRGBA:
                        for (int32_t x = 0; x < width; ++x)
                        {
                            const uint8_t* p = row + x * 4;
                            destination[x] = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
                        }
                        break;
                    case Pw::kVideoxRGB:
                    case Pw::kVideoARGB:
                        for (int32_t x = 0; x < width; ++x)
                        {
                            const uint8_t* p = row + x * 4;
                            destination[x] = (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
                        }
                        break;
                    case Pw::kVideoxBGR:
                    case Pw::kVideoABGR:
                        for (int32_t x = 0; x < width; ++x)
                        {
                            const uint8_t* p = row + x * 4;
                            destination[x] = (uint32_t(p[3]) << 16) | (uint32_t(p[2]) << 8) | p[1];
                        }
                        break;
                    case Pw::kVideoRGB:
                        for (int32_t x = 0; x < width; ++x)
                        {
                            const uint8_t* p = row + x * 3;
                            destination[x] = (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
                        }
                        break;
                    case Pw::kVideoBGR:
                        for (int32_t x = 0; x < width; ++x)
                        {
                            const uint8_t* p = row + x * 3;
                            destination[x] = (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0];
                        }
                        break;
                    default: return false;
                }
            }
            return true;
        }

        PipeWireCapture::PipeWireCapture() : m_impl(std::make_unique<Impl>()) {}

        PipeWireCapture::~PipeWireCapture() { Stop(); }

        bool PipeWireCapture::Start(int remoteFd, uint32_t nodeId, bool wantCursorMetadata, std::string& error)
        {
            Impl& impl = *m_impl;
            impl.api = Pw::GetApi(error);
            if (impl.api == nullptr)
            {
                if (remoteFd >= 0)
                {
                    close(remoteFd);
                }
                return false;
            }
            const Pw::Api& api = *impl.api;
            impl.wantCursor = wantCursorMetadata;
            impl.loop = api.threadLoopNew("marconi-capture", nullptr);
            if (impl.loop == nullptr)
            {
                error = "Could not start PipeWire's processing thread.";
                if (remoteFd >= 0)
                {
                    close(remoteFd);
                }
                return false;
            }
            impl.context = api.contextNew(api.threadLoopGetLoop(impl.loop), nullptr, 0);
            if (impl.context == nullptr)
            {
                error = "Could not set up PipeWire.";
                if (remoteFd >= 0)
                {
                    close(remoteFd);
                }
                Stop();
                return false;
            }
            // The fd belongs to pw_context_connect_fd from here, even when it fails.
            impl.core = remoteFd >= 0 ? api.contextConnectFd(impl.context, remoteFd, nullptr, 0)
                                      : api.contextConnect(impl.context, nullptr, 0);
            if (impl.core == nullptr)
            {
                error = "Could not connect to PipeWire.";
                Stop();
                return false;
            }
            void* properties = api.propertiesNew("media.type", "Video", "media.category", "Capture", "media.role",
                                                 "Screen", nullptr);
            impl.stream = api.streamNew(impl.core, "marconi-server", properties);
            if (impl.stream == nullptr)
            {
                error = "Could not create the screen-sharing stream.";
                Stop();
                return false;
            }
            std::memset(&impl.events, 0, sizeof(impl.events));
            impl.events.version = Pw::kStreamEventsVersion;
            impl.events.stateChanged = &Impl::OnStateChanged;
            impl.events.paramChanged = &Impl::OnParamChanged;
            impl.events.process = &Impl::OnProcess;
            std::memset(&impl.hook, 0, sizeof(impl.hook));
            api.streamAddListener(impl.stream, &impl.hook, &impl.events, &impl);

            Pw::PodBuilder builder;
            builder.BeginObject(Pw::kObjectFormat, Pw::kParamEnumFormat);
            builder.AddId(Pw::kFormatMediaType, Pw::kMediaTypeVideo);
            builder.AddId(Pw::kFormatMediaSubtype, Pw::kMediaSubtypeRaw);
            builder.AddIdChoiceEnum(Pw::kFormatVideoFormat, Pw::kVideoBGRx,
                                    {Pw::kVideoBGRx, Pw::kVideoBGRA, Pw::kVideoRGBx, Pw::kVideoRGBA, Pw::kVideoxRGB,
                                     Pw::kVideoARGB, Pw::kVideoxBGR, Pw::kVideoABGR, Pw::kVideoBGR, Pw::kVideoRGB});
            builder.AddRectangleChoiceRange(Pw::kFormatVideoSize, 1920, 1080, 1, 1, 16384, 16384);
            builder.AddFractionChoiceRange(Pw::kFormatVideoFramerate, 60, 1, 0, 1, 1000, 1);
            const Pw::SpaPod* params[1] = {builder.Finish()};

            api.threadLoopLock(impl.loop);
            impl.started = true;
            int result = api.streamConnect(impl.stream, Pw::kDirectionInput, nodeId,
                                           Pw::kFlagAutoconnect | Pw::kFlagMapBuffers, params, 1);
            api.threadLoopUnlock(impl.loop);
            if (result < 0)
            {
                error = "Could not connect to the screen-sharing stream.";
                Stop();
                return false;
            }
            if (api.threadLoopStart(impl.loop) < 0)
            {
                error = "Could not start PipeWire's processing thread.";
                Stop();
                return false;
            }
            return true;
        }

        void PipeWireCapture::Stop()
        {
            Impl& impl = *m_impl;
            if (impl.api == nullptr)
            {
                return;
            }
            const Pw::Api& api = *impl.api;
            if (impl.loop != nullptr)
            {
                api.threadLoopStop(impl.loop);
            }
            if (impl.stream != nullptr)
            {
                api.streamDestroy(impl.stream);
                impl.stream = nullptr;
            }
            if (impl.core != nullptr)
            {
                api.coreDisconnect(impl.core);
                impl.core = nullptr;
            }
            if (impl.context != nullptr)
            {
                api.contextDestroy(impl.context);
                impl.context = nullptr;
            }
            if (impl.loop != nullptr)
            {
                api.threadLoopDestroy(impl.loop);
                impl.loop = nullptr;
            }
            impl.started = false;
        }

        PipeWireCapture::State PipeWireCapture::GetState() const
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            return m_impl->state;
        }

        std::string PipeWireCapture::GetFailure() const
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            return m_impl->failure;
        }

        bool PipeWireCapture::GetLatest(std::vector<uint32_t>& pixels, int32_t& width, int32_t& height,
                                        uint64_t& sequence)
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            if (!m_impl->havePicture)
            {
                return false;
            }
            if (sequence != m_impl->sequence || pixels.size() != m_impl->pixels.size())
            {
                pixels = m_impl->pixels;
            }
            width = m_impl->width;
            height = m_impl->height;
            sequence = m_impl->sequence;
            return true;
        }

        bool PipeWireCapture::GetSize(int32_t& width, int32_t& height) const
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            if (!m_impl->havePicture)
            {
                return false;
            }
            width = m_impl->width;
            height = m_impl->height;
            return true;
        }

        bool PipeWireCapture::GetCursor(Cursor& out)
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            if (!m_impl->cursorImageNew)
            {
                return false;
            }
            out = m_impl->cursorImage;
            m_impl->cursorImageNew = false;
            return true;
        }

        bool PipeWireCapture::GetCursorPosition(int32_t& x, int32_t& y)
        {
            std::lock_guard<std::mutex> lock(m_impl->mutex);
            if (!m_impl->haveCursorPosition)
            {
                return false;
            }
            x = m_impl->cursorX;
            y = m_impl->cursorY;
            return true;
        }
    } // namespace Server
} // namespace Marconi
