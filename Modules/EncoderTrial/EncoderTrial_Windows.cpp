// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The encoder trial on Windows: every H.264 encoder Media Foundation offers, fed a moving picture at a steady frame
// rate, with the low-latency settings PERFORMANCE_PLAN.md wants, recording which settings each accepts and how it
// behaves.
//
// NOTE: written in a Linux container and only compile-checked there against the MinGW headers; it has not been built
// with MSVC or run. Run it on each Windows machine whose GPU matters and paste the output into PERFORMANCE_PLAN.md,
// section 6. A setting that "takes" (the encoder returns success) may still be ignored, so the trial also measures
// what comes out: slices per picture, keyframes, sizes and delay.
//
// For each hardware encoder (and Microsoft's software one) it:
//  - unlocks it if it is asynchronous, and hands it the Direct3D 11 device so it can take textures;
//  - sets low-latency mode, constant bitrate, no B-frames, gradual intra refresh, slices limited by size and a
//    long-term reference buffer, recording each answer;
//  - converts a moving BGRA picture to NV12 on the GPU with the Direct3D video processor (timed), or on the CPU for
//    an encoder that can't take textures;
//  - feeds it in real time and times each picture from input to output;
//  - partway through, marks and uses a long-term reference, halves the bitrate and forces a keyframe;
//  - afterwards, checks whether it accepts full-colour (4:4:4) input.

