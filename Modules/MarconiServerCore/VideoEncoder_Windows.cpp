// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Windows video encoder: H.264 through a Media Foundation transform (a hardware one when the computer has one,
// Microsoft's software one otherwise), set up for low latency.
//
// NOTE: NOT YET BUILT OR RUN ON WINDOWS. Written from the Media Foundation documentation in an environment with no
// Windows SDK. Expect compile errors and wrong guesses on a first build; the first things to check, in order:
//   1. the includes and import libraries (they are pulled in with #pragma comment below, as this file is the only
//      user of them, so no manifest change is needed);
//   2. EnumerateEncoders / Start: whether the hardware MFT on the test machine is asynchronous (NVIDIA, AMD and
//      Intel ones are) and the event loop in PullOutput works for it; the software MFT is synchronous;
//   3. the codec settings in ApplyCodecSettings (rate control mode, GOP, low latency): some encoders ignore the
//      ones they do not know, and which they ignore differs by vendor;
//   4. that each keyframe carries SPS and PPS in-band (the viewer's decoder needs them), see GetExtradata.
//
// What it does, and what it does not yet:
//   - Colour conversion is on the CPU (BGRX to NV12, BT.709, limited range), one pass over the area. The plan wants
//     it on the GPU straight from a captured texture; that comes with Desktop Duplication, not before.
//   - The picture is padded to even sizes (H.264 needs them); the stream's size stays the real one and the viewer's
//     decoder crops.
//   - Low latency: no B-frames, one frame in flight, constant bitrate (or low-delay variable where the encoder offers
//     only that), the GOP set by `keyframeInterval`, a keyframe on request (CODECAPI_AVEncVideoForceKeyFrame), the
//     bitrate changeable at run time (CODECAPI_AVEncCommonMeanBitRate). Intra refresh is not requested: there is no
//     portable Media Foundation setting for it, so the short GOP does that job.

#include <MarconiCore/Video/VideoBackendSwitch.h>

#if defined(MARCONI_VIDEO_USE_MEDIA_FOUNDATION)

