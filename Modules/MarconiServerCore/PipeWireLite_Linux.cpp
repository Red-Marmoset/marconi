// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: loads libpipewire-0.3 and builds and reads the few parameter pods screen capture needs. See
// PipeWireLite.h.

#include "PipeWireLite.h"

#include <dlfcn.h>

#include <mutex>

namespace Marconi
{
    namespace Server
    {
        namespace Pw
        {
            namespace
            {
                Api g_api;
                std::string g_error;
                std::once_flag g_once;

                template <typename T>
                bool Resolve(T& target, const char* name)
                {
                    target = reinterpret_cast<T>(dlsym(g_api.handle, name));
                    return target != nullptr;
                }

                void Load()
                {
                    void* handle = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
                    if (handle == nullptr)
                    {
                        g_error = "PipeWire (libpipewire-0.3) isn't installed on this computer, and the desktop "
                                  "needs it to share the screen.";
                        return;
                    }
                    g_api.handle = handle;
                    bool ok = Resolve(g_api.init, "pw_init") &&
                              Resolve(g_api.threadLoopNew, "pw_thread_loop_new") &&
                              Resolve(g_api.threadLoopDestroy, "pw_thread_loop_destroy") &&
                              Resolve(g_api.threadLoopStart, "pw_thread_loop_start") &&
                              Resolve(g_api.threadLoopStop, "pw_thread_loop_stop") &&
                              Resolve(g_api.threadLoopLock, "pw_thread_loop_lock") &&
                              Resolve(g_api.threadLoopUnlock, "pw_thread_loop_unlock") &&
                              Resolve(g_api.threadLoopGetLoop, "pw_thread_loop_get_loop") &&
                              Resolve(g_api.contextNew, "pw_context_new") &&
                              Resolve(g_api.contextDestroy, "pw_context_destroy") &&
                              Resolve(g_api.contextConnect, "pw_context_connect") &&
                              Resolve(g_api.contextConnectFd, "pw_context_connect_fd") &&
                              Resolve(g_api.coreDisconnect, "pw_core_disconnect") &&
                              Resolve(g_api.propertiesNew, "pw_properties_new") &&
                              Resolve(g_api.propertiesSet, "pw_properties_set") &&
                              Resolve(g_api.streamNew, "pw_stream_new") &&
                              Resolve(g_api.streamDestroy, "pw_stream_destroy") &&
                              Resolve(g_api.streamAddListener, "pw_stream_add_listener") &&
                              Resolve(g_api.streamConnect, "pw_stream_connect") &&
                              Resolve(g_api.streamDisconnect, "pw_stream_disconnect") &&
                              Resolve(g_api.streamUpdateParams, "pw_stream_update_params") &&
                              Resolve(g_api.streamDequeue, "pw_stream_dequeue_buffer") &&
                              Resolve(g_api.streamQueue, "pw_stream_queue_buffer") &&
                              Resolve(g_api.streamSetActive, "pw_stream_set_active");
                    // Only the test video source needs this one; the server never calls it.
                    Resolve(g_api.streamTriggerProcess, "pw_stream_trigger_process");
                    if (!ok)
                    {
                        g_error = "The PipeWire library on this computer is not a version Marconi Server understands.";
                        g_api.handle = nullptr;
                        return;
                    }
                    g_api.init(nullptr, nullptr);
                }
            } // namespace

            const Api* GetApi(std::string& error)
            {
                std::call_once(g_once, Load);
                if (g_api.handle == nullptr)
                {
                    error = g_error;
                    return nullptr;
                }
                return &g_api;
            }

            // ---- PodBuilder -----------------------------------------------------------------------------

            void PodBuilder::Put32(uint32_t v)
            {
                const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
                m_bytes.insert(m_bytes.end(), p, p + 4);
            }

            void PodBuilder::Pad()
            {
                while (m_bytes.size() % 8 != 0)
                {
                    m_bytes.push_back(0);
                }
            }

            void PodBuilder::PutProp(uint32_t key, uint32_t flags)
            {
                Put32(key);
                Put32(flags);
            }

            void PodBuilder::BeginObject(uint32_t objectType, uint32_t objectId)
            {
                m_bytes.clear();
                Put32(0); // body size, patched by Finish
                Put32(kPodObject);
                Put32(objectType);
                Put32(objectId);
            }

            void PodBuilder::AddId(uint32_t key, uint32_t value)
            {
                PutProp(key, 0);
                Put32(4);
                Put32(kPodId);
                Put32(value);
                Pad();
            }

            void PodBuilder::AddInt(uint32_t key, int32_t value)
            {
                PutProp(key, 0);
                Put32(4);
                Put32(kPodInt);
                Put32(static_cast<uint32_t>(value));
                Pad();
            }

            void PodBuilder::AddLong(uint32_t key, int64_t value)
            {
                PutProp(key, 0);
                Put32(8);
                Put32(kPodLong);
                Put32(static_cast<uint32_t>(value & 0xffffffff));
                Put32(static_cast<uint32_t>((value >> 32) & 0xffffffff));
            }

            void PodBuilder::AddRectangle(uint32_t key, uint32_t width, uint32_t height)
            {
                PutProp(key, 0);
                Put32(8);
                Put32(kPodRectangle);
                Put32(width);
                Put32(height);
            }

