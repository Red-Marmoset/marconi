// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The test-only video codec (see TestVideoCodec.h)

#include "TestVideoCodec.h"

#include <MarconiCore/Wire/WireCompress.h>

#include <cstring>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        const uint8_t kExtradata[4] = {'T', 'E', 'S', 'T'};

        class TestEncoder : public Server::VideoEncoder
        {
        public:
            explicit TestEncoder(std::shared_ptr<CodecProbe> probe) : m_probe(std::move(probe))
            {
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                ++m_probe->encodersCreated;
                ++m_probe->encodersAlive;
            }
            ~TestEncoder() override
            {
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                --m_probe->encodersAlive;
            }

            const char* Name() const override { return "test codec encoder"; }

            bool Configure(const Server::VideoEncoderConfig& config, std::string& error) override
            {
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                if (m_probe->failConfigure)
                {
                    error = "the test encoder was told to refuse";
                    return false;
                }
                m_width = config.width;
                m_height = config.height;
                m_probe->configs.push_back({config.width, config.height});
                m_probe->configBitrates.push_back(config.bitrateKbps);
                m_previous.clear();
                m_sequence = 0;
                m_keyframe = true;
                return true;
            }

            Wire::Bytes GetExtradata() const override { return Wire::Bytes(kExtradata, kExtradata + 4); }

            Server::EncodeStatus Encode(const Server::Capture& capture, const Wire::Rect& area,
                                        Server::EncodedPicture& out, std::string& error) override
            {
                {
                    std::lock_guard<std::mutex> lock(m_probe->mutex);
                    if (m_probe->failEncodeAfter >= 0 && m_probe->picturesEncoded >= m_probe->failEncodeAfter)
                    {
                        error = "the test encoder was told to fail";
                        return Server::EncodeStatus::Failed;
                    }
                }
                if (static_cast<uint32_t>(area.width) != m_width || static_cast<uint32_t>(area.height) != m_height ||
                    area.x < 0 || area.y < 0 || area.x + area.width > capture.width ||
                    area.y + area.height > capture.height)
                {
                    error = "the area is not the size the encoder was configured for";
                    return Server::EncodeStatus::Failed;
                }
                std::vector<uint32_t> current(static_cast<size_t>(m_width) * m_height);
                for (uint32_t y = 0; y < m_height; ++y)
                {
                    const uint32_t* row = capture.Row(area.y + static_cast<int32_t>(y)) + area.x;
                    for (uint32_t x = 0; x < m_width; ++x)
                    {
                        current[static_cast<size_t>(y) * m_width + x] = TestCodecQuantize(row[x]);
                    }
                }
                const bool key = m_keyframe || m_previous.size() != current.size();
                Wire::Bytes raw(current.size() * 4);
                for (size_t i = 0; i < current.size(); ++i)
                {
                    uint32_t value = key ? current[i] : (current[i] ^ m_previous[i]);
                    std::memcpy(raw.data() + i * 4, &value, 4);
                }
                Wire::Writer writer;
                writer.U8(key ? 0 : 1).U32(++m_sequence).U32(m_width).U32(m_height);
                writer.Blob(Wire::ZlibCompress(raw));
                out.data = writer.Take();
                out.keyframe = key;
                out.encodeMicros = 0;
                m_previous = std::move(current);
                m_keyframe = false;
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                ++m_probe->picturesEncoded;
                m_probe->keyframesEncoded += key ? 1 : 0;
                return Server::EncodeStatus::Picture;
            }

            void ForceKeyframe() override
            {
                m_keyframe = true;
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                ++m_probe->keyframesForced;
            }

            void SetBitrate(uint32_t kbps) override
            {
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                m_probe->bitrates.push_back(kbps);
            }

        private:
            std::shared_ptr<CodecProbe> m_probe;
            uint32_t m_width = 0;
            uint32_t m_height = 0;
            std::vector<uint32_t> m_previous;
            uint32_t m_sequence = 0;
            bool m_keyframe = true;
        };

        class TestDecoder : public Video::VideoDecoder
        {
        public:
            explicit TestDecoder(std::shared_ptr<CodecProbe> probe) : m_probe(std::move(probe))
            {
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                ++m_probe->decodersCreated;
                ++m_probe->decodersAlive;
            }
            ~TestDecoder() override
            {
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                --m_probe->decodersAlive;
            }

            const char* Name() const override { return "test codec decoder"; }

            bool Configure(const Video::DecoderStreamInfo& info, std::string& error) override
            {
                {
                    std::lock_guard<std::mutex> lock(m_probe->mutex);
                    if (m_probe->decoderRefusesConfigure)
                    {
                        error = "the test decoder was told to refuse";
                        return false;
                    }
                }
                if (info.extradata != Wire::Bytes(kExtradata, kExtradata + 4))
                {
                    error = "the set-up bytes did not arrive";
                    return false;
                }
                m_width = info.width;
                m_height = info.height;
                Reset();
                return true;
            }

            Video::DecodeStatus Decode(const uint8_t* data, size_t size, bool keyframe, Video::DecodedPicture& out,
                                       std::string& error) override
            {
                try
                {
                    Wire::Reader reader(data, size);
                    const uint8_t kind = reader.U8();
                    const uint32_t sequence = reader.U32();
                    const uint32_t width = reader.U32();
                    const uint32_t height = reader.U32();
                    Wire::Bytes packed = reader.Blob();
                    if (width != m_width || height != m_height)
                    {
                        return Fail(error, "the picture is not the size of the stream");
                    }
                    if ((kind == 0) != keyframe)
                    {
                        return Fail(error, "the keyframe flag does not match the data");
                    }
                    if (kind == 1 && (!m_haveFrame || sequence != m_sequence + 1))
                    {
                        return Fail(error, "a delta frame without the frame before it");
                    }
                    Wire::Bytes raw;
                    if (!Wire::ZlibDecompress(packed, raw, static_cast<size_t>(width) * height * 4))
                    {
                        return Fail(error, "the picture did not decompress");
                    }
                    std::vector<uint32_t> values(static_cast<size_t>(width) * height);
                    for (size_t i = 0; i < values.size(); ++i)
                    {
                        std::memcpy(&values[i], raw.data() + i * 4, 4);
                        if (kind == 1)
                        {
                            values[i] ^= m_previous[i];
                        }
                    }
                    m_previous = values;
                    m_sequence = sequence;
                    m_haveFrame = true;
                    out.width = static_cast<int32_t>(width);
                    out.height = static_cast<int32_t>(height);
                    out.pixels = std::move(values);
                }
                catch (const Wire::WireError& wireError)
                {
                    return Fail(error, wireError.what());
                }
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                ++m_probe->picturesDecoded;
                return Video::DecodeStatus::Picture;
            }

            void Reset() override
            {
                m_haveFrame = false;
                m_previous.clear();
                m_sequence = 0;
            }

        private:
            Video::DecodeStatus Fail(std::string& error, const char* why)
            {
                error = why;
                std::lock_guard<std::mutex> lock(m_probe->mutex);
                ++m_probe->decodeFailures;
                return Video::DecodeStatus::Failed;
            }

            std::shared_ptr<CodecProbe> m_probe;
            uint32_t m_width = 0;
            uint32_t m_height = 0;
            std::vector<uint32_t> m_previous;
            uint32_t m_sequence = 0;
            bool m_haveFrame = false;
        };
    } // namespace

    Server::VideoEncoderFactory MakeTestEncoderFactory(std::shared_ptr<CodecProbe> probe)
    {
        return [probe](Wire::VideoCodec codec) -> std::unique_ptr<Server::VideoEncoder> {
            if (codec != Wire::VideoCodec::Test)
            {
                return nullptr;
            }
            return std::make_unique<TestEncoder>(probe);
        };
    }

    Video::VideoDecoderFactory MakeTestDecoderFactory(std::shared_ptr<CodecProbe> probe)
    {
        return [probe](Wire::VideoCodec codec) -> std::unique_ptr<Video::VideoDecoder> {
            if (codec != Wire::VideoCodec::Test)
            {
                return nullptr;
            }
            return std::make_unique<TestDecoder>(probe);
        };
    }
} // namespace MarconiServerTest
