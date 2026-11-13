// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: the fake screen of the Wayland tests, a PipeWire video source. See WaylandFakes.h.

#include "WaylandFakes.h"

#if defined(__linux__)

#include <MarconiServerCore/PipeWireLite.h>

#include <condition_variable>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>

namespace MarconiServerTest
{
    using namespace Marconi::Server;

    std::string FindPipeWireSocket()
    {
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        if (runtime == nullptr || *runtime == '\0')
        {
            return std::string();
        }
        std::string path = std::string(runtime) + "/pipewire-0";
        struct stat info;
        return stat(path.c_str(), &info) == 0 && S_ISSOCK(info.st_mode) ? path : std::string();
    }

    struct FakeScreen::Impl
    {
        const Pw::Api* api = nullptr;
        void* loop = nullptr;
        void* context = nullptr;
        void* core = nullptr;
        void* stream = nullptr;
        Pw::SpaHook hook;
        Pw::PwStreamEvents events;
        std::thread driver;
        std::atomic<bool> stopDriver{false};
        std::atomic<uint64_t>* frames = nullptr;

        std::mutex mutex;
        std::condition_variable changed;
        bool connected = false;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t pixelFormat = Pw::kVideoBGRx;
        uint32_t negotiatedWidth = 0;
        uint32_t negotiatedHeight = 0;
        std::vector<uint32_t> image; // 0x00RRGGBB
        bool haveCursor = false;
        int cursorX = 0;
        int cursorY = 0;
        int hotX = 0;
        int hotY = 0;
        int cursorW = 0;
        int cursorH = 0;
        uint32_t cursorArgb = 0;

        static void OnState(void* data, int, int state, const char*)
        {
            Impl* self = static_cast<Impl*>(data);
            std::lock_guard<std::mutex> lock(self->mutex);
            if (state >= Pw::kStatePaused)
            {
                self->connected = true;
            }
            self->changed.notify_all();
        }

        static void OnParam(void* data, uint32_t id, const Pw::SpaPod* param)
        {
            Impl* self = static_cast<Impl*>(data);
            Pw::VideoFormat format;
            if (param == nullptr || id != Pw::kParamFormat || !Pw::ParseVideoFormat(param, format))
            {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                self->negotiatedWidth = format.width;
                self->negotiatedHeight = format.height;
                self->pixelFormat = format.format;
            }
            const uint32_t bytes = (format.format == Pw::kVideoRGB || format.format == Pw::kVideoBGR) ? 3 : 4;
            const int32_t stride = static_cast<int32_t>(format.width * bytes);
            Pw::PodBuilder buffers;
            buffers.BeginObject(Pw::kObjectParamBuffers, Pw::kParamBuffers);
            buffers.AddIntChoiceRange(Pw::kBuffersBuffers, 4, 2, 8);
            buffers.AddInt(Pw::kBuffersBlocks, 1);
            buffers.AddInt(Pw::kBuffersSize, stride * static_cast<int32_t>(format.height));
            buffers.AddInt(Pw::kBuffersStride, stride);
            buffers.AddInt(Pw::kBuffersDataType, (1 << Pw::kDataMemPtr) | (1 << Pw::kDataMemFd));
            const Pw::SpaPod* bufferPod = buffers.Finish();
            std::vector<uint8_t> copy(reinterpret_cast<const uint8_t*>(bufferPod),
                                      reinterpret_cast<const uint8_t*>(bufferPod) + 8 + bufferPod->size);
            Pw::PodBuilder meta;
            meta.BeginObject(Pw::kObjectParamMeta, Pw::kParamMeta);
            meta.AddId(Pw::kMetaType, Pw::kMetaCursor);
            meta.AddInt(Pw::kMetaSize, static_cast<int32_t>(sizeof(Pw::SpaMetaCursor) + sizeof(Pw::SpaMetaBitmap) +
                                                           64 * 64 * 4));
            const Pw::SpaPod* params[2] = {reinterpret_cast<const Pw::SpaPod*>(copy.data()), meta.Finish()};
            self->api->streamUpdateParams(self->stream, params, 2);
        }

        static void OnProcess(void* data)
        {
            Impl* self = static_cast<Impl*>(data);
            Pw::PwBuffer* buffer = self->api->streamDequeue(self->stream);
            if (buffer == nullptr)
            {
                return;
            }
            self->Paint(*buffer->buffer);
            self->api->streamQueue(self->stream, buffer);
            if (self->frames != nullptr)
            {
                ++*self->frames;
            }
        }

