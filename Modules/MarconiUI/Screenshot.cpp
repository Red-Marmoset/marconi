// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Screenshots of the remote desktop: composing the picture and naming the file

#include "Screenshot.h"

#include <algorithm>

namespace MarconiUI
{
    wxImage ComposeScreenshot(const wxImage& picture, const Marconi::Rfb::CursorShape* cursor, const wxPoint& pointer)
    {
        if (!picture.IsOk() || cursor == nullptr || cursor->width <= 0 || cursor->height <= 0 ||
            cursor->argb.size() < static_cast<size_t>(cursor->width) * static_cast<size_t>(cursor->height))
        {
            return picture;
        }
        wxImage image = picture.Copy();
        int left = pointer.x - cursor->hotspotX;
        int top = pointer.y - cursor->hotspotY;
        int width = image.GetWidth();
        int height = image.GetHeight();
        unsigned char* rgb = image.GetData();
        for (int y = std::max(0, -top); y < cursor->height; ++y)
        {
            int targetY = top + y;
            if (targetY >= height)
            {
                break;
            }
            for (int x = std::max(0, -left); x < cursor->width; ++x)
            {
                int targetX = left + x;
                if (targetX >= width)
                {
                    break;
                }
                uint32_t pixel = cursor->argb[static_cast<size_t>(y) * static_cast<size_t>(cursor->width) + static_cast<size_t>(x)];
                unsigned alpha = pixel >> 24;
                if (alpha == 0)
                {
                    continue;
                }
                unsigned char* target = rgb + (static_cast<size_t>(targetY) * static_cast<size_t>(width) + static_cast<size_t>(targetX)) * 3;
                unsigned source[3] = {(pixel >> 16) & 0xFF, (pixel >> 8) & 0xFF, pixel & 0xFF};
                for (int channel = 0; channel < 3; ++channel)
                {
                    target[channel] = static_cast<unsigned char>((source[channel] * alpha + target[channel] * (255 - alpha)) / 255);
                }
            }
        }
        return image;
    }

    wxString SuggestScreenshotName(const wxString& computerName, const wxDateTime& when)
    {
        wxString name;
        const wxString forbidden = "\\/:*?\"<>|";
        for (wxUniChar c : computerName)
        {
            name += (forbidden.Find(c) != wxNOT_FOUND || c < 0x20) ? wxUniChar('-') : c;
        }
        name.Trim(true).Trim(false);
        if (name.empty())
        {
            name = "Screenshot";
        }
        return name + when.Format(" %Y-%m-%d %H-%M-%S") + ".png";
    }
} // namespace MarconiUI
