// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Windows device-independent bitmaps (the clipboard image format other VNC programs exchange) to and from wxImage

#pragma once

#include <MarconiCore/Rfb/Stream.h>

#include <wx/image.h>

namespace MarconiUI
{
    // A 24-bit DIB (BITMAPINFOHEADER followed by bottom-up rows) of the image. Empty if the image isn't valid.
    Marconi::Rfb::Bytes ImageToDib(const wxImage& image);

    // The image in a DIB with 24 or 32 bits per pixel, uncompressed or with colour masks. Invalid if the data is
    // malformed or in a form Marconi doesn't read (palettes, run-length compression).
    wxImage DibToImage(const Marconi::Rfb::Bytes& dib);
} // namespace MarconiUI
