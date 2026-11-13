// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: reads a screen-sharing video stream from PipeWire (the stream the desktop's screen-sharing portal
// hands out) into the server's own picture format. Shared-memory pictures only; see the notes in SERVER_PLAN.md on
// what GPU-memory (dmabuf) pictures would need.

#pragma once

#include "ScreenSource.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        class PipeWireCapture
        {
        public:
            // How the picture reaches us when the stream gives memory we can't read.
            enum class State
            {
                Connecting, // asked, nothing negotiated yet
                Paused,     // connected, but the desktop sends no pictures right now
                Streaming,
                Failed
            };

            PipeWireCapture();
            ~PipeWireCapture();
            PipeWireCapture(const PipeWireCapture&) = delete;
            PipeWireCapture& operator=(const PipeWireCapture&) = delete;

            // Connects through `remoteFd` (a PipeWire connection from the portal; ownership passes to this object),
            // or to the PipeWire daemon of this session when `remoteFd` is negative, and starts reading the stream
            // on node `nodeId`. `wantCursorMetadata` asks for the pointer's image and position as separate data.
            bool Start(int remoteFd, uint32_t nodeId, bool wantCursorMetadata, std::string& error);

            // Stops reading and lets the connection go.
            void Stop();

            State GetState() const;

            // The reason in plain words when the state is Failed.
            std::string GetFailure() const;

            // Copies the latest picture as 0x00RRGGBB. False until a first picture has arrived. `sequence` goes up
            // with every new picture, so a caller can tell a repeated one.
            bool GetLatest(std::vector<uint32_t>& pixels, int32_t& width, int32_t& height, uint64_t& sequence);

            // The size of the latest picture. False until one has arrived.
            bool GetSize(int32_t& width, int32_t& height) const;

            // Same contract as ScreenSource::GetCursor / GetCursorPosition.
            bool GetCursor(Cursor& out);
            bool GetCursorPosition(int32_t& x, int32_t& y);

            // For tests: converts one picture's rows as the capture does. `format` is a Pw::kVideo* value.
            static bool ConvertRows(uint32_t format, const uint8_t* source, int32_t stride, int32_t width,
                                    int32_t height, std::vector<uint32_t>& out);

        private:
            struct Impl;
            std::unique_ptr<Impl> m_impl;
        };
    } // namespace Server
} // namespace Marconi
