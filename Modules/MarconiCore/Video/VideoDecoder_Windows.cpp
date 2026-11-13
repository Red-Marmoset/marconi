// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Windows video decoder: H.264 through Microsoft's Media Foundation decoder transform, run one frame in, one
// frame out, with the low-latency setting on.
//
// NOTE: NOT YET BUILT OR RUN ON WINDOWS. Written from the Media Foundation documentation in an environment with no
// Windows SDK. Things to check first on a Windows machine:
//   1. the includes and import libraries (pulled in below with #pragma comment);
//   2. that the decoder accepts Annex B input with SPS and PPS in-band (the server sends them on every keyframe) and
//      that giving it the stream's `extradata` first is not needed or harmful;
//   3. the output type: NV12 is asked for; some Windows builds offer only YUY2 or I420 first (see PickOutputType);
//   4. the row pitch of the decoded frame (IMF2DBuffer is used where the buffer has it).
//
// What it does not do yet: hardware decoding. Microsoft's decoder uses DXVA when given a Direct3D device manager
// (MFT_MESSAGE_SET_D3D_MANAGER); that and presenting the decoded texture on the GPU are the Phase 4 follow-up. Today
// the picture is converted to 0x00RRGGBB on the CPU, which is fine for 1080p and slower at 4K.
//
// Modelled on the Media Foundation use in the brag project (VideoSource_MediaFoundation.cpp), which reads files with
// IMFSourceReader; this talks to the transform directly because a stream has no file to read.

#include <MarconiCore/Video/VideoBackendSwitch.h>

#if defined(MARCONI_VIDEO_USE_MEDIA_FOUNDATION)

#include <MarconiCore/Video/VideoDecoder.h>

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
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

namespace Marconi
{
    namespace Video
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

            uint8_t ClampByte(int value)
            {
                return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
            }

            uint32_t YuvToRgb(int y, int u, int v) // BT.709, limited range
            {
                int c = y - 16;
                int d = u - 128;
                int e = v - 128;
                int r = (298 * c + 459 * e + 128) >> 8;
                int g = (298 * c - 55 * d - 136 * e + 128) >> 8;
                int b = (298 * c + 541 * d + 128) >> 8;
                return (static_cast<uint32_t>(ClampByte(r)) << 16) | (static_cast<uint32_t>(ClampByte(g)) << 8) |
                       ClampByte(b);
            }

            class WindowsH264Decoder : public VideoDecoder
            {
            public:
                WindowsH264Decoder()
                {
                    HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    m_comStarted = SUCCEEDED(com);
                    m_mfResult = MFStartup(MF_VERSION, MFSTARTUP_FULL);
                }
                ~WindowsH264Decoder() override
                {
                    m_transform.Reset();
                    if (SUCCEEDED(m_mfResult))
                    {
                        MFShutdown();
                    }
                    if (m_comStarted)
                    {
                        CoUninitialize();
                    }
                }

                const char* Name() const override { return "Media Foundation H.264 (software)"; }

                bool Configure(const DecoderStreamInfo& info, std::string& error) override
                {
                    m_transform.Reset();
                    if (FAILED(m_mfResult))
                    {
                        error = HresultText("Media Foundation startup", m_mfResult);
                        return false;
                    }
                    m_width = info.width;
                    m_height = info.height;
                    HRESULT result = CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER,
                                                      IID_PPV_ARGS(&m_transform));
                    if (FAILED(result))
                    {
                        error = HresultText("creating the H.264 decoder", result);
                        return false;
                    }
                    ComPtr<IMFAttributes> attributes;
                    if (SUCCEEDED(m_transform->GetAttributes(&attributes)) && attributes)
                    {
                        attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
                    }
                    ComPtr<ICodecAPI> codecApi;
                    if (SUCCEEDED(m_transform.As(&codecApi)))
                    {
                        VARIANT value;
                        VariantInit(&value);
                        value.vt = VT_UI4;
                        value.ulVal = 1;
                        codecApi->SetValue(&CODECAPI_AVLowLatencyMode, &value);
                    }
                    ComPtr<IMFMediaType> inType;
                    MFCreateMediaType(&inType);
                    inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                    MFSetAttributeSize(inType.Get(), MF_MT_FRAME_SIZE, m_width, m_height);
                    inType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                    result = m_transform->SetInputType(0, inType.Get(), 0);
                    if (FAILED(result))
                    {
                        error = HresultText("SetInputType (H.264)", result);
                        return false;
                    }
                    if (!PickOutputType(error))
                    {
                        return false;
                    }
                    m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
                    m_transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
                    m_extradata = info.extradata;
                    m_frameIndex = 0;
                    m_needsKeyframe = true;
                    return true;
                }