            void PodBuilder::AddFraction(uint32_t key, uint32_t num, uint32_t denom)
            {
                PutProp(key, 0);
                Put32(8);
                Put32(kPodFraction);
                Put32(num);
                Put32(denom);
            }

            void PodBuilder::AddIdChoiceEnum(uint32_t key, uint32_t defaultValue, const std::vector<uint32_t>& values)
            {
                PutProp(key, 0);
                Put32(static_cast<uint32_t>(16 + 4 * (1 + values.size())));
                Put32(kPodChoice);
                Put32(kChoiceEnum);
                Put32(0);
                Put32(4);
                Put32(kPodId);
                Put32(defaultValue); // the default comes first, then every alternative
                for (uint32_t value : values)
                {
                    Put32(value);
                }
                Pad();
            }

            void PodBuilder::AddRectangleChoiceRange(uint32_t key, uint32_t defW, uint32_t defH, uint32_t minW,
                                                     uint32_t minH, uint32_t maxW, uint32_t maxH)
            {
                PutProp(key, 0);
                Put32(16 + 8 * 3);
                Put32(kPodChoice);
                Put32(kChoiceRange);
                Put32(0);
                Put32(8);
                Put32(kPodRectangle);
                Put32(defW);
                Put32(defH);
                Put32(minW);
                Put32(minH);
                Put32(maxW);
                Put32(maxH);
            }

            void PodBuilder::AddFractionChoiceRange(uint32_t key, uint32_t defNum, uint32_t defDenom, uint32_t minNum,
                                                    uint32_t minDenom, uint32_t maxNum, uint32_t maxDenom)
            {
                PutProp(key, 0);
                Put32(16 + 8 * 3);
                Put32(kPodChoice);
                Put32(kChoiceRange);
                Put32(0);
                Put32(8);
                Put32(kPodFraction);
                Put32(defNum);
                Put32(defDenom);
                Put32(minNum);
                Put32(minDenom);
                Put32(maxNum);
                Put32(maxDenom);
            }

            void PodBuilder::AddIntChoiceRange(uint32_t key, int32_t defValue, int32_t minValue, int32_t maxValue)
            {
                PutProp(key, 0);
                Put32(16 + 4 * 3);
                Put32(kPodChoice);
                Put32(kChoiceRange);
                Put32(0);
                Put32(4);
                Put32(kPodInt);
                Put32(static_cast<uint32_t>(defValue));
                Put32(static_cast<uint32_t>(minValue));
                Put32(static_cast<uint32_t>(maxValue));
                Pad();
            }

            const SpaPod* PodBuilder::Finish()
            {
                Pad();
                uint32_t size = static_cast<uint32_t>(m_bytes.size() - 8);
                std::memcpy(m_bytes.data(), &size, 4);
                return reinterpret_cast<const SpaPod*>(m_bytes.data());
            }

            // ---- reading a Format ------------------------------------------------------------------------

            bool ParseVideoFormat(const SpaPod* pod, VideoFormat& out)
            {
                if (pod == nullptr || pod->type != kPodObject || pod->size < 8)
                {
                    return false;
                }
                const uint8_t* body = reinterpret_cast<const uint8_t*>(pod) + sizeof(SpaPod);
                const uint8_t* end = body + pod->size;
                uint32_t objectType = 0;
                std::memcpy(&objectType, body, 4);
                if (objectType != kObjectFormat)
                {
                    return false;
                }
                bool raw = false;
                bool video = false;
                const uint8_t* cursor = body + 8;
                while (cursor + 16 <= end)
                {
                    uint32_t key = 0;
                    SpaPod value;
                    std::memcpy(&key, cursor, 4);
                    std::memcpy(&value, cursor + 8, sizeof(value));
                    const uint8_t* valueBody = cursor + 16;
                    if (valueBody + value.size > end)
                    {
                        break;
                    }
                    auto read32 = [&](size_t offset) {
                        uint32_t v = 0;
                        if (offset + 4 <= value.size)
                        {
                            std::memcpy(&v, valueBody + offset, 4);
                        }
                        return v;
                    };
                    switch (key)
                    {
                        case kFormatMediaType: video = value.type == kPodId && read32(0) == kMediaTypeVideo; break;
                        case kFormatMediaSubtype: raw = value.type == kPodId && read32(0) == kMediaSubtypeRaw; break;
                        case kFormatVideoFormat:
                            if (value.type == kPodId)
                            {
                                out.format = read32(0);
                            }
                            break;
                        case kFormatVideoModifier: out.hasModifier = true; break;
                        case kFormatVideoSize:
                            if (value.type == kPodRectangle)
                            {
                                out.width = read32(0);
                                out.height = read32(4);
                            }
                            break;
                        case kFormatVideoFramerate:
                            if (value.type == kPodFraction)
                            {
                                out.framerateNum = read32(0);
                                out.framerateDenom = read32(4);
                            }
                            break;
                        default: break;
                    }
                    cursor += 16 + ((value.size + 7) & ~7u);
                }
                return video && raw;
            }
        } // namespace Pw
    } // namespace Server
} // namespace Marconi
