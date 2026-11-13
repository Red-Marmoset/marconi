// Copyright (c) 2026 Red Marmoset Studios Ltd.
// macOS screen capture: copy each display with Core Graphics, put them side by side in one picture, and report the
// 64x64 tiles that changed. The Mac counterpart of the Windows GDI and Linux XGetImage capture.
//
// NOTE: written without a Mac to build on; NOT yet compiled or run on macOS. SERVER_PLAN.md (Phase 8) lists what
// has to be checked on a Mac.
//
// Decisions (written down so they can be argued with):
//  - Capture call: CGDisplayCreateImage, one display at a time. It is the simplest thing that works with the Screen
//    Recording permission and needs no run loop or delegate. ScreenCaptureKit is the replacement Apple points to
//    (macOS 12.3+, and the only route if CGDisplayCreateImage stops working); it is objective-C/Swift-shaped, needs
//    a frame callback and a permission flow of its own, and can't be tried without a Mac, so it is left for when
//    this has been run and measured (Phase 4's video work wants it anyway, for GPU frames). Because Apple has marked
//    CGDisplayCreateImage as going away, it is looked up at run time (dlsym) so the build doesn't break whichever
//    SDK is used, and a missing function becomes a plain-words error rather than a crash.
//  - Retina: the viewer gets the desktop in points, the size the Mac's own user sees ("looks like 1440x900"), not
//    in the panel's pixels (2880x1800). Each display's image is scaled down to its size in points with Core
//    Graphics' high-quality filter. This keeps the picture, the mouse coordinates (CGEvent works in points) and the
//    bandwidth in step, at the price of 2x less detail than the panel has. A "full pixels" mode would be a setting
//    and a per-display scale in the input mapping; not built.
//  - All displays are served as one desktop, as on Windows: the rectangle holding every display, with a black gap
//    where the displays don't cover it. Each display is also listed (id, position, size, relative to that
//    rectangle's top left) for the viewer's display picker.
//  - The cursor is not captured (GetCursor says no), so the viewer draws its own. Reading the Mac's cursor image
//    needs AppKit; left for later.

#include "SystemScreen.h"

#include "DirtyTracker.h"
#include "MacDisplays.h"

#include <CoreFoundation/CoreFoundation.h>

#include <dlfcn.h>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            typedef CGImageRef (*CreateImageFunction)(CGDirectDisplayID);

            constexpr const char* kScreenRecordingText =
                "Marconi Server needs your permission to record this Mac's screen. Open System Settings, go to "
                "Privacy & Security, then Screen & System Audio Recording, and turn on Marconi Server. Then quit "
                "Marconi Server and open it again (macOS only applies this permission to programs that start "
                "afterwards).";

            class MacScreen : public ScreenSource
            {
            public:
                ~MacScreen() override
                {
                    if (m_colorSpace != nullptr)
                    {
                        CGColorSpaceRelease(m_colorSpace);
                    }
                }

                bool Open(std::string& error)
                {
                    // Without the permission CGDisplayCreateImage still returns a picture, but one with only the
                    // desktop background and this program's own windows in it, so ask first. The request shows
                    // macOS's own prompt the first time (and does nothing after that).
                    if (!CGPreflightScreenCaptureAccess())
                    {
                        CGRequestScreenCaptureAccess();
                        error = kScreenRecordingText;
                        return false;
                    }
                    m_createImage = reinterpret_cast<CreateImageFunction>(dlsym(RTLD_DEFAULT, "CGDisplayCreateImage"));
                    if (m_createImage == nullptr)
                    {
                        error = "This version of macOS no longer lets Marconi Server copy the screen the way it "
                                "does now. A newer Marconi Server is needed.";
                        return false;
                    }
                    if (ListMacDisplays().empty())
                    {
                        error = "This Mac reports no displays to share (is the screen asleep or being set up?).";
                        return false;
                    }
                    m_colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB); // what viewers expect, on any panel
                    if (m_colorSpace == nullptr)
                    {
                        error = "Could not set up the colours for the screen picture.";
                        return false;
                    }
                    return true;
                }

                std::vector<Wire::Display> GetDisplays() override
                {
                    std::vector<Wire::Display> result;
                    auto displays = ListMacDisplays();
                    if (displays.empty())
                    {
                        return result;
                    }
                    CGRect desktop = MacDesktopBounds(displays);
                    for (const auto& display : displays)
                    {
                        Wire::Display entry;
                        entry.id = display.id;
                        entry.x = static_cast<int32_t>(display.bounds.origin.x - desktop.origin.x);
                        entry.y = static_cast<int32_t>(display.bounds.origin.y - desktop.origin.y);
                        entry.width = static_cast<int32_t>(display.bounds.size.width);
                        entry.height = static_cast<int32_t>(display.bounds.size.height);
                        result.push_back(entry);
                    }
                    return result;
                }

                bool Grab(Capture& out) override
                {
                    auto displays = ListMacDisplays();
                    if (displays.empty())
                    {
                        return false; // being reconfigured, or asleep: the session retries
                    }
                    CGRect desktop = MacDesktopBounds(displays);
                    const int width = static_cast<int>(desktop.size.width);
                    const int height = static_cast<int>(desktop.size.height);
                    if (width <= 0 || height <= 0)
                    {
                        return false;
                    }

                    out.width = width;
                    out.height = height;
                    out.pixels.assign(static_cast<size_t>(width) * height, 0); // black wherever no display is

                    // 8 bits per channel, the skipped byte first, little-endian words: bytes B,G,R,X in memory, so
                    // each uint32 reads 0x??RRGGBB; the unused top byte is cleared below.
                    CGContextRef context = CGBitmapContextCreate(out.pixels.data(), width, height, 8,
                                                                 static_cast<size_t>(width) * 4, m_colorSpace,
                                                                 kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
                    if (context == nullptr)
                    {
                        return false;
                    }
                    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);

                    int drawn = 0;
                    for (const auto& display : displays)
                    {
                        CGImageRef image = m_createImage(display.id);
                        if (image == nullptr)
                        {
                            continue; // that display can't be read right now (asleep, or the screen is locked)
                        }
                        // Core Graphics draws with y going up from the bottom of the picture; the global points go
                        // down from the top.
                        const double left = display.bounds.origin.x - desktop.origin.x;
                        const double top = display.bounds.origin.y - desktop.origin.y;
                        CGRect target = CGRectMake(left, height - (top + display.bounds.size.height),
                                                   display.bounds.size.width, display.bounds.size.height);
                        CGContextDrawImage(context, target, image); // scales a Retina image down to points
                        CGImageRelease(image);
                        ++drawn;
                    }
                    CGContextRelease(context);
                    if (drawn == 0)
                    {
                        return false;
                    }

                    for (uint32_t& pixel : out.pixels)
                    {
                        pixel &= 0x00FFFFFFu;
                    }
                    m_dirty.Update(out);
                    return true;
                }

            private:
                CreateImageFunction m_createImage = nullptr;
                CGColorSpaceRef m_colorSpace = nullptr;
                DirtyTracker m_dirty;
            };
        } // namespace

        std::unique_ptr<ScreenSource> CreateSystemScreen(std::string& error)
        {
            auto screen = std::make_unique<MacScreen>();
            if (!screen->Open(error))
            {
                return nullptr;
            }
            return screen;
        }
    } // namespace Server
} // namespace Marconi