                DecodeStatus Decode(const uint8_t* data, size_t size, bool keyframe, DecodedPicture& out,
                                    std::string& error) override
                {
                    if (!m_transform)
                    {
                        error = "the decoder is not configured";
                        return DecodeStatus::Failed;
                    }
                    if (m_needsKeyframe && !keyframe)
                    {
                        error = "a delta frame without its keyframe";
                        return DecodeStatus::Failed;
                    }
                    // A keyframe is sent with its SPS and PPS, but a stream that does not carry them in-band gets them
                    // from the VideoStart set-up bytes.
                    std::vector<uint8_t> joined;
                    const uint8_t* input = data;
                    size_t inputSize = size;
                    if (keyframe && !m_extradata.empty() && !StartsWithParameterSets(data, size))
                    {
                        joined = m_extradata;
                        joined.insert(joined.end(), data, data + size);
                        input = joined.data();
                        inputSize = joined.size();
                    }
                    ComPtr<IMFMediaBuffer> buffer;
                    HRESULT result = MFCreateMemoryBuffer(static_cast<DWORD>(inputSize), &buffer);
                    if (FAILED(result))
                    {
                        error = HresultText("MFCreateMemoryBuffer", result);
                        return DecodeStatus::Failed;
                    }
                    BYTE* bytes = nullptr;
                    if (FAILED(buffer->Lock(&bytes, nullptr, nullptr)))
                    {
                        error = "could not lock the input buffer";
                        return DecodeStatus::Failed;
                    }
                    std::memcpy(bytes, input, inputSize);
                    buffer->Unlock();
                    buffer->SetCurrentLength(static_cast<DWORD>(inputSize));
                    ComPtr<IMFSample> sample;
                    MFCreateSample(&sample);
                    sample->AddBuffer(buffer.Get());
                    sample->SetSampleTime(m_frameIndex * 166667LL); // the decoder wants increasing times, not true ones
                    sample->SetSampleDuration(166667LL);
                    ++m_frameIndex;
                    if (keyframe)
                    {
                        sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
                    }
                    result = m_transform->ProcessInput(0, sample.Get(), 0);
                    if (FAILED(result))
                    {
                        error = HresultText("ProcessInput", result);
                        return DecodeStatus::Failed;
                    }
                    DecodeStatus status = PullPicture(out, error);
                    if (status == DecodeStatus::Picture)
                    {
                        m_needsKeyframe = false;
                    }
                    return status;
                }

                void Reset() override
                {
                    if (m_transform)
                    {
                        m_transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
                    }
                    m_needsKeyframe = true;
                }

            private:
                static bool StartsWithParameterSets(const uint8_t* data, size_t size)
                {
                    // Looks for an SPS (NAL type 7) in the first few NALs of an Annex B frame.
                    for (size_t i = 0; i + 4 < size && i < 64; ++i)
                    {
                        if (data[i] == 0 && data[i + 1] == 0 && (data[i + 2] == 1 || (data[i + 2] == 0 && data[i + 3] == 1)))
                        {
                            size_t header = data[i + 2] == 1 ? i + 3 : i + 4;
                            if (header < size && (data[header] & 0x1F) == 7)
                            {
                                return true;
                            }
                        }
                    }
                    return false;
                }

                // NV12 preferred; whichever of the listed output types is the first NV12 or, failing that, the first.
                bool PickOutputType(std::string& error)
                {
                    for (DWORD index = 0;; ++index)
                    {
                        ComPtr<IMFMediaType> type;
                        HRESULT result = m_transform->GetOutputAvailableType(0, index, &type);
                        if (FAILED(result))
                        {
                            break;
                        }
                        GUID subtype = {};
                        type->GetGUID(MF_MT_SUBTYPE, &subtype);
                        if (subtype == MFVideoFormat_NV12)
                        {
                            if (SUCCEEDED(m_transform->SetOutputType(0, type.Get(), 0)))
                            {
                                m_outputIsNv12 = true;
                                ReadOutputSize(type.Get());
                                return true;
                            }
                        }
                    }
                    error = "the decoder offers no NV12 output";
                    return false;
                }

