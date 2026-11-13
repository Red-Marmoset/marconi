// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Screenshot pictures: copying part of the desktop, drawing the cursor in, and shrinking to a size AI models take

#include <MarconiMcp/Picture.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Marconi
{
    namespace Mcp
    {
        PictureSize FitWithin(int width, int height, int maxSide, long long maxPixels)
        {
            PictureSize size;
            if (width <= 0 || height <= 0)
            {
                return size;
            }
            double scale = 1.0;
            if (maxSide > 0)
            {
                scale = std::min(scale, static_cast<double>(maxSide) / width);
                scale = std::min(scale, static_cast<double>(maxSide) / height);
            }
            const double area = static_cast<double>(width) * height;
            if (maxPixels > 0 && area > static_cast<double>(maxPixels))
            {
                scale = std::min(scale, std::sqrt(static_cast<double>(maxPixels) / area));
            }
            size.width = std::max(1, std::min(width, static_cast<int>(std::floor(width * scale))));
            size.height = std::max(1, std::min(height, static_cast<int>(std::floor(height * scale))));
            return size;
        }

        Picture CopyArea(const uint32_t* pixels, size_t stride, int x, int y, int width, int height)
        {
            Picture picture;
            if (width <= 0 || height <= 0)
            {
                return picture;
            }
            picture.width = width;
            picture.height = height;
            picture.pixels.resize(static_cast<size_t>(width) * height);
            for (int row = 0; row < height; ++row)
            {
                const uint32_t* from = pixels + static_cast<size_t>(y + row) * stride + x;
                std::memcpy(picture.pixels.data() + static_cast<size_t>(row) * width, from,
                            static_cast<size_t>(width) * sizeof(uint32_t));
            }
            return picture;
        }

        void DrawCursor(Picture& picture, const CursorShape& cursor, int x, int y)
        {
            if (cursor.width <= 0 || cursor.height <= 0 ||
                cursor.argb.size() < static_cast<size_t>(cursor.width) * cursor.height)
            {
                return;
            }
            const int left = x - cursor.hotspotX;
            const int top = y - cursor.hotspotY;
            for (int row = 0; row < cursor.height; ++row)
            {
                const int py = top + row;
                if (py < 0 || py >= picture.height)
                {
                    continue;
                }
                for (int column = 0; column < cursor.width; ++column)
                {
                    const int px = left + column;
                    if (px < 0 || px >= picture.width)
                    {
                        continue;
                    }
                    const uint32_t argb = cursor.argb[static_cast<size_t>(row) * cursor.width + column];
                    if ((argb >> 24) != 0)
                    {
                        picture.pixels[static_cast<size_t>(py) * picture.width + px] = argb & 0x00ffffffu;
                    }
                }
            }
        }

        Picture ScaleDown(const Picture& source, int outWidth, int outHeight)
        {
            outWidth = std::max(1, std::min(outWidth, source.width));
            outHeight = std::max(1, std::min(outHeight, source.height));
            if (source.width <= 0 || source.height <= 0)
            {
                return Picture();
            }
            if (outWidth == source.width && outHeight == source.height)
            {
                return source;
            }

            Picture out;
            out.width = outWidth;
            out.height = outHeight;
            out.pixels.resize(static_cast<size_t>(outWidth) * outHeight);
            for (int oy = 0; oy < outHeight; ++oy)
            {
                // The source rows this output row covers: at least one.
                const int y0 = static_cast<int>(static_cast<long long>(oy) * source.height / outHeight);
                const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(oy + 1) * source.height /
                                                                 outHeight));
                for (int ox = 0; ox < outWidth; ++ox)
                {
                    const int x0 = static_cast<int>(static_cast<long long>(ox) * source.width / outWidth);
                    const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(ox + 1) * source.width /
                                                                     outWidth));
                    uint64_t r = 0;
                    uint64_t g = 0;
                    uint64_t b = 0;
                    for (int sy = y0; sy < y1; ++sy)
                    {
                        const uint32_t* row = source.pixels.data() + static_cast<size_t>(sy) * source.width;
                        for (int sx = x0; sx < x1; ++sx)
                        {
                            r += (row[sx] >> 16) & 0xff;
                            g += (row[sx] >> 8) & 0xff;
                            b += row[sx] & 0xff;
                        }
                    }
                    const uint64_t count = static_cast<uint64_t>(y1 - y0) * (x1 - x0);
                    const uint32_t ar = static_cast<uint32_t>((r + count / 2) / count);
                    const uint32_t ag = static_cast<uint32_t>((g + count / 2) / count);
                    const uint32_t ab = static_cast<uint32_t>((b + count / 2) / count);
                    out.pixels[static_cast<size_t>(oy) * outWidth + ox] = (ar << 16) | (ag << 8) | ab;
                }
            }
            return out;
        }
    } // namespace Mcp
} // namespace Marconi