#include "EncoderTrial.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <codecapi.h>
#include <d3d10.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
    // A COM pointer that releases what it holds. (Kept local so the trial needs nothing but the Windows SDK.)
    template <class T>
    class Com
    {
    public:
        Com() = default;
        ~Com() { Reset(); }
        Com(const Com&) = delete;
        Com& operator=(const Com&) = delete;

        T* Get() const { return m_p; }
        T* operator->() const { return m_p; }
        explicit operator bool() const { return m_p != nullptr; }
        T** Put()
        {
            Reset();
            return &m_p;
        }
        void Reset()
        {
            if (m_p != nullptr)
            {
                m_p->Release();
                m_p = nullptr;
            }
        }
        template <class U>
        HRESULT As(Com<U>& out) const
        {
            return m_p->QueryInterface(__uuidof(U), reinterpret_cast<void**>(out.Put()));
        }

    private:
        T* m_p = nullptr;
    };

    using Clock = std::chrono::steady_clock;

    double MsSince(Clock::time_point start, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    // Waits until `due` precisely: Windows' sleeps are coarse (about 15 ms unless the timer resolution is raised),
    // which would feed the encoder unevenly, so the last stretch spins. A measuring tool can spare the core.
    void WaitUntil(Clock::time_point due)
    {
        while (Clock::now() < due)
        {
            if (due - Clock::now() > std::chrono::milliseconds(20))
            {
                Sleep(1);
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    double Median(std::vector<double> values)
    {
        if (values.empty())
        {
            return -1;
        }
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }

    double P99(std::vector<double> values)
    {
        if (values.empty())
        {
            return -1;
        }
        std::sort(values.begin(), values.end());
        size_t index = static_cast<size_t>(static_cast<double>(values.size()) * 0.99);
        return values[std::min(index, values.size() - 1)];
    }

    std::string Narrow(const wchar_t* text)
    {
        if (text == nullptr)
        {
            return std::string();
        }
        int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(std::max(0, length - 1)), '\0');
        if (length > 1)
        {
            WideCharToMultiByte(CP_UTF8, 0, text, -1, &result[0], length, nullptr, nullptr);
        }
        return result;
    }

    std::string Hr(HRESULT hr)
    {
        if (hr == S_OK)
        {
            return "ok";
        }
        char text[32];
        std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(hr));
        return text;
    }

    struct Options
    {
        UINT width = 1920;
        UINT height = 1080;
        UINT fps = 60;
        UINT kbps = 20000;
        int frames = 300;
    };

    // One setting tried on an encoder: whether it says it supports it, and what setting it returned.
    struct SettingResult
    {
        std::string name;
        bool supported = false;
        HRESULT result = E_NOTIMPL;
    };

    struct Report
    {
        std::string name;
        UINT32 vendor = 0;
        bool hardware = false;
        bool async = false;
        bool textureInput = false;
        std::vector<SettingResult> settings;
        std::vector<std::pair<std::string, HRESULT>> actions; // things done partway through
        int framesIn = 0;
        int framesOut = 0;
        int delayFrames = -1; // pictures fed in before the first came out, minus one (0 = one in, one out)
        std::vector<double> latencyMs;
        std::vector<double> convertMs;
        int idrFrames = 0; // pictures that were keyframes (IDR)
        std::vector<double> keyBytes;
        std::vector<double> otherBytes;
        std::vector<double> slicesPerFrame;
        size_t largestSliceBytes = 0;
        double kbpsBefore = 0; // the bitrate the output actually ran at, before and after halving it
        double kbpsAfter = 0;
        bool keyframeAfterBitrateChange = false;
        HRESULT fullColour = E_NOTIMPL; // 4:4:4 input accepted?
        std::string error;
    };

    // ---- The picture ---------------------------------------------------------------------------------------

    // A gradient with a moving bar and a moving box: enough change for the encoder to work at, the way a game or a
    // dragged window would make it. BGRA, row by row.
    void DrawPicture(std::vector<uint8_t>& bgra, UINT width, UINT height, int frame)
    {
        bgra.resize(static_cast<size_t>(width) * height * 4);
        const UINT bar = static_cast<UINT>(frame * 7) % width;
        const UINT boxX = static_cast<UINT>(frame * 5) % (width > 200 ? width - 200 : 1);
        const UINT boxY = static_cast<UINT>(frame * 3) % (height > 200 ? height - 200 : 1);
        for (UINT y = 0; y < height; ++y)
        {
            uint8_t* row = bgra.data() + static_cast<size_t>(y) * width * 4;
            for (UINT x = 0; x < width; ++x)
            {
                uint8_t r = static_cast<uint8_t>(x * 255 / std::max(1u, width - 1));
                uint8_t g = static_cast<uint8_t>(y * 255 / std::max(1u, height - 1));
                uint8_t b = static_cast<uint8_t>((x ^ y) & 0x3F);
                if (x >= bar && x < bar + 8)
                {
                    r = g = b = 255;
                }
                if (x >= boxX && x < boxX + 200 && y >= boxY && y < boxY + 200)
                {
                    r = static_cast<uint8_t>(frame * 3);
                    g = 40;
                    b = static_cast<uint8_t>(255 - frame);
                }
                row[x * 4 + 0] = b;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = r;
                row[x * 4 + 3] = 255;
            }
        }
    }

    // BGRA to NV12 (BT.709, studio range) on the CPU, for encoders that can't take textures.
    void BgraToNv12(const std::vector<uint8_t>& bgra, UINT width, UINT height, uint8_t* out)
    {
        uint8_t* yPlane = out;
        uint8_t* uvPlane = out + static_cast<size_t>(width) * height;
        for (UINT y = 0; y < height; ++y)
        {
            for (UINT x = 0; x < width; ++x)
            {
                const uint8_t* p = bgra.data() + (static_cast<size_t>(y) * width + x) * 4;
                double r = p[2], g = p[1], b = p[0];
                yPlane[static_cast<size_t>(y) * width + x] =
                    static_cast<uint8_t>(16 + (0.1826 * r + 0.6142 * g + 0.0620 * b));
                if ((x % 2) == 0 && (y % 2) == 0)
                {
                    uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * width + x;
                    uv[0] = static_cast<uint8_t>(128 + (-0.1006 * r - 0.3386 * g + 0.4392 * b));
                    uv[1] = static_cast<uint8_t>(128 + (0.4392 * r - 0.3989 * g - 0.0403 * b));
                }
            }
        }
    }

    // ---- Direct3D: the device and the GPU colour conversion -----------------------------------------------

    struct Gpu
    {
        Com<ID3D11Device> device;
        Com<ID3D11DeviceContext> context;
        Com<IMFDXGIDeviceManager> manager;
        UINT resetToken = 0;
        std::string adapterName;

        Com<ID3D11VideoDevice> videoDevice;
        Com<ID3D11VideoContext> videoContext;
        Com<ID3D11VideoProcessorEnumerator> processorEnum;
        Com<ID3D11VideoProcessor> processor;
        Com<ID3D11Texture2D> bgra;
        Com<ID3D11VideoProcessorInputView> inputView;
        static const int kPool = 6; // NV12 textures in rotation, so the encoder can hold a few
        Com<ID3D11Texture2D> nv12[kPool];
        Com<ID3D11VideoProcessorOutputView> outputView[kPool];
        Com<ID3D11Query> done;
    };

    bool CreateGpu(Gpu& gpu, const Options& options, std::string& error)
    {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                       D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 2,
                                       D3D11_SDK_VERSION, gpu.device.Put(), nullptr, gpu.context.Put());
        if (FAILED(hr))
        {
            error = "Direct3D 11 with video support isn't available: " + Hr(hr);
            return false;
        }
        // The encoder uses the device from its own threads.
        Com<ID3D10Multithread> multithread;
        if (SUCCEEDED(gpu.device.As(multithread)))
        {
            multithread->SetMultithreadProtected(TRUE);
        }
        Com<IDXGIDevice> dxgiDevice;
        Com<IDXGIAdapter> adapter;
        if (SUCCEEDED(gpu.device.As(dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(adapter.Put())))
        {
            DXGI_ADAPTER_DESC desc = {};
            adapter->GetDesc(&desc);
            gpu.adapterName = Narrow(desc.Description);
        }
        hr = MFCreateDXGIDeviceManager(&gpu.resetToken, gpu.manager.Put());
        if (SUCCEEDED(hr))
        {
            hr = gpu.manager->ResetDevice(gpu.device.Get(), gpu.resetToken);
        }
        if (FAILED(hr))
        {
            error = "Couldn't make a Media Foundation device manager: " + Hr(hr);
            return false;
        }

        // The video processor: BGRA in, NV12 out, same size.
        if (FAILED(gpu.device.As(gpu.videoDevice)) || FAILED(gpu.context.As(gpu.videoContext)))
        {
            error = "The device has no video processor";
            return false;
        }
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content = {};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate = {options.fps, 1};
        content.InputWidth = options.width;
        content.InputHeight = options.height;
        content.OutputFrameRate = {options.fps, 1};
        content.OutputWidth = options.width;
        content.OutputHeight = options.height;
        content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
        hr = gpu.videoDevice->CreateVideoProcessorEnumerator(&content, gpu.processorEnum.Put());
        if (SUCCEEDED(hr))
        {
            hr = gpu.videoDevice->CreateVideoProcessor(gpu.processorEnum.Get(), 0, gpu.processor.Put());
        }
        if (FAILED(hr))
        {
            error = "Couldn't make a video processor: " + Hr(hr);
            return false;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = options.width;
        desc.Height = options.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        hr = gpu.device->CreateTexture2D(&desc, nullptr, gpu.bgra.Put());
        if (FAILED(hr))
        {
            error = "Couldn't make the BGRA texture: " + Hr(hr);
            return false;
        }
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc = {};
        inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        hr = gpu.videoDevice->CreateVideoProcessorInputView(gpu.bgra.Get(), gpu.processorEnum.Get(), &inputDesc,
                                                            gpu.inputView.Put());
        if (FAILED(hr))
        {
            error = "Couldn't make the video processor's input view: " + Hr(hr);
            return false;
        }
        desc.Format = DXGI_FORMAT_NV12;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        for (int i = 0; i < Gpu::kPool; ++i)
        {
            hr = gpu.device->CreateTexture2D(&desc, nullptr, gpu.nv12[i].Put());
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc = {};
            outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            if (SUCCEEDED(hr))
            {
                hr = gpu.videoDevice->CreateVideoProcessorOutputView(gpu.nv12[i].Get(), gpu.processorEnum.Get(),
                                                                     &outputDesc, gpu.outputView[i].Put());
            }
            if (FAILED(hr))
            {
                error = "Couldn't make an NV12 texture for the encoder: " + Hr(hr);
                return false;
            }
        }
        // Full-range RGB in, BT.709 studio-range YCbCr out.
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inSpace = {};
        inSpace.RGB_Range = 0; // full
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outSpace = {};
        outSpace.YCbCr_Matrix = 1; // BT.709
        outSpace.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        gpu.videoContext->VideoProcessorSetStreamColorSpace(gpu.processor.Get(), 0, &inSpace);
        gpu.videoContext->VideoProcessorSetOutputColorSpace(gpu.processor.Get(), &outSpace);

        D3D11_QUERY_DESC query = {D3D11_QUERY_EVENT, 0};
        gpu.device->CreateQuery(&query, gpu.done.Put());
        return true;
    }

    // Uploads the picture and converts it into NV12 texture `slot`, waiting for the GPU so the time is the
    // conversion's. Returns the milliseconds the conversion took (upload excluded), or -1 on failure.
    double ConvertOnGpu(Gpu& gpu, const std::vector<uint8_t>& bgra, const Options& options, int slot)
    {
        gpu.context->UpdateSubresource(gpu.bgra.Get(), 0, nullptr, bgra.data(), options.width * 4, 0);
        gpu.context->End(gpu.done.Get());
        while (gpu.context->GetData(gpu.done.Get(), nullptr, 0, 0) == S_FALSE)
        {
            // wait for the upload, so it isn't counted
        }
        Clock::time_point start = Clock::now();
        D3D11_VIDEO_PROCESSOR_STREAM stream = {};
        stream.Enable = TRUE;
        stream.pInputSurface = gpu.inputView.Get();
        HRESULT hr = gpu.videoContext->VideoProcessorBlt(gpu.processor.Get(), gpu.outputView[slot].Get(), 0, 1, &stream);
        if (FAILED(hr))
        {
            return -1;
        }
        gpu.context->End(gpu.done.Get());
        while (gpu.context->GetData(gpu.done.Get(), nullptr, 0, 0) == S_FALSE)
        {
        }
        return MsSince(start, Clock::now());
    }

    // ---- Media types and settings ------------------------------------------------------------------------

    HRESULT SetOutputType(IMFTransform* encoder, const Options& options, UINT32 profile)
    {
        Com<IMFMediaType> type;
        HRESULT hr = MFCreateMediaType(type.Put());
        if (FAILED(hr))
        {
            return hr;
        }
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        type->SetUINT32(MF_MT_AVG_BITRATE, options.kbps * 1000);
        MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, options.width, options.height);
        MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, options.fps, 1);
        MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        type->SetUINT32(MF_MT_MPEG2_PROFILE, profile);
        return encoder->SetOutputType(0, type.Get(), 0);
    }

    HRESULT SetInputType(IMFTransform* encoder, const Options& options, const GUID& subtype)
    {
        Com<IMFMediaType> type;
        HRESULT hr = MFCreateMediaType(type.Put());
        if (FAILED(hr))
        {
            return hr;
        }
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID(MF_MT_SUBTYPE, subtype);
        MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, options.width, options.height);
        MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, options.fps, 1);
        MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        return encoder->SetInputType(0, type.Get(), 0);
    }

    void TrySetting(ICodecAPI* codec, Report& report, const char* name, const GUID& property, VARTYPE type,
                    ULONG value)
    {
        SettingResult result;
        result.name = name;
        if (codec == nullptr)
        {
            report.settings.push_back(result);
            return;
        }
        result.supported = codec->IsSupported(&property) == S_OK;
        VARIANT variant;
        VariantInit(&variant);
        variant.vt = type;
        if (type == VT_BOOL)
        {
            variant.boolVal = value != 0 ? VARIANT_TRUE : VARIANT_FALSE;
        }
        else
        {
            variant.ulVal = value;
        }
        result.result = codec->SetValue(&property, &variant);
        report.settings.push_back(result);
    }

    HRESULT SetValue(ICodecAPI* codec, const GUID& property, ULONG value)
    {
        if (codec == nullptr)
        {
            return E_NOINTERFACE;
        }
        VARIANT variant;
        VariantInit(&variant);
        variant.vt = VT_UI4;
        variant.ulVal = value;
        return codec->SetValue(&property, &variant);
    }

    bool SettingTook(const Report& report, const char* name)
    {
        for (const SettingResult& setting : report.settings)
        {
            if (setting.name == name)
            {
                return SUCCEEDED(setting.result);
            }
        }
        return false;
    }

    // ---- Reading what comes out ----------------------------------------------------------------------------

    // Counts the H.264 slices in an Annex B stream (start codes 00 00 01 or 00 00 00 01), their largest size, and
    // whether there is an IDR slice (a keyframe).
    void ParseNals(const uint8_t* data, size_t size, int& slices, size_t& largestSlice, bool& idr)
    {
        slices = 0;
        largestSlice = 0;
        idr = false;
        size_t start = SIZE_MAX;
        int startType = 0;
        auto finish = [&](size_t end) {
            if (start != SIZE_MAX && (startType == 1 || startType == 5))
            {
                ++slices;
                largestSlice = std::max(largestSlice, end - start);
                idr = idr || startType == 5;
            }
        };
        for (size_t i = 0; i + 3 < size; ++i)
        {
            if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
            {
                size_t codeStart = (i > 0 && data[i - 1] == 0) ? i - 1 : i;
                finish(codeStart);
                start = i + 3;
                startType = data[i + 3] & 0x1F;
                i += 2;
            }
        }
        finish(size);
    }

    // ---- Running one encoder -------------------------------------------------------------------------------

    class Trial
    {
    public:
        Trial(IMFTransform* encoder, Report& report, Gpu* gpu, const Options& options)
            : m_encoder(encoder), m_report(report), m_gpu(gpu), m_options(options)
        {
        }

        bool Run()
        {
            const LONGLONG duration = 10000000LL / m_options.fps; // 100 ns units
            m_inputTimes.assign(static_cast<size_t>(m_options.frames), Clock::time_point());
            m_encoder->QueryInterface(IID_ICodecAPI, reinterpret_cast<void**>(m_codec.Put()));

            HRESULT hr = m_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
            if (SUCCEEDED(hr))
            {
                hr = m_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
            }
            if (FAILED(hr))
            {
                m_report.error = "wouldn't start streaming: " + Hr(hr);
                return false;
            }

            Com<IMFMediaEventGenerator> events;
            if (m_report.async)
            {
                m_encoder->QueryInterface(__uuidof(IMFMediaEventGenerator), reinterpret_cast<void**>(events.Put()));
                if (!events)
                {
                    m_report.error = "asynchronous but has no event generator";
                    return false;
                }
            }

            m_start = Clock::now();
            int next = 0;
            int wanted = 0; // pictures an asynchronous encoder has asked for and not yet been given
            bool draining = false;
            while (m_report.framesOut < m_report.framesIn || next < m_options.frames)
            {
                if (Clock::now() - m_start > std::chrono::seconds(30 + m_options.frames / static_cast<int>(m_options.fps)))
                {
                    m_report.error = "stopped answering";
                    return false;
                }
                if (m_report.async)
                {
                    // Poll rather than block, so finished pictures are collected the moment they're ready even
                    // while waiting for the next picture's time; blocking would add up to a picture to the delay.
                    if (wanted > 0 && next < m_options.frames && Clock::now() >= Due(next))
                    {
                        if (!Feed(next, duration))
                        {
                            return false;
                        }
                        ++next;
                        --wanted;
                        continue;
                    }
                    if (wanted > 0 && next >= m_options.frames && !draining)
                    {
                        m_encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
                        draining = true;
                    }
                    Com<IMFMediaEvent> event;
                    hr = events->GetEvent(MF_EVENT_FLAG_NO_WAIT, event.Put());
                    if (hr == MF_E_NO_EVENTS_AVAILABLE)
                    {
                        std::this_thread::yield();
                        continue;
                    }
                    if (FAILED(hr))
                    {
                        m_report.error = "event queue failed: " + Hr(hr);
                        return false;
                    }
                    MediaEventType type = MEUnknown;
                    event->GetType(&type);
                    if (type == METransformNeedInput)
                    {
                        ++wanted;
                    }
                    else if (type == METransformHaveOutput)
                    {
                        if (!Collect(duration))
                        {
                            return false;
                        }
                    }
                    else if (type == METransformDrainComplete)
                    {
                        break;
                    }
                }
                else
                {
                    if (next < m_options.frames)
                    {
                        WaitUntil(Due(next));
                        if (!Feed(next, duration))
                        {
                            return false;
                        }
                        ++next;
                    }
                    else if (!draining)
                    {
                        m_encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
                        draining = true;
                    }
                    // Take everything that's ready.
                    bool more = true;
                    while (more)
                    {
                        more = Collect(duration);
                        if (!m_report.error.empty())
                        {
                            return false;
                        }
                    }
                    if (draining)
                    {
                        break;
                    }
                }
            }
            m_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
            m_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
            Summarise();
            return true;
        }

    private:
        // When picture `index` is due, at the chosen frame rate.
        Clock::time_point Due(int index) const
        {
            return m_start + std::chrono::microseconds(1000000LL * index / m_options.fps);
        }

        // Hands picture `index` to the encoder (the caller waits for its time).
        bool Feed(int index, LONGLONG duration)
        {
            Actions(index);

            DrawPicture(m_bgra, m_options.width, m_options.height, index);
            Com<IMFMediaBuffer> buffer;
            HRESULT hr = S_OK;
            if (m_report.textureInput)
            {
                const int slot = index % Gpu::kPool;
                double ms = ConvertOnGpu(*m_gpu, m_bgra, m_options, slot);
                if (ms < 0)
                {
                    m_report.error = "the GPU colour conversion failed";
                    return false;
                }
                m_report.convertMs.push_back(ms);
                hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), m_gpu->nv12[slot].Get(), 0, FALSE,
                                               buffer.Put());
                if (SUCCEEDED(hr))
                {
                    // A DXGI buffer's length must be set for some encoders to take it.
                    Com<IMF2DBuffer> twoD;
                    DWORD length = 0;
                    if (SUCCEEDED(buffer.As(twoD)) && SUCCEEDED(twoD->GetContiguousLength(&length)))
                    {
                        buffer->SetCurrentLength(length);
                    }
                }
            }
            else
            {
                const DWORD size = m_options.width * m_options.height * 3 / 2;
                hr = MFCreateMemoryBuffer(size, buffer.Put());
                if (SUCCEEDED(hr))
                {
                    BYTE* data = nullptr;
                    Clock::time_point start = Clock::now();
                    buffer->Lock(&data, nullptr, nullptr);
                    BgraToNv12(m_bgra, m_options.width, m_options.height, data);
                    buffer->Unlock();
                    buffer->SetCurrentLength(size);
                    m_report.convertMs.push_back(MsSince(start, Clock::now()));
                }
            }
            Com<IMFSample> sample;
            if (SUCCEEDED(hr))
            {
                hr = MFCreateSample(sample.Put());
            }
            if (SUCCEEDED(hr))
            {
                sample->AddBuffer(buffer.Get());
                sample->SetSampleTime(duration * index);
                sample->SetSampleDuration(duration);
                m_inputTimes[static_cast<size_t>(index)] = Clock::now();
                hr = m_encoder->ProcessInput(0, sample.Get(), 0);
                if (hr == MF_E_NOTACCEPTING && !m_report.async)
                {
                    // A synchronous encoder full of work: take its output, then try again.
                    while (Collect(duration))
                    {
                    }
                    m_inputTimes[static_cast<size_t>(index)] = Clock::now();
                    hr = m_encoder->ProcessInput(0, sample.Get(), 0);
                }
            }
            if (FAILED(hr))
            {
                m_report.error = "wouldn't take picture " + std::to_string(index) + ": " + Hr(hr);
                return false;
            }
            ++m_report.framesIn;
            return true;
        }

        // The things done partway through: long-term references, a bitrate change and a forced keyframe.
        void Actions(int index)
        {
            const int frames = m_options.frames;
            if (index == frames / 3 && SettingTook(m_report, "long-term reference buffer"))
            {
                m_report.actions.emplace_back("mark long-term reference",
                                              SetValue(m_codec.Get(), CODECAPI_AVEncVideoMarkLTRFrame, 0));
            }
            if (index == frames / 2)
            {
                m_bitrateChangeFrame = index;
                m_report.actions.emplace_back(
                    "halve the bitrate", SetValue(m_codec.Get(), CODECAPI_AVEncCommonMeanBitRate, m_options.kbps * 500));
            }
            if (index == 2 * frames / 3 && SettingTook(m_report, "long-term reference buffer"))
            {
                m_report.actions.emplace_back("use long-term reference",
                                              SetValue(m_codec.Get(), CODECAPI_AVEncVideoUseLTRFrame, 1));
            }
            if (index == 5 * frames / 6)
            {
                m_report.actions.emplace_back("force a keyframe",
                                              SetValue(m_codec.Get(), CODECAPI_AVEncVideoForceKeyFrame, 1));
            }
        }

        // Takes one output picture if there is one. False when the encoder needs more input (or failed).
        bool Collect(LONGLONG duration)
        {
            MFT_OUTPUT_STREAM_INFO info = {};
            HRESULT hr = m_encoder->GetOutputStreamInfo(0, &info);
            if (FAILED(hr))
            {
                m_report.error = "no output stream information: " + Hr(hr);
                return false;
            }
            const bool provides =
                (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            Com<IMFSample> ours;
            if (!provides)
            {
                Com<IMFMediaBuffer> buffer;
                DWORD size = std::max<DWORD>(info.cbSize, m_options.width * m_options.height * 2);
                if (FAILED(MFCreateMemoryBuffer(size, buffer.Put())) || FAILED(MFCreateSample(ours.Put())))
                {
                    m_report.error = "couldn't make an output sample";
                    return false;
                }
                ours->AddBuffer(buffer.Get());
            }
            MFT_OUTPUT_DATA_BUFFER output = {};
            output.dwStreamID = 0;
            output.pSample = ours.Get();
            DWORD status = 0;
            hr = m_encoder->ProcessOutput(0, 1, &output, &status);
            const Clock::time_point now = Clock::now();
            if (output.pEvents != nullptr)
            {
                output.pEvents->Release();
            }
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
            {
                return false;
            }
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
            {
                // The encoder wants its output type set again (some do once they start).
                Com<IMFMediaType> type;
                if (SUCCEEDED(m_encoder->GetOutputAvailableType(0, 0, type.Put())))
                {
                    m_encoder->SetOutputType(0, type.Get(), 0);
                }
                return true;
            }
            if (FAILED(hr))
            {
                m_report.error = "output failed: " + Hr(hr);
                return false;
            }
            IMFSample* sample = output.pSample;
            if (sample == nullptr)
            {
                return true;
            }
            LONGLONG time = 0;
            sample->GetSampleTime(&time);
            const int index = static_cast<int>((time + duration / 2) / duration);
            if (m_report.delayFrames < 0)
            {
                m_report.delayFrames = m_report.framesIn - 1;
            }
            if (index >= 0 && index < m_options.frames && m_inputTimes[static_cast<size_t>(index)] != Clock::time_point())
            {
                m_report.latencyMs.push_back(MsSince(m_inputTimes[static_cast<size_t>(index)], now));
            }
            Com<IMFMediaBuffer> buffer;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(buffer.Put())))
            {
                BYTE* data = nullptr;
                DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)))
                {
                    int slices = 0;
                    size_t largest = 0;
                    bool idr = false;
                    ParseNals(data, length, slices, largest, idr);
                    buffer->Unlock();
                    m_report.slicesPerFrame.push_back(slices);
                    m_report.largestSliceBytes = std::max(m_report.largestSliceBytes, largest);
                    if (idr)
                    {
                        ++m_report.idrFrames;
                        m_report.keyBytes.push_back(length);
                    }
                    else
                    {
                        m_report.otherBytes.push_back(length);
                    }
                    if (index < m_bitrateChangeFrame || m_bitrateChangeFrame < 0)
                    {
                        m_bytesBefore += length;
                        ++m_framesBefore;
                    }
                    else
                    {
                        m_bytesAfter += length;
                        ++m_framesAfter;
                        if (idr && index <= m_bitrateChangeFrame + 3)
                        {
                            m_report.keyframeAfterBitrateChange = true;
                        }
                    }
                }
            }
            ++m_report.framesOut;
            if (output.pSample != ours.Get())
            {
                output.pSample->Release(); // the encoder's own sample
            }
            return true;
        }

        void Summarise()
        {
            if (m_framesBefore > 0)
            {
                m_report.kbpsBefore = m_bytesBefore * 8.0 / 1000.0 / (static_cast<double>(m_framesBefore) / m_options.fps);
            }
            if (m_framesAfter > 0)
            {
                m_report.kbpsAfter = m_bytesAfter * 8.0 / 1000.0 / (static_cast<double>(m_framesAfter) / m_options.fps);
            }
        }

        IMFTransform* m_encoder;
        Report& m_report;
        Gpu* m_gpu;
        const Options& m_options;
        Com<ICodecAPI> m_codec;
        std::vector<Clock::time_point> m_inputTimes;
        std::vector<uint8_t> m_bgra;
        Clock::time_point m_start;
        int m_bitrateChangeFrame = -1;
        double m_bytesBefore = 0;
        double m_bytesAfter = 0;
        int m_framesBefore = 0;
        int m_framesAfter = 0;
    };

    // Sets an encoder up (unlocking, the device, the settings, the media types) and runs the trial on it.
    void TryEncoder(IMFActivate* activate, bool hardware, Gpu* gpu, const Options& options, Report& report)
    {
        WCHAR* name = nullptr;
        UINT32 length = 0;
        if (SUCCEEDED(activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &length)))
        {
            report.name = Narrow(name);
            CoTaskMemFree(name);
        }
        WCHAR* vendor = nullptr;
        if (SUCCEEDED(activate->GetAllocatedString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, &vendor, &length)))
        {
            report.name += " [" + Narrow(vendor) + "]";
            CoTaskMemFree(vendor);
        }
        report.hardware = hardware;

        Com<IMFTransform> encoder;
        HRESULT hr = activate->ActivateObject(__uuidof(IMFTransform), reinterpret_cast<void**>(encoder.Put()));
        if (FAILED(hr))
        {
            report.error = "couldn't be started: " + Hr(hr);
            return;
        }
        Com<IMFAttributes> attributes;
        if (SUCCEEDED(encoder->GetAttributes(attributes.Put())) && attributes)
        {
            report.async = MFGetAttributeUINT32(attributes.Get(), MF_TRANSFORM_ASYNC, FALSE) != FALSE;
            if (report.async)
            {
                attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
            }
            const bool d3d11Aware = MFGetAttributeUINT32(attributes.Get(), MF_SA_D3D11_AWARE, FALSE) != FALSE;
            if (d3d11Aware && gpu != nullptr)
            {
                hr = encoder->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                             reinterpret_cast<ULONG_PTR>(gpu->manager.Get()));
                report.textureInput = SUCCEEDED(hr);
            }
        }

        Com<ICodecAPI> codec;
        encoder->QueryInterface(IID_ICodecAPI, reinterpret_cast<void**>(codec.Put()));
        // Settings, in the order encoders generally want them: before the media types.
        TrySetting(codec.Get(), report, "low latency", CODECAPI_AVLowLatencyMode, VT_BOOL, 1);
        TrySetting(codec.Get(), report, "constant bitrate", CODECAPI_AVEncCommonRateControlMode, VT_UI4,
                   eAVEncCommonRateControlMode_CBR);
        TrySetting(codec.Get(), report, "bitrate", CODECAPI_AVEncCommonMeanBitRate, VT_UI4, options.kbps * 1000);
        TrySetting(codec.Get(), report, "no B-frames", CODECAPI_AVEncMPVDefaultBPictureCount, VT_UI4, 0);
        // The value's meaning (here: refresh spread over one second of pictures) is to be checked against
        // Microsoft's documentation; the keyframe count below shows whether keyframes stopped.
        TrySetting(codec.Get(), report, "gradual intra refresh", CODECAPI_AVEncVideoGradualIntraRefresh, VT_UI4,
                   options.fps);
        // Mode 1 is believed to mean "size in bits" (verify); the slices-per-picture count shows the effect.
        TrySetting(codec.Get(), report, "slice size mode", CODECAPI_AVEncSliceControlMode, VT_UI4, 1);
        TrySetting(codec.Get(), report, "slice size (bits)", CODECAPI_AVEncSliceControlSize, VT_UI4, 1200 * 8);
        // One long-term reference frame, trust mode 1 (the low 16 bits are the count, the high 16 the mode).
        TrySetting(codec.Get(), report, "long-term reference buffer", CODECAPI_AVEncVideoLTRBufferControl, VT_UI4,
                   0x00010001);

        hr = SetOutputType(encoder.Get(), options, eAVEncH264VProfile_High);
        if (FAILED(hr))
        {
            hr = SetOutputType(encoder.Get(), options, eAVEncH264VProfile_Main);
        }
        if (FAILED(hr))
        {
            report.error = "refused the H.264 output type: " + Hr(hr);
            activate->ShutdownObject();
            return;
        }
        hr = SetInputType(encoder.Get(), options, MFVideoFormat_NV12);
        if (FAILED(hr))
        {
            report.error = "refused NV12 input: " + Hr(hr);
            activate->ShutdownObject();
            return;
        }

        Trial trial(encoder.Get(), report, report.textureInput ? gpu : nullptr, options);
        trial.Run();
        encoder.Reset();
        activate->ShutdownObject();

        // Full colour: a fresh instance asked for the 4:4:4 profile with AYUV (4:4:4) input.
        Com<IMFTransform> probe;
        if (SUCCEEDED(activate->ActivateObject(__uuidof(IMFTransform), reinterpret_cast<void**>(probe.Put()))))
        {
            Com<IMFAttributes> probeAttributes;
            if (report.async && SUCCEEDED(probe->GetAttributes(probeAttributes.Put())) && probeAttributes)
            {
                probeAttributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
            }
            report.fullColour = SetOutputType(probe.Get(), options, eAVEncH264VProfile_444);
            if (SUCCEEDED(report.fullColour))
            {
                report.fullColour = SetInputType(probe.Get(), options, MFVideoFormat_AYUV);
            }
            probe.Reset();
            activate->ShutdownObject();
        }
    }

    void Print(const Report& r, const Options& options)
    {
        std::printf("\n%s (%s, %s)\n", r.name.c_str(), r.hardware ? "hardware" : "software",
                    r.async ? "asynchronous" : "synchronous");
        std::printf("  takes GPU textures: %s\n", r.textureInput ? "yes" : "no");
        for (const SettingResult& s : r.settings)
        {
            std::printf("  setting %-28s %-14s %s\n", s.name.c_str(), s.supported ? "supported" : "not listed",
                        Hr(s.result).c_str());
        }
        if (!r.error.empty())
        {
            std::printf("  STOPPED: %s\n", r.error.c_str());
        }
        std::printf("  pictures in %d, out %d; pictures held before the first came out: %d\n", r.framesIn, r.framesOut,
                    r.delayFrames);
        std::printf("  colour conversion %.2f ms (median, %s)\n", Median(r.convertMs),
                    r.textureInput ? "GPU video processor" : "CPU");
        std::printf("  input to output   %.2f ms (median), %.2f ms (99th percentile)\n", Median(r.latencyMs),
                    P99(r.latencyMs));
        std::printf("  keyframes %d (the first, and one forced at picture %d, expected); keyframe %.0f bytes, others "
                    "%.0f bytes (medians)\n",
                    r.idrFrames, 5 * options.frames / 6, Median(r.keyBytes), Median(r.otherBytes));
        std::printf("  slices per picture %.0f (median), largest slice %zu bytes\n", Median(r.slicesPerFrame),
                    r.largestSliceBytes);
        std::printf("  bitrate %.0f kbit/s asked, %.0f kbit/s before halving, %.0f after; keyframe right after the "
                    "change: %s\n",
                    static_cast<double>(options.kbps), r.kbpsBefore, r.kbpsAfter,
                    r.keyframeAfterBitrateChange ? "yes (it restarted)" : "no");
        for (const auto& action : r.actions)
        {
            std::printf("  %-28s %s\n", action.first.c_str(), Hr(action.second).c_str());
        }
        std::printf("  full colour (4:4:4) input: %s\n",
                    SUCCEEDED(r.fullColour) ? "accepted" : ("refused, " + Hr(r.fullColour)).c_str());
        // One line for PERFORMANCE_PLAN.md section 6's encoder table.
        std::printf("  table row: | %s | %s | %.1f / %.1f | %s | %s | %.0f slices, largest %zu B | %s, %s | %s |\n",
                    r.name.c_str(), r.textureInput ? "yes" : "no", Median(r.latencyMs), P99(r.latencyMs),
                    SettingTook(r, "gradual intra refresh") ? "takes" : "refuses",
                    SettingTook(r, "long-term reference buffer") ? "takes" : "refuses", Median(r.slicesPerFrame),
                    r.largestSliceBytes, r.kbpsAfter > 0 ? "changes" : "-",
                    r.keyframeAfterBitrateChange ? "restarts" : "no restart",
                    SUCCEEDED(r.fullColour) ? "accepted" : "refused");
    }

    bool ParseOptions(int argc, char** argv, Options& options)
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            auto value = [&](UINT& out) {
                if (i + 1 >= argc)
                {
                    return false;
                }
                out = static_cast<UINT>(std::atoi(argv[++i]));
                return out > 0;
            };
            UINT number = 0;
            if (arg == "--size" && i + 1 < argc)
            {
                if (std::sscanf(argv[++i], "%ux%u", &options.width, &options.height) != 2 || options.width < 64 ||
                    options.height < 64 || (options.width % 2) != 0 || (options.height % 2) != 0)
                {
                    std::printf("--size needs an even WIDTHxHEIGHT, at least 64x64\n");
                    return false;
                }
            }
            else if (arg == "--fps" && value(options.fps))
            {
            }
            else if (arg == "--kbps" && value(options.kbps))
            {
            }
            else if (arg == "--frames" && value(number))
            {
                options.frames = static_cast<int>(std::max(30u, number));
            }
            else
            {
                std::printf("Usage: EncoderTrial [--size 1920x1080] [--fps 60] [--kbps 20000] [--frames 300]\n");
                return false;
            }
        }
        return true;
    }
} // namespace

