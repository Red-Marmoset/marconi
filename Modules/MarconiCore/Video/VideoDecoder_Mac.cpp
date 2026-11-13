// Copyright (c) 2026 Red Marmoset Studios Ltd.
// macOS video decoder: H.264 with VideoToolbox, one frame in and one picture out.
//
// NOTE: WRITTEN WITHOUT A MAC; NOT YET COMPILED OR RUN ON macOS. Written from Apple's VideoToolbox and CoreMedia
// headers' documented behaviour. Things to check first on a Mac:
//   1. that it builds: the file is plain C++ over the C APIs (no Objective-C), but the frameworks VideoToolbox,
//      CoreMedia, CoreVideo and CoreFoundation must be on the link line (the module manifest does not list them yet:
//      add "-framework VideoToolbox -framework CoreMedia -framework CoreVideo" to MarconiCore's macOS libraries);
//   2. VTDecompressionSessionDecodeFrame with the synchronous flag: the callback runs before the call returns, which
//      the code relies on (it reads `m_pending` right after);
//   3. that the pixel buffer comes out as 32BGRA (kCVPixelFormatType_32BGRA is requested), which the copy below
//      expects.
//
// The stream is Annex B (start codes). VideoToolbox wants AVCC (each NAL prefixed by its length) and the SPS and PPS
// as a format description, so each frame is converted and the format description is rebuilt whenever the SPS or PPS
// in a keyframe differ from the ones in use.
//
// Not done: showing the decoded buffer without copying it (a CVPixelBuffer can go straight to a Metal texture or an
// AVSampleBufferDisplayLayer); like the Windows backend, the first version copies to 0x00RRGGBB on the CPU.

#include <MarconiCore/Video/VideoBackendSwitch.h>

#if defined(MARCONI_VIDEO_USE_VIDEOTOOLBOX)

