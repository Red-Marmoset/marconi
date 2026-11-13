// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Virtual game controllers on macOS: not available yet. A Mac needs a small driver extension of our own for this
// (see SERVER_PLAN.md, Phase 8), and Apple has to allow it first, so for now the viewer is told so in plain words.
//
// Not yet built on macOS.

#include "VirtualGamepad.h"

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            class NoGamepadBackend : public VirtualGamepadBackend
            {
            public:
                std::string GetName() const override { return "none"; }

                std::unique_ptr<VirtualGamepad> Create(Wire::GamepadType, RumbleHandler,
                                                       Wire::GamepadResult::Status& status,
                                                       std::string& message) override
                {
                    status = Wire::GamepadResult::Status::NotSupported;
                    message = "Game controllers aren't available on a Mac yet.";
                    return nullptr;
                }
            };
        } // namespace

        std::unique_ptr<VirtualGamepadBackend> CreateSystemGamepadBackend()
        {
            return std::make_unique<NoGamepadBackend>();
        }
    } // namespace Server
} // namespace Marconi