int RunEncoderTrial(int argc, char** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        return 2;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) || FAILED(MFStartup(MF_VERSION)))
    {
        std::printf("Media Foundation isn't available.\n");
        return 1;
    }
    std::printf("Encoder trial: %ux%u at %u pictures a second, %u kbit/s, %d pictures per encoder\n", options.width,
                options.height, options.fps, options.kbps, options.frames);

    Gpu gpu;
    std::string gpuError;
    const bool haveGpu = CreateGpu(gpu, options, gpuError);
    if (haveGpu)
    {
        std::printf("Graphics: %s\n", gpu.adapterName.c_str());
    }
    else
    {
        std::printf("No GPU path (%s); encoders get pictures from memory.\n", gpuError.c_str());
    }

    MFT_REGISTER_TYPE_INFO input = {MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO output = {MFMediaType_Video, MFVideoFormat_H264};
    const struct
    {
        UINT32 flags;
        bool hardware;
    } kinds[] = {{MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER, true},
                 {MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, false}};
    int tried = 0;
    for (const auto& kind : kinds)
    {
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, kind.flags, &input, &output, &activates, &count)))
        {
            continue;
        }
        for (UINT32 i = 0; i < count; ++i)
        {
            Report report;
            TryEncoder(activates[i], kind.hardware, haveGpu ? &gpu : nullptr, options, report);
            Print(report, options);
            activates[i]->Release();
            ++tried;
        }
        CoTaskMemFree(activates);
    }
    if (tried == 0)
    {
        std::printf("No H.264 encoders found.\n");
    }
    MFShutdown();
    CoUninitialize();
    return tried > 0 ? 0 : 1;
}