#include "VideoEncoder.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            using Microsoft::WRL::ComPtr;

            std::string HresultText(const char* what, HRESULT result)
            {
                char text[160];
                snprintf(text, sizeof(text), "%s failed (0x%08lX)", what, static_cast<unsigned long>(result));
                return text;
            }

            // Starts Media Foundation for as long as an encoder lives. Counted, because several sessions can each hold one.
            class MediaFoundationScope
            {
            public:
                MediaFoundationScope()
                {
                    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    m_comStarted = SUCCEEDED(com); // RPC_E_CHANGED_MODE leaves it as the thread already had it
                    m_result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
                }
                ~MediaFoundationScope()
                {
                    if (SUCCEEDED(m_result))
                    {
                        MFShutdown();
                    }
                    if (m_comStarted)
                    {
                        CoUninitialize();
                    }
                }
                bool Ok() const { return SUCCEEDED(m_result); }
                HRESULT Result() const { return m_result; }

            private:
                bool m_comStarted = false;
                HRESULT m_result = E_FAIL;
            };

            uint8_t ClampByte(int value)
            {
                return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
            }

            // 0x00RRGGBB pixels of `area` to NV12 (a full-size Y plane, then interleaved U and V at half size), BT.709
            // limited range, into a buffer of the padded (even) size. Pixels past the picture repeat its last row and
            // column.
            void ConvertToNv12(const Capture& capture, const Wire::Rect& area, uint32_t paddedWidth,
                               uint32_t paddedHeight, uint8_t* out)
            {
                uint8_t* yPlane = out;
                uint8_t* uvPlane = out + static_cast<size_t>(paddedWidth) * paddedHeight;
                auto pixelAt = [&](uint32_t x, uint32_t y) {
                    uint32_t cx = std::min<uint32_t>(x, static_cast<uint32_t>(area.width) - 1);
                    uint32_t cy = std::min<uint32_t>(y, static_cast<uint32_t>(area.height) - 1);
                    return capture.Row(area.y + static_cast<int32_t>(cy))[area.x + static_cast<int32_t>(cx)];
                };
                for (uint32_t y = 0; y < paddedHeight; ++y)
                {
                    for (uint32_t x = 0; x < paddedWidth; ++x)
                    {
                        uint32_t p = pixelAt(x, y);
                        int r = (p >> 16) & 0xFF;
                        int g = (p >> 8) & 0xFF;
                        int b = p & 0xFF;
                        yPlane[static_cast<size_t>(y) * paddedWidth + x] =
                            ClampByte(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
                    }
                }
                for (uint32_t y = 0; y < paddedHeight; y += 2)
                {
                    for (uint32_t x = 0; x < paddedWidth; x += 2)
                    {
                        int r = 0, g = 0, b = 0;
                        for (uint32_t dy = 0; dy < 2; ++dy)
                        {
                            for (uint32_t dx = 0; dx < 2; ++dx)
                            {
                                uint32_t p = pixelAt(x + dx, y + dy);
                                r += (p >> 16) & 0xFF;
                                g += (p >> 8) & 0xFF;
                                b += p & 0xFF;
                            }
                        }
                        r /= 4;
                        g /= 4;
                        b /= 4;
                        uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * paddedWidth + x;
                        uv[0] = ClampByte(((-26 * r - 87 * g + 112 * b + 128) >> 8) + 128); // U
                        uv[1] = ClampByte(((112 * r - 102 * g - 10 * b + 128) >> 8) + 128); // V
                    }
                }
            }

            class WindowsH264Encoder : public VideoEncoder
            {
            public:
                ~WindowsH264Encoder() override { Stop(); }

                const char* Name() const override { return m_name.c_str(); }
                bool IsHardware() const override { return m_hardware; }

                bool Configure(const VideoEncoderConfig& config, std::string& error) override
                {
                    Stop();
                    if (config.codec != Wire::VideoCodec::H264 || config.width == 0 || config.height == 0)
                    {
                        error = "this encoder does H.264 only, at a size above zero";
                        return false;
                    }
                    if (!m_mf.Ok())
                    {
                        error = HresultText("Media Foundation startup", m_mf.Result());
                        return false;
                    }
                    m_config = config;
                    m_paddedWidth = (config.width + 1) & ~1u;
                    m_paddedHeight = (config.height + 1) & ~1u;
                    // Hardware first (they are asynchronous transforms), then Microsoft's software encoder.
                    for (bool hardware : {true, false})
                    {
                        std::string why;
                        if (Start(hardware, why))
                        {
                            return true;
                        }
                        error = why;
                        Stop();
                    }
                    return false;
                }

                Wire::Bytes GetExtradata() const override { return m_extradata; }

                EncodeStatus Encode(const Capture& capture, const Wire::Rect& area, EncodedPicture& out,
                                    std::string& error) override
                {
                    if (!m_transform)
                    {
                        error = "the encoder is not configured";
                        return EncodeStatus::Failed;
                    }
                    if (static_cast<uint32_t>(area.width) != m_config.width ||
                        static_cast<uint32_t>(area.height) != m_config.height)
                    {
                        error = "the area is not the size the encoder was configured for";
                        return EncodeStatus::Failed;
                    }
                    const auto started = std::chrono::steady_clock::now();
                    if (m_keyframeRequested)
                    {
                        VARIANT value;
                        VariantInit(&value);
                        value.vt = VT_UI4;
                        value.ulVal = 1;
                        if (m_codecApi)
                        {
                            m_codecApi->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &value);
                        }
                        m_keyframeRequested = false;
                    }

                    // The input sample: the area as NV12.
                    const DWORD size = m_paddedWidth * m_paddedHeight * 3 / 2;
                    ComPtr<IMFMediaBuffer> buffer;
                    HRESULT result = MFCreateMemoryBuffer(size, &buffer);
                    if (FAILED(result))
                    {
                        error = HresultText("MFCreateMemoryBuffer", result);
                        return EncodeStatus::Failed;
                    }
                    BYTE* data = nullptr;
                    result = buffer->Lock(&data, nullptr, nullptr);
                    if (FAILED(result))
                    {
                        error = HresultText("IMFMediaBuffer::Lock", result);
                        return EncodeStatus::Failed;
                    }
                    ConvertToNv12(capture, area, m_paddedWidth, m_paddedHeight, data);
                    buffer->Unlock();
                    buffer->SetCurrentLength(size);
                    ComPtr<IMFSample> sample;
                    result = MFCreateSample(&sample);
                    if (FAILED(result))
                    {
                        error = HresultText("MFCreateSample", result);
                        return EncodeStatus::Failed;
                    }
                    sample->AddBuffer(buffer.Get());
                    const LONGLONG frameDuration = 10000000LL / std::max<uint32_t>(m_config.fps, 1);
                    sample->SetSampleTime(m_frameIndex * frameDuration);
                    sample->SetSampleDuration(frameDuration);
                    ++m_frameIndex;

                    if (!FeedInput(sample.Get(), error))
                    {
                        return EncodeStatus::Failed;
                    }
                    EncodeStatus status = PullOutput(out, error);
                    out.encodeMicros = static_cast<uint32_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started)
                            .count());
                    return status;
                }

                void ForceKeyframe() override { m_keyframeRequested = true; }

                void SetBitrate(uint32_t kbps) override
                {
                    if (!m_codecApi || kbps == 0)
                    {
                        return;
                    }
                    VARIANT value;
                    VariantInit(&value);
                    value.vt = VT_UI4;
                    value.ulVal = kbps * 1000;
                    m_codecApi->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &value);
                    m_config.bitrateKbps = kbps;
                }

                void Flush(std::vector<EncodedPicture>& out) override
                {
                    out.clear(); // low latency: nothing is held back
                    if (m_transform)
                    {
                        m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
                    }
                }

            private:
                bool Start(bool hardware, std::string& error)
                {
                    MFT_REGISTER_TYPE_INFO input = {MFMediaType_Video, MFVideoFormat_NV12};
                    MFT_REGISTER_TYPE_INFO output = {MFMediaType_Video, MFVideoFormat_H264};
                    const UINT32 flags = (hardware ? MFT_ENUM_FLAG_HARDWARE : MFT_ENUM_FLAG_SYNCMFT) |
                                         MFT_ENUM_FLAG_SORTANDFILTER;
                    IMFActivate** activates = nullptr;
                    UINT32 count = 0;
                    HRESULT result = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, &input, &output, &activates, &count);
                    if (FAILED(result) || count == 0)
                    {
                        error = hardware ? "no hardware H.264 encoder found" : "no software H.264 encoder found";
                        if (activates != nullptr)
                        {
                            CoTaskMemFree(activates);
                        }
                        return false;
                    }
                    bool made = false;
                    for (UINT32 i = 0; i < count; ++i)
                    {
                        if (!made)
                        {
                            ComPtr<IMFTransform> transform;
                            if (SUCCEEDED(activates[i]->ActivateObject(IID_PPV_ARGS(&transform))))
                            {
                                WCHAR* friendly = nullptr;
                                UINT32 length = 0;
                                m_name = hardware ? "Media Foundation (hardware)" : "Media Foundation (software)";
                                if (SUCCEEDED(activates[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &friendly,
                                                                                &length)) &&
                                    friendly != nullptr)
                                {
                                    char narrow[200] = {};
                                    WideCharToMultiByte(CP_UTF8, 0, friendly, -1, narrow, sizeof(narrow) - 1, nullptr,
                                                        nullptr);
                                    m_name += std::string(" ") + narrow;
                                    CoTaskMemFree(friendly);
                                }
                                m_transform = transform;
                                m_hardware = hardware;
                                made = true;
                            }
                        }
                        activates[i]->Release();
                    }
                    CoTaskMemFree(activates);
                    if (!made)
                    {
                        error = "could not start an H.264 encoder";
                        return false;
                    }
                    return Prepare(error);
                }

                // Unlocks an asynchronous transform, applies the settings, sets the types and starts streaming.
                bool Prepare(std::string& error)
                {
                    ComPtr<IMFAttributes> attributes;
                    if (SUCCEEDED(m_transform->GetAttributes(&attributes)) && attributes)
                    {
                        UINT32 isAsync = 0;
                        if (SUCCEEDED(attributes->GetUINT32(MF_TRANSFORM_ASYNC, &isAsync)) && isAsync != 0)
                        {
                            attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
                            m_async = true;
                            if (FAILED(m_transform.As(&m_events)))
                            {
                                error = "the asynchronous encoder has no event interface";
                                return false;
                            }
                        }
                        attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
                    }
                    m_transform.As(&m_codecApi); // optional: without it the encoder runs on its defaults
                    ApplyCodecSettings();

                    // The output type (H.264) first, then the input (NV12): encoders want them in that order.
                    ComPtr<IMFMediaType> outType;
                    HRESULT result = MFCreateMediaType(&outType);
                    if (FAILED(result))
                    {
                        error = HresultText("MFCreateMediaType", result);
                        return false;
                    }
                    outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                    outType->SetUINT32(MF_MT_AVG_BITRATE, m_config.bitrateKbps * 1000);
                    MFSetAttributeSize(outType.Get(), MF_MT_FRAME_SIZE, m_paddedWidth, m_paddedHeight);
                    MFSetAttributeRatio(outType.Get(), MF_MT_FRAME_RATE, m_config.fps, 1);
                    MFSetAttributeRatio(outType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                    outType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                    outType->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base); // no B-frames possible
                    result = m_transform->SetOutputType(0, outType.Get(), 0);
                    if (FAILED(result))
                    {
                        error = HresultText("SetOutputType (H.264)", result);
                        return false;
                    }
                    ComPtr<IMFMediaType> inType;
                    MFCreateMediaType(&inType);
                    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
                    MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, m_paddedWidth, m_paddedHeight);
                    MFSetAttributeRatio(inType.Get(), MF_MT_FRAME_RATE, m_config.fps, 1);
                    MFSetAttributeRatio(inType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
                    inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                    result = m_transform->SetInputType(0, inType.Get(), 0);
                    if (FAILED(result))
                    {
                        error = HresultText("SetInputType (NV12)", result);
                        return false;
                    }

                    // The sequence header (SPS and PPS), when the encoder exposes it on its output type.
                    ComPtr<IMFMediaType> current;
                    if (SUCCEEDED(m_transform->GetOutputCurrentType(0, &current)) && current)
                    {
                        UINT32 blobSize = 0;
                        if (SUCCEEDED(current->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &blobSize)) && blobSize > 0 &&
                            blobSize <= Wire::kMaxVideoExtradata)
                        {
                            m_extradata.resize(blobSize);
                            current->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, m_extradata.data(), blobSize, &blobSize);
                        }
                    }
                    MFT_OUTPUT_STREAM_INFO info = {};
                    m_transform->GetOutputStreamInfo(0, &info);
                    m_providesSamples = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                                          MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
                    m_outputSize = info.cbSize;
                    m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
                    m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
                    m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
                    m_keyframeRequested = true;
                    m_frameIndex = 0;
                    return true;
                }

                void SetCodecValue(const GUID& key, ULONG value)
                {
                    if (!m_codecApi)
                    {
                        return;
                    }
                    VARIANT variant;
                    VariantInit(&variant);
                    variant.vt = VT_UI4;
                    variant.ulVal = value;
                    m_codecApi->SetValue(&key, &variant); // an encoder that does not know a setting says no: fine
                }

                void ApplyCodecSettings()
                {
                    SetCodecValue(CODECAPI_AVLowLatencyMode, 1);
                    SetCodecValue(CODECAPI_AVEncCommonRealTime, 1);
                    SetCodecValue(CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
                    SetCodecValue(CODECAPI_AVEncCommonMeanBitRate, m_config.bitrateKbps * 1000);
                    SetCodecValue(CODECAPI_AVEncMPVDefaultBPictureCount, 0); // no B-frames
                    SetCodecValue(CODECAPI_AVEncCommonLowLatency, 1);
                    SetCodecValue(CODECAPI_AVEncMPVGOPSize,
                                  m_config.keyframeInterval != 0 ? m_config.keyframeInterval : 0x7FFFFFFF);
                    SetCodecValue(CODECAPI_AVEncCommonQualityVsSpeed, 20); // favour speed
                }

                // Gives the transform one sample. An asynchronous one asks for input with an event: wait for it.
                bool FeedInput(IMFSample* sample, std::string& error)
                {
                    if (m_async)
                    {
                        while (m_needInputTokens == 0)
                        {
                            if (!WaitEvent(error))
                            {
                                return false;
                            }
                        }
                        --m_needInputTokens;
                    }
                    HRESULT result = m_transform->ProcessInput(0, sample, 0);
                    if (FAILED(result))
                    {
                        error = HresultText("ProcessInput", result);
                        return false;
                    }
                    return true;
                }

                // Handles one event from an asynchronous transform (blocks until there is one).
                bool WaitEvent(std::string& error)
                {
                    ComPtr<IMFMediaEvent> event;
                    HRESULT result = m_events->GetEvent(0, &event);
                    if (FAILED(result))
                    {
                        error = HresultText("GetEvent", result);
                        return false;
                    }
                    MediaEventType type = MEUnknown;
                    event->GetType(&type);
                    if (type == METransformNeedInput)
                    {
                        ++m_needInputTokens;
                    }
                    else if (type == METransformHaveOutput)
                    {
                        ++m_haveOutputTokens;
                    }
                    return true;
                }

                EncodeStatus PullOutput(EncodedPicture& out, std::string& error)
                {
                    if (m_async)
                    {
                        while (m_haveOutputTokens == 0)
                        {
                            if (!WaitEvent(error))
                            {
                                return EncodeStatus::Failed;
                            }
                        }
                        --m_haveOutputTokens;
                    }
                    MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
                    ComPtr<IMFSample> own;
                    if (!m_providesSamples)
                    {
                        ComPtr<IMFMediaBuffer> buffer;
                        MFCreateMemoryBuffer(std::max<DWORD>(m_outputSize, 1 << 20), &buffer);
                        MFCreateSample(&own);
                        own->AddBuffer(buffer.Get());
                        outputBuffer.pSample = own.Get();
                    }
                    DWORD status = 0;
                    HRESULT result = m_transform->ProcessOutput(0, 1, &outputBuffer, &status);
                    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT)
                    {
                        return EncodeStatus::NoOutput;
                    }
                    if (FAILED(result))
                    {
                        error = HresultText("ProcessOutput", result);
                        return EncodeStatus::Failed;
                    }
                    ComPtr<IMFSample> sample;
                    if (m_providesSamples)
                    {
                        sample.Attach(outputBuffer.pSample); // the transform made it; we release it
                    }
                    else
                    {
                        sample = own;
                    }
                    if (outputBuffer.pEvents != nullptr)
                    {
                        outputBuffer.pEvents->Release();
                    }
                    if (!sample)
                    {
                        return EncodeStatus::NoOutput;
                    }
                    ComPtr<IMFMediaBuffer> joined;
                    if (FAILED(sample->ConvertToContiguousBuffer(&joined)))
                    {
                        error = "could not read the encoded picture";
                        return EncodeStatus::Failed;
                    }
                    BYTE* data = nullptr;
                    DWORD length = 0;
                    if (FAILED(joined->Lock(&data, nullptr, &length)))
                    {
                        error = "could not lock the encoded picture";
                        return EncodeStatus::Failed;
                    }
                    out.data.assign(data, data + length);
                    joined->Unlock();
                    UINT32 clean = 0;
                    out.keyframe = SUCCEEDED(sample->GetUINT32(MFSampleExtension_CleanPoint, &clean)) && clean != 0;
                    return EncodeStatus::Picture;
                }

                void Stop()
                {
                    if (m_transform)
                    {
                        m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
                        m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
                    }
                    m_codecApi.Reset();
                    m_events.Reset();
                    m_transform.Reset();
                    m_async = false;
                    m_needInputTokens = 0;
                    m_haveOutputTokens = 0;
                    m_extradata.clear();
                }

                MediaFoundationScope m_mf;
                VideoEncoderConfig m_config;
                uint32_t m_paddedWidth = 0;
                uint32_t m_paddedHeight = 0;
                ComPtr<IMFTransform> m_transform;
                ComPtr<IMFMediaEventGenerator> m_events;
                ComPtr<ICodecAPI> m_codecApi;
                bool m_async = false;
                bool m_hardware = false;
                bool m_providesSamples = false;
                DWORD m_outputSize = 0;
                int m_needInputTokens = 0;
                int m_haveOutputTokens = 0;
                bool m_keyframeRequested = true;
                LONGLONG m_frameIndex = 0;
                Wire::Bytes m_extradata;
                std::string m_name = "Media Foundation";
            };
        } // namespace

        std::vector<Wire::VideoCodec> WindowsEncodableCodecs()
        {
            // H.264 is always there on Windows 7 and later (the software encoder at least). Whether it starts is
            // found out in Configure, which reports a plain-words error and lets the session fall back to stills.
            return {Wire::VideoCodec::H264};
        }

        std::unique_ptr<VideoEncoder> CreateWindowsVideoEncoder(Wire::VideoCodec codec)
        {
            if (codec != Wire::VideoCodec::H264)
            {
                return nullptr;
            }
            return std::make_unique<WindowsH264Encoder>();
        }
    } // namespace Server
} // namespace Marconi

#endif // MARCONI_VIDEO_USE_MEDIA_FOUNDATION