#include <MarconiCore/Video/VideoDecoder.h>

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Video
    {
        namespace
        {
            struct Nal
            {
                const uint8_t* data = nullptr; // first byte after the start code
                size_t size = 0;
            };

            // Splits an Annex B byte stream into its NAL units (3- or 4-byte start codes).
            std::vector<Nal> SplitAnnexB(const uint8_t* data, size_t size)
            {
                std::vector<Nal> nals;
                size_t i = 0;
                size_t start = SIZE_MAX;
                while (i + 3 <= size)
                {
                    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
                    {
                        if (start != SIZE_MAX)
                        {
                            size_t end = i;
                            while (end > start && data[end - 1] == 0)
                            {
                                --end; // the zero of a 4-byte start code belongs to the next NAL's start code
                            }
                            nals.push_back({data + start, end - start});
                        }
                        i += 3;
                        start = i;
                    }
                    else
                    {
                        ++i;
                    }
                }
                if (start != SIZE_MAX && start < size)
                {
                    nals.push_back({data + start, size - start});
                }
                return nals;
            }

            class MacH264Decoder : public VideoDecoder
            {
            public:
                ~MacH264Decoder() override { Release(); }

                const char* Name() const override { return "VideoToolbox H.264"; }

                bool Configure(const DecoderStreamInfo& info, std::string& error) override
                {
                    Release();
                    m_width = info.width;
                    m_height = info.height;
                    // The set-up bytes carry the SPS and PPS; a keyframe repeats them, so a missing set is not fatal.
                    m_extraNals = std::vector<uint8_t>(info.extradata.begin(), info.extradata.end());
                    if (!m_extraNals.empty())
                    {
                        std::vector<Nal> nals = SplitAnnexB(m_extraNals.data(), m_extraNals.size());
                        if (!BuildSession(nals, error))
                        {
                            return false;
                        }
                    }
                    m_needsKeyframe = true;
                    return true;
                }

                DecodeStatus Decode(const uint8_t* data, size_t size, bool keyframe, DecodedPicture& out,
                                    std::string& error) override
                {
                    std::vector<Nal> nals = SplitAnnexB(data, size);
                    if (nals.empty())
                    {
                        error = "no NAL units in the frame";
                        return DecodeStatus::Failed;
                    }
                    if (m_needsKeyframe && !keyframe)
                    {
                        error = "a delta frame without its keyframe";
                        return DecodeStatus::Failed;
                    }
                    if (keyframe && !BuildSession(nals, error))
                    {
                        return DecodeStatus::Failed;
                    }
                    if (m_session == nullptr)
                    {
                        error = "no SPS and PPS yet";
                        return DecodeStatus::Failed;
                    }

                    // AVCC: each NAL (parameter sets left out: they live in the format description) behind a 4-byte
                    // big-endian length.
                    std::vector<uint8_t> avcc;
                    for (const Nal& nal : nals)
                    {
                        const uint8_t type = nal.data[0] & 0x1F;
                        if (nal.size == 0 || type == 7 || type == 8)
                        {
                            continue;
                        }
                        avcc.push_back(static_cast<uint8_t>(nal.size >> 24));
                        avcc.push_back(static_cast<uint8_t>(nal.size >> 16));
                        avcc.push_back(static_cast<uint8_t>(nal.size >> 8));
                        avcc.push_back(static_cast<uint8_t>(nal.size));
                        avcc.insert(avcc.end(), nal.data, nal.data + nal.size);
                    }
                    if (avcc.empty())
                    {
                        error = "the frame holds only parameter sets";
                        return DecodeStatus::Failed;
                    }
                    CMBlockBufferRef block = nullptr;
                    OSStatus status = CMBlockBufferCreateWithMemoryBlock(nullptr, nullptr, avcc.size(), nullptr, nullptr,
                                                                         0, avcc.size(), kCMBlockBufferAssureMemoryNowFlag,
                                                                         &block);
                    if (status != noErr)
                    {
                        error = "CMBlockBufferCreateWithMemoryBlock failed";
                        return DecodeStatus::Failed;
                    }
                    CMBlockBufferReplaceDataBytes(avcc.data(), block, 0, avcc.size());
                    CMSampleBufferRef sample = nullptr;
                    const size_t sampleSize = avcc.size();
                    status = CMSampleBufferCreate(nullptr, block, true, nullptr, nullptr, m_format, 1, 0, nullptr, 1,
                                                  &sampleSize, &sample);
                    CFRelease(block);
                    if (status != noErr)
                    {
                        error = "CMSampleBufferCreate failed";
                        return DecodeStatus::Failed;
                    }
                    m_pending = nullptr;
                    VTDecodeInfoFlags flagsOut = 0;
                    // Flags 0: decode synchronously, so the output callback has run when this returns.
                    status = VTDecompressionSessionDecodeFrame(m_session, sample, 0, this, &flagsOut);
                    CFRelease(sample);
                    if (status != noErr)
                    {
                        error = "VTDecompressionSessionDecodeFrame failed (" + std::to_string(static_cast<int>(status)) + ")";
                        return DecodeStatus::Failed;
                    }
                    VTDecompressionSessionWaitForAsynchronousFrames(m_session);
                    if (m_pending == nullptr)
                    {
                        return DecodeStatus::NeedMoreInput;
                    }
                    DecodeStatus result = Copy(m_pending, out, error);
                    CVPixelBufferRelease(m_pending);
                    m_pending = nullptr;
                    if (result == DecodeStatus::Picture)
                    {
                        m_needsKeyframe = false;
                    }
                    return result;
                }

                void Reset() override
                {
                    if (m_session != nullptr)
                    {
                        VTDecompressionSessionWaitForAsynchronousFrames(m_session);
                    }
                    m_needsKeyframe = true;
                }

            private:
                static void OutputCallback(void* decoderRef, void* /*frameRef*/, OSStatus status, VTDecodeInfoFlags /*flags*/,
                                           CVImageBufferRef image, CMTime /*pts*/, CMTime /*duration*/)
                {
                    MacH264Decoder* self = static_cast<MacH264Decoder*>(decoderRef);
                    if (status == noErr && image != nullptr)
                    {
                        self->m_pending = CVPixelBufferRetain(image);
                    }
                }

                // Makes the decompression session from the SPS and PPS found in `nals`, unless the ones in use are the same.
                bool BuildSession(const std::vector<Nal>& nals, std::string& error)
                {
                    std::vector<uint8_t> sps;
                    std::vector<uint8_t> pps;
                    for (const Nal& nal : nals)
                    {
                        if (nal.size == 0)
                        {
                            continue;
                        }
                        const uint8_t type = nal.data[0] & 0x1F;
                        if (type == 7)
                        {
                            sps.assign(nal.data, nal.data + nal.size);
                        }
                        else if (type == 8)
                        {
                            pps.assign(nal.data, nal.data + nal.size);
                        }
                    }
                    if (sps.empty() || pps.empty())
                    {
                        return m_session != nullptr; // nothing new: keep the session, fail only if there never was one
                    }
                    if (m_session != nullptr && sps == m_sps && pps == m_pps)
                    {
                        return true;
                    }
                    Release();
                    const uint8_t* sets[2] = {sps.data(), pps.data()};
                    const size_t sizes[2] = {sps.size(), pps.size()};
                    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(nullptr, 2, sets, sizes, 4,
                                                                                         &m_format);
                    if (status != noErr)
                    {
                        error = "CMVideoFormatDescriptionCreateFromH264ParameterSets failed";
                        return false;
                    }
                    // Ask for 32-bit BGRA so the copy below is a plain one.
                    SInt32 pixelFormat = kCVPixelFormatType_32BGRA;
                    CFNumberRef formatNumber = CFNumberCreate(nullptr, kCFNumberSInt32Type, &pixelFormat);
                    const void* keys[1] = {kCVPixelBufferPixelFormatTypeKey};
                    const void* values[1] = {formatNumber};
                    CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                                                    &kCFTypeDictionaryValueCallBacks);
                    VTDecompressionOutputCallbackRecord callback;
                    callback.decompressionOutputCallback = reinterpret_cast<VTDecompressionOutputCallback>(OutputCallback);
                    callback.decompressionOutputRefCon = this;
                    status = VTDecompressionSessionCreate(nullptr, m_format, nullptr, attributes, &callback, &m_session);
                    CFRelease(attributes);
                    CFRelease(formatNumber);
                    if (status != noErr)
                    {
                        error = "VTDecompressionSessionCreate failed (" + std::to_string(static_cast<int>(status)) + ")";
                        Release();
                        return false;
                    }
                    m_sps = sps;
                    m_pps = pps;
                    return true;
                }

                DecodeStatus Copy(CVPixelBufferRef buffer, DecodedPicture& out, std::string& error)
                {
                    if (CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
                    {
                        error = "could not lock the decoded buffer";
                        return DecodeStatus::Failed;
                    }
                    const size_t width = std::min<size_t>(CVPixelBufferGetWidth(buffer), m_width);
                    const size_t height = std::min<size_t>(CVPixelBufferGetHeight(buffer), m_height);
                    const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
                    const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
                    out.width = static_cast<int32_t>(width);
                    out.height = static_cast<int32_t>(height);
                    out.pixels.resize(width * height);
                    for (size_t y = 0; y < height; ++y)
                    {
                        const uint8_t* row = base + y * stride;
                        for (size_t x = 0; x < width; ++x)
                        {
                            // B, G, R, A in memory -> 0x00RRGGBB
                            out.pixels[y * width + x] = (static_cast<uint32_t>(row[x * 4 + 2]) << 16) |
                                                        (static_cast<uint32_t>(row[x * 4 + 1]) << 8) |
                                                        static_cast<uint32_t>(row[x * 4]);
                        }
                    }
                    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
                    return DecodeStatus::Picture;
                }

                void Release()
                {
                    if (m_session != nullptr)
                    {
                        VTDecompressionSessionInvalidate(m_session);
                        CFRelease(m_session);
                        m_session = nullptr;
                    }
                    if (m_format != nullptr)
                    {
                        CFRelease(m_format);
                        m_format = nullptr;
                    }
                    if (m_pending != nullptr)
                    {
                        CVPixelBufferRelease(m_pending);
                        m_pending = nullptr;
                    }
                    m_sps.clear();
                    m_pps.clear();
                }

                VTDecompressionSessionRef m_session = nullptr;
                CMVideoFormatDescriptionRef m_format = nullptr;
                CVPixelBufferRef m_pending = nullptr; // set by the output callback, read right after the decode call
                std::vector<uint8_t> m_sps;
                std::vector<uint8_t> m_pps;
                std::vector<uint8_t> m_extraNals;
                size_t m_width = 0;
                size_t m_height = 0;
                bool m_needsKeyframe = true;
            };
        } // namespace

        std::vector<Wire::VideoCodec> MacDecodableCodecs()
        {
            return {Wire::VideoCodec::H264};
        }

        std::unique_ptr<VideoDecoder> CreateMacVideoDecoder(Wire::VideoCodec codec)
        {
            if (codec != Wire::VideoCodec::H264)
            {
                return nullptr;
            }
            return std::make_unique<MacH264Decoder>();
        }
    } // namespace Video
} // namespace Marconi

#endif // MARCONI_VIDEO_USE_VIDEOTOOLBOX