        void Paint(Pw::SpaBuffer& buffer)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (buffer.nDatas == 0 || buffer.datas[0].data == nullptr || negotiatedWidth == 0)
            {
                return;
            }
            Pw::SpaData& d = buffer.datas[0];
            const uint32_t bytes = (pixelFormat == Pw::kVideoRGB || pixelFormat == Pw::kVideoBGR) ? 3 : 4;
            const uint32_t stride = negotiatedWidth * bytes;
            if (static_cast<size_t>(stride) * negotiatedHeight > d.maxsize)
            {
                return;
            }
            uint8_t* out = static_cast<uint8_t*>(d.data);
            for (uint32_t y = 0; y < negotiatedHeight; ++y)
            {
                for (uint32_t x = 0; x < negotiatedWidth; ++x)
                {
                    uint32_t rgb = (x < width && y < height) ? image[static_cast<size_t>(y) * width + x] : 0;
                    uint8_t r = (rgb >> 16) & 0xFF;
                    uint8_t g = (rgb >> 8) & 0xFF;
                    uint8_t b = rgb & 0xFF;
                    uint8_t* p = out + static_cast<size_t>(y) * stride + x * bytes;
                    switch (pixelFormat)
                    {
                        case Pw::kVideoBGRx:
                        case Pw::kVideoBGRA: p[0] = b; p[1] = g; p[2] = r; p[3] = 0xFF; break;
                        case Pw::kVideoRGBx:
                        case Pw::kVideoRGBA: p[0] = r; p[1] = g; p[2] = b; p[3] = 0xFF; break;
                        case Pw::kVideoxRGB:
                        case Pw::kVideoARGB: p[0] = 0xFF; p[1] = r; p[2] = g; p[3] = b; break;
                        case Pw::kVideoxBGR:
                        case Pw::kVideoABGR: p[0] = 0xFF; p[1] = b; p[2] = g; p[3] = r; break;
                        case Pw::kVideoRGB: p[0] = r; p[1] = g; p[2] = b; break;
                        case Pw::kVideoBGR: p[0] = b; p[1] = g; p[2] = r; break;
                        default: break;
                    }
                }
            }
            d.chunk->offset = 0;
            d.chunk->size = stride * negotiatedHeight;
            d.chunk->stride = static_cast<int32_t>(stride);
            for (uint32_t i = 0; i < buffer.nMetas; ++i)
            {
                if (buffer.metas[i].type == Pw::kMetaCursor && haveCursor && buffer.metas[i].data != nullptr &&
                    buffer.metas[i].size >= sizeof(Pw::SpaMetaCursor) + sizeof(Pw::SpaMetaBitmap) + cursorW * cursorH * 4u)
                {
                    uint8_t* base = static_cast<uint8_t*>(buffer.metas[i].data);
                    auto* cursor = reinterpret_cast<Pw::SpaMetaCursor*>(base);
                    cursor->id = 1;
                    cursor->flags = 0;
                    cursor->position = {cursorX, cursorY};
                    cursor->hotspot = {hotX, hotY};
                    cursor->bitmapOffset = sizeof(Pw::SpaMetaCursor);
                    auto* bitmap = reinterpret_cast<Pw::SpaMetaBitmap*>(base + cursor->bitmapOffset);
                    bitmap->format = Pw::kVideoBGRA;
                    bitmap->width = cursorW;
                    bitmap->height = cursorH;
                    bitmap->stride = cursorW * 4;
                    bitmap->offset = sizeof(Pw::SpaMetaBitmap);
                    uint32_t* pixels = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(bitmap) + bitmap->offset);
                    for (int p = 0; p < cursorW * cursorH; ++p)
                    {
                        pixels[p] = cursorArgb; // BGRA bytes of a little-endian 0xAARRGGBB
                    }
                }
            }
        }

        const Pw::SpaPod* BuildFormat(Pw::PodBuilder& builder)
        {
            builder.BeginObject(Pw::kObjectFormat, Pw::kParamEnumFormat);
            builder.AddId(Pw::kFormatMediaType, Pw::kMediaTypeVideo);
            builder.AddId(Pw::kFormatMediaSubtype, Pw::kMediaSubtypeRaw);
            builder.AddId(Pw::kFormatVideoFormat, pixelFormat);
            builder.AddRectangle(Pw::kFormatVideoSize, width, height);
            builder.AddFraction(Pw::kFormatVideoFramerate, 30, 1);
            return builder.Finish();
        }
    };

    FakeScreen::FakeScreen() = default;

    FakeScreen::~FakeScreen() { Stop(); }

    bool FakeScreen::Start(uint32_t width, uint32_t height, uint32_t pixelFormat, std::string& error)
    {
        m_impl = std::make_unique<Impl>();
        Impl& impl = *m_impl;
        impl.api = Pw::GetApi(error);
        if (impl.api == nullptr)
        {
            return false;
        }
        const Pw::Api& api = *impl.api;
        impl.frames = &m_frames;
        impl.width = width;
        impl.height = height;
        impl.pixelFormat = pixelFormat;
        impl.image.assign(static_cast<size_t>(width) * height, 0);
        impl.loop = api.threadLoopNew("fake-screen", nullptr);
        impl.context = api.contextNew(api.threadLoopGetLoop(impl.loop), nullptr, 0);
        impl.core = api.contextConnect(impl.context, nullptr, 0);
        if (impl.core == nullptr)
        {
            error = "could not connect to PipeWire";
            return false;
        }
        void* props = api.propertiesNew("media.class", "Video/Source", "node.name", "marconi-fake-screen",
                                        "media.role", "Screen", nullptr);
        impl.stream = api.streamNew(impl.core, "marconi-fake-screen", props);
        std::memset(&impl.events, 0, sizeof(impl.events));
        impl.events.version = Pw::kStreamEventsVersion;
        impl.events.stateChanged = &Impl::OnState;
        impl.events.paramChanged = &Impl::OnParam;
        impl.events.process = &Impl::OnProcess;
        std::memset(&impl.hook, 0, sizeof(impl.hook));
        api.streamAddListener(impl.stream, &impl.hook, &impl.events, &impl);
        Pw::PodBuilder builder;
        const Pw::SpaPod* params[1] = {impl.BuildFormat(builder)};
        api.threadLoopLock(impl.loop);
        int result = api.streamConnect(impl.stream, Pw::kDirectionOutput, Pw::kIdAny,
                                       Pw::kFlagDriver | Pw::kFlagMapBuffers, params, 1);
        api.threadLoopUnlock(impl.loop);
        if (result < 0 || api.threadLoopStart(impl.loop) < 0)
        {
            error = "could not start the fake screen";
            return false;
        }
        {
            std::unique_lock<std::mutex> lock(impl.mutex);
            impl.changed.wait_for(lock, std::chrono::seconds(5), [&] { return impl.connected; });
            if (!impl.connected)
            {
                error = "the fake screen never connected";
                return false;
            }
        }
        api.threadLoopLock(impl.loop);
        m_node = api.streamGetNodeId != nullptr ? api.streamGetNodeId(impl.stream) : 0;
        api.threadLoopUnlock(impl.loop);
        Impl* raw = m_impl.get();
        impl.driver = std::thread([raw]() {
            while (!raw->stopDriver.load())
            {
                raw->api->threadLoopLock(raw->loop);
                raw->api->streamTriggerProcess(raw->stream);
                raw->api->threadLoopUnlock(raw->loop);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
        return true;
    }

    void FakeScreen::Stop()
    {
        if (!m_impl)
        {
            return;
        }
        Impl& impl = *m_impl;
        impl.stopDriver.store(true);
        if (impl.driver.joinable())
        {
            impl.driver.join();
        }
        const Pw::Api& api = *impl.api;
        if (impl.loop != nullptr)
        {
            api.threadLoopStop(impl.loop);
        }
        if (impl.stream != nullptr)
        {
            api.streamDestroy(impl.stream);
        }
        if (impl.core != nullptr)
        {
            api.coreDisconnect(impl.core);
        }
        if (impl.context != nullptr)
        {
            api.contextDestroy(impl.context);
        }
        if (impl.loop != nullptr)
        {
            api.threadLoopDestroy(impl.loop);
        }
        m_impl.reset();
    }

    void FakeScreen::Fill(uint32_t rgb)
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        std::fill(m_impl->image.begin(), m_impl->image.end(), rgb & 0xFFFFFF);
    }

    void FakeScreen::FillRect(int x, int y, int width, int height, uint32_t rgb)
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        for (int row = y; row < y + height; ++row)
        {
            for (int column = x; column < x + width; ++column)
            {
                if (row >= 0 && column >= 0 && row < static_cast<int>(m_impl->height) &&
                    column < static_cast<int>(m_impl->width))
                {
                    m_impl->image[static_cast<size_t>(row) * m_impl->width + column] = rgb & 0xFFFFFF;
                }
            }
        }
    }

    void FakeScreen::Resize(uint32_t width, uint32_t height)
    {
        Impl& impl = *m_impl;
        {
            std::lock_guard<std::mutex> lock(impl.mutex);
            impl.width = width;
            impl.height = height;
            impl.image.assign(static_cast<size_t>(width) * height, 0);
        }
        Pw::PodBuilder builder;
        const Pw::SpaPod* params[1] = {impl.BuildFormat(builder)};
        impl.api->threadLoopLock(impl.loop);
        impl.api->streamUpdateParams(impl.stream, params, 1);
        impl.api->threadLoopUnlock(impl.loop);
    }

    void FakeScreen::SetCursor(int x, int y, int hotspotX, int hotspotY, int width, int height, uint32_t argb)
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        m_impl->haveCursor = true;
        m_impl->cursorX = x;
        m_impl->cursorY = y;
        m_impl->hotX = hotspotX;
        m_impl->hotY = hotspotY;
        m_impl->cursorW = width;
        m_impl->cursorH = height;
        m_impl->cursorArgb = argb;
    }
} // namespace MarconiServerTest

#endif