                void ReadOutputSize(IMFMediaType* type)
                {
                    UINT32 width = 0;
                    UINT32 height = 0;
                    if (SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)))
                    {
                        m_outWidth = width;
                        m_outHeight = height;
                    }
                }

                DecodeStatus PullPicture(DecodedPicture& out, std::string& error)
                {
                    MFT_OUTPUT_STREAM_INFO info = {};
                    m_transform->GetOutputStreamInfo(0, &info);
                    const bool provides = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                                           MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
                    for (int attempt = 0; attempt < 2; ++attempt)
                    {
                        MFT_OUTPUT_DATA_BUFFER outputBuffer = {};
                        ComPtr<IMFSample> own;
                        if (!provides)
                        {
                            ComPtr<IMFMediaBuffer> buffer;
                            MFCreateMemoryBuffer(std::max<DWORD>(info.cbSize, m_outWidth * m_outHeight * 3 / 2), &buffer);
                            MFCreateSample(&own);
                            own->AddBuffer(buffer.Get());
                            outputBuffer.pSample = own.Get();
                        }
                        DWORD status = 0;
                        HRESULT result = m_transform->ProcessOutput(0, 1, &outputBuffer, &status);
                        if (result == MF_E_TRANSFORM_NEED_MORE_INPUT)
                        {
                            return DecodeStatus::NeedMoreInput;
                        }
                        if (result == MF_E_TRANSFORM_STREAM_CHANGE)
                        {
                            // The decoder learned the real size from the stream's SPS: take its new output type.
                            if (outputBuffer.pSample != nullptr && provides)
                            {
                                outputBuffer.pSample->Release();
                            }
                            if (!PickOutputType(error))
                            {
                                return DecodeStatus::Failed;
                            }
                            continue;
                        }
                        if (FAILED(result))
                        {
                            error = HresultText("ProcessOutput", result);
                            return DecodeStatus::Failed;
                        }
                        ComPtr<IMFSample> sample;
                        if (provides)
                        {
                            sample.Attach(outputBuffer.pSample);
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
                            return DecodeStatus::NeedMoreInput;
                        }
                        return ConvertSample(sample.Get(), out, error);
                    }
                    error = "the decoder kept changing its output type";
                    return DecodeStatus::Failed;
                }

                DecodeStatus ConvertSample(IMFSample* sample, DecodedPicture& out, std::string& error)
                {
                    ComPtr<IMFMediaBuffer> buffer;
                    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
                    {
                        error = "could not read the decoded picture";
                        return DecodeStatus::Failed;
                    }
                    BYTE* data = nullptr;
                    LONG pitch = static_cast<LONG>(m_outWidth);
                    ComPtr<IMF2DBuffer> flat;
                    bool locked2D = false;
                    DWORD length = 0;
                    if (SUCCEEDED(buffer.As(&flat)) && SUCCEEDED(flat->Lock2D(&data, &pitch)))
                    {
                        locked2D = true;
                    }
                    else if (FAILED(buffer->Lock(&data, nullptr, &length)))
                    {
                        error = "could not lock the decoded picture";
                        return DecodeStatus::Failed;
                    }
                    const uint32_t width = std::min<uint32_t>(m_width, m_outWidth);
                    const uint32_t height = std::min<uint32_t>(m_height, m_outHeight);
                    out.width = static_cast<int32_t>(width);
                    out.height = static_cast<int32_t>(height);
                    out.pixels.assign(static_cast<size_t>(width) * height, 0);
                    // NV12: a Y plane of `height` rows of `pitch` bytes (the buffer's rows may be taller than the
                    // picture), then the interleaved UV plane at half height.
                    const uint32_t rows = m_outHeight; // the UV plane starts after `rows` rows of Y (check on a real run)
                    const uint8_t* yPlane = data;
                    const uint8_t* uvPlane = data + static_cast<size_t>(pitch) * rows;
                    for (uint32_t y = 0; y < height; ++y)
                    {
                        for (uint32_t x = 0; x < width; ++x)
                        {
                            int luma = yPlane[static_cast<size_t>(y) * pitch + x];
                            const uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * pitch + (x & ~1u);
                            out.pixels[static_cast<size_t>(y) * width + x] = YuvToRgb(luma, uv[0], uv[1]);
                        }
                    }
                    if (locked2D)
                    {
                        flat->Unlock2D();
                    }
                    else
                    {
                        buffer->Unlock();
                    }
                    return DecodeStatus::Picture;
                }

                bool m_comStarted = false;
                HRESULT m_mfResult = E_FAIL;
                ComPtr<IMFTransform> m_transform;
                uint32_t m_width = 0;
                uint32_t m_height = 0;
                uint32_t m_outWidth = 0;
                uint32_t m_outHeight = 0;
                bool m_outputIsNv12 = false;
                std::vector<uint8_t> m_extradata;
                LONGLONG m_frameIndex = 0;
                bool m_needsKeyframe = true;
            };
        } // namespace

        std::vector<Wire::VideoCodec> WindowsDecodableCodecs()
        {
            // The H.264 decoder ships with Windows 7 and later (not on some "N" editions without the media pack:
            // Configure then fails with a plain-words error and the session falls back to stills).
            return {Wire::VideoCodec::H264};
        }

        std::unique_ptr<VideoDecoder> CreateWindowsVideoDecoder(Wire::VideoCodec codec)
        {
            if (codec != Wire::VideoCodec::H264)
            {
                return nullptr;
            }
            return std::make_unique<WindowsH264Decoder>();
        }
    } // namespace Video
} // namespace Marconi

#endif // MARCONI_VIDEO_USE_MEDIA_FOUNDATION
