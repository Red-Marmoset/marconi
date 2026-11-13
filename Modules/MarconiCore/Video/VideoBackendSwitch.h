// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Which platform video backends are built
//
// The Windows (Media Foundation) and macOS (VideoToolbox) video backends were written without a Windows or a Mac
// to build them on, so they are OFF until someone with the machine turns them on, so that a mistake in them cannot
// stop the rest of the program building. To try them:
//   Windows: remove the // in front of `#define MARCONI_VIDEO_WINDOWS 1` below, build, and fix what the compiler says
//            (VideoEncoder_Windows.cpp in MarconiServerCore, VideoDecoder_Windows.cpp in MarconiCore).
//   macOS:   the same with MARCONI_VIDEO_VIDEOTOOLBOX, and add the frameworks named in VideoDecoder_Mac.cpp.
// With a switch off, the platform has no video codec: the server does not offer video and the viewer does not ask
// for it, so everything is still pictures, exactly as before. Once a backend has been run and works, delete its
// switch and make it unconditional.

#pragma once

// #define MARCONI_VIDEO_WINDOWS 1
// #define MARCONI_VIDEO_VIDEOTOOLBOX 1

#if defined(_WIN32) && defined(MARCONI_VIDEO_WINDOWS)
#define MARCONI_VIDEO_USE_MEDIA_FOUNDATION 1
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX && defined(MARCONI_VIDEO_VIDEOTOOLBOX)
#define MARCONI_VIDEO_USE_VIDEOTOOLBOX 1
#endif
#endif
