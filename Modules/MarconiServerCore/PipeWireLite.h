// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: the small part of PipeWire's client library (libpipewire-0.3) that screen capture needs, loaded while
// the program runs so the server starts on a computer without PipeWire. The declarations are copies of PipeWire's
// and SPA's public, stable interface (checked against the real headers with a throwaway program while writing this),
// so building needs no PipeWire development files.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        namespace Pw
        {
            // ---- SPA: pods, buffers (from spa/pod/pod.h, spa/buffer/*.h) ------------------------------------

            struct SpaPod
            {
                uint32_t size; // size of the body
                uint32_t type;
            };

            enum : uint32_t
            {
                kPodBool = 2,
                kPodId = 3,
                kPodInt = 4,
                kPodLong = 5,
                kPodRectangle = 10,
                kPodFraction = 11,
                kPodObject = 15,
                kPodChoice = 19,
                kObjectFormat = 0x40003,
                kObjectParamBuffers = 0x40004,
                kObjectParamMeta = 0x40005,
                kParamEnumFormat = 3,
                kParamFormat = 4,
                kParamBuffers = 5,
                kParamMeta = 6,
                kFormatMediaType = 1,
                kFormatMediaSubtype = 2,
                kFormatVideoFormat = 0x20001,
                kFormatVideoModifier = 0x20002,
                kFormatVideoSize = 0x20003,
                kFormatVideoFramerate = 0x20004,
                kMediaTypeVideo = 2,
                kMediaSubtypeRaw = 1,
                kChoiceNone = 0,
                kChoiceRange = 1,
                kChoiceEnum = 3,
                kBuffersBuffers = 1,
                kBuffersBlocks = 2,
                kBuffersSize = 3,
                kBuffersStride = 4,
                kBuffersAlign = 5,
                kBuffersDataType = 6,
                kMetaType = 1,
                kMetaSize = 2,
                kMetaCursor = 5,
                kDataMemPtr = 1,
                kDataMemFd = 2,
                kDataDmaBuf = 3
            };

            // spa_video_format values used here (spa/param/video/raw.h).
            enum : uint32_t
            {
                kVideoRGBx = 7,
                kVideoBGRx = 8,
                kVideoxRGB = 9,
                kVideoxBGR = 10,
                kVideoRGBA = 11,
                kVideoBGRA = 12,
                kVideoARGB = 13,
                kVideoABGR = 14,
                kVideoRGB = 15,
                kVideoBGR = 16
            };

            struct SpaChunk
            {
                uint32_t offset;
                uint32_t size;
                int32_t stride;
                int32_t flags;
            };

            struct SpaData
            {
                uint32_t type;
                uint32_t flags;
                int64_t fd;
                uint32_t mapoffset;
                uint32_t maxsize;
                void* data;
                SpaChunk* chunk;
            };

            struct SpaMeta
            {
                uint32_t type;
                uint32_t size;
                void* data;
            };

            struct SpaBuffer
            {
                uint32_t nMetas;
                uint32_t nDatas;
                SpaMeta* metas;
                SpaData* datas;
            };

            struct SpaPoint
            {
                int32_t x;
                int32_t y;
            };

            struct SpaMetaCursor
            {
                uint32_t id;
                uint32_t flags;
                SpaPoint position;
                SpaPoint hotspot;
                uint32_t bitmapOffset;
            };

            struct SpaMetaBitmap
            {
                uint32_t format;
                uint32_t width;
                uint32_t height;
                int32_t stride;
                uint32_t offset;
            };

            // ---- PipeWire: stream (from pipewire/stream.h) -------------------------------------------------

            struct PwBuffer
            {
                SpaBuffer* buffer;
                void* userData;
                uint64_t size;
                uint64_t requested;
            };

            enum PwStreamState : int
            {
                kStateError = -1,
                kStateUnconnected = 0,
                kStateConnecting = 1,
                kStatePaused = 2,
                kStateStreaming = 3
            };

            struct PwStreamEvents
            {
                uint32_t version;
                void (*destroy)(void* data);
                void (*stateChanged)(void* data, int oldState, int state, const char* error);
                void (*controlInfo)(void* data, uint32_t id, const void* control);
                void (*ioChanged)(void* data, uint32_t id, void* area, uint32_t size);
                void (*paramChanged)(void* data, uint32_t id, const SpaPod* param);
                void (*addBuffer)(void* data, PwBuffer* buffer);
                void (*removeBuffer)(void* data, PwBuffer* buffer);
                void (*process)(void* data);
                void (*drained)(void* data);
                void (*command)(void* data, const void* command);
                void (*triggerDone)(void* data);
            };

            constexpr uint32_t kStreamEventsVersion = 2;
            constexpr uint32_t kIdAny = 0xffffffffu;
            constexpr int kDirectionInput = 0;
            constexpr int kDirectionOutput = 1;
            constexpr uint32_t kFlagAutoconnect = 1u << 0;
            constexpr uint32_t kFlagInactive = 1u << 1;
            constexpr uint32_t kFlagMapBuffers = 1u << 2;
            constexpr uint32_t kFlagDriver = 1u << 3;

            // spa_hook is 7 pointers' worth today; this is a zeroed block big enough for it.
            struct alignas(16) SpaHook
            {
                unsigned char data[128];
            };

            struct Api
            {
                void* handle = nullptr;
                void (*init)(int*, char***) = nullptr;
                void* (*threadLoopNew)(const char*, const void*) = nullptr;
                void (*threadLoopDestroy)(void*) = nullptr;
                int (*threadLoopStart)(void*) = nullptr;
                void (*threadLoopStop)(void*) = nullptr;
                void (*threadLoopLock)(void*) = nullptr;
                void (*threadLoopUnlock)(void*) = nullptr;
                void* (*threadLoopGetLoop)(void*) = nullptr;
                void* (*contextNew)(void*, void*, size_t) = nullptr;
                void (*contextDestroy)(void*) = nullptr;
                void* (*contextConnect)(void*, void*, size_t) = nullptr;
                void* (*contextConnectFd)(void*, int, void*, size_t) = nullptr;
                int (*coreDisconnect)(void*) = nullptr;
                void* (*propertiesNew)(const char*, ...) = nullptr;
                int (*propertiesSet)(void*, const char*, const char*) = nullptr;
                void* (*streamNew)(void*, const char*, void*) = nullptr;
                void (*streamDestroy)(void*) = nullptr;
                void (*streamAddListener)(void*, SpaHook*, const PwStreamEvents*, void*) = nullptr;
                int (*streamConnect)(void*, int, uint32_t, uint32_t, const SpaPod**, uint32_t) = nullptr;
                int (*streamDisconnect)(void*) = nullptr;
                int (*streamUpdateParams)(void*, const SpaPod**, uint32_t) = nullptr;
                PwBuffer* (*streamDequeue)(void*) = nullptr;
                int (*streamQueue)(void*, PwBuffer*) = nullptr;
                int (*streamSetActive)(void*, bool) = nullptr;
                int (*streamTriggerProcess)(void*) = nullptr;
                uint32_t (*streamGetNodeId)(void*) = nullptr;
            };

            // Loads libpipewire-0.3 (once). Null when it isn't installed or is too old; `error` says so in plain
            // words.
            const Api* GetApi(std::string& error);

            // ---- building and reading the few parameter objects the stream needs -------------------------------

            // Builds SPA pods into a byte vector. Every element is padded to 8 bytes as the format requires.
            class PodBuilder
            {
            public:
                // An object pod (type, id) with properties added by the Add* calls; call Finish for the pod.
                void BeginObject(uint32_t objectType, uint32_t objectId);
                void AddId(uint32_t key, uint32_t value);
                void AddInt(uint32_t key, int32_t value);
                void AddIdChoiceEnum(uint32_t key, uint32_t defaultValue, const std::vector<uint32_t>& values);
                void AddRectangleChoiceRange(uint32_t key, uint32_t defW, uint32_t defH, uint32_t minW, uint32_t minH,
                                             uint32_t maxW, uint32_t maxH);
                void AddFractionChoiceRange(uint32_t key, uint32_t defNum, uint32_t defDenom, uint32_t minNum,
                                            uint32_t minDenom, uint32_t maxNum, uint32_t maxDenom);
                void AddIntChoiceRange(uint32_t key, int32_t defValue, int32_t minValue, int32_t maxValue);
                void AddRectangle(uint32_t key, uint32_t width, uint32_t height);
                void AddFraction(uint32_t key, uint32_t num, uint32_t denom);
                void AddLong(uint32_t key, int64_t value);
                // Ends the object and returns its bytes (valid until the next Begin).
                const SpaPod* Finish();

            private:
                void Put32(uint32_t v);
                void PutProp(uint32_t key, uint32_t flags);
                void Pad();
                std::vector<uint8_t> m_bytes;
            };

            // What a negotiated video format says.
            struct VideoFormat
            {
                uint32_t format = 0; // kVideoBGRx ...
                uint32_t width = 0;
                uint32_t height = 0;
                uint32_t framerateNum = 0;
                uint32_t framerateDenom = 0;
                bool hasModifier = false; // a dmabuf format
            };

            // Reads a Format object pod (what param_changed delivers). False if it isn't a raw video format.
            bool ParseVideoFormat(const SpaPod* pod, VideoFormat& out);
        } // namespace Pw
    } // namespace Server
} // namespace Marconi
