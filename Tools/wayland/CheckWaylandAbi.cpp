// Checks the hand-copied PipeWire, SPA and D-Bus declarations in Modules/MarconiServerCore (PipeWireLite.h and
// DBusLite_Linux.cpp) against the real headers: every constant, struct size and field position must match, and the
// pods the server builds must be byte for byte what PipeWire's own builder makes. Run it when PipeWire or libdbus
// has a new release, or the declarations are edited. Needs libpipewire-0.3-dev, libspa-0.2-dev and libdbus-1-dev.
//
//   g++ -std=c++17 -fpermissive -w $(pkg-config --cflags libpipewire-0.3 dbus-1) \
//       Tools/wayland/CheckWaylandAbi.cpp Modules/MarconiServerCore/PipeWireLite_Linux.cpp -ldl -o CheckWaylandAbi
//   ./CheckWaylandAbi     (a build error is a mismatch; the program prints IDENTICAL for each pod that matches)
//
// (Run from the repository root; the include of PipeWireLite.h below is relative to this file.)
#include <dbus/dbus.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/props.h>
#include <spa/buffer/meta.h>
#include <spa/pod/builder.h>
#include <cstddef>
#include <cstdio>
#include <vector>
#include <cstring>
#include <string>

#include "../../Modules/MarconiServerCore/PipeWireLite.h"
namespace P = Marconi::Server::Pw;

#define EQ(a, b) static_assert((a) == (b), #a " != " #b)
#define SZ(a, b) static_assert(sizeof(a) == sizeof(b), "size " #a)
#define OFF(t1, m1, t2, m2) static_assert(offsetof(t1, m1) == offsetof(t2, m2), #m1)

EQ(P::kPodBool, SPA_TYPE_Bool); EQ(P::kPodId, SPA_TYPE_Id); EQ(P::kPodInt, SPA_TYPE_Int); EQ(P::kPodLong, SPA_TYPE_Long);
EQ(P::kPodRectangle, SPA_TYPE_Rectangle); EQ(P::kPodFraction, SPA_TYPE_Fraction); EQ(P::kPodObject, SPA_TYPE_Object);
EQ(P::kPodChoice, SPA_TYPE_Choice);
EQ(P::kObjectFormat, SPA_TYPE_OBJECT_Format); EQ(P::kObjectParamBuffers, SPA_TYPE_OBJECT_ParamBuffers);
EQ(P::kObjectParamMeta, SPA_TYPE_OBJECT_ParamMeta);
EQ(P::kParamEnumFormat, SPA_PARAM_EnumFormat); EQ(P::kParamFormat, SPA_PARAM_Format); EQ(P::kParamBuffers, SPA_PARAM_Buffers);
EQ(P::kParamMeta, SPA_PARAM_Meta);
EQ(P::kFormatMediaType, SPA_FORMAT_mediaType); EQ(P::kFormatMediaSubtype, SPA_FORMAT_mediaSubtype);
EQ(P::kFormatVideoFormat, SPA_FORMAT_VIDEO_format); EQ(P::kFormatVideoModifier, SPA_FORMAT_VIDEO_modifier);
EQ(P::kFormatVideoSize, SPA_FORMAT_VIDEO_size); EQ(P::kFormatVideoFramerate, SPA_FORMAT_VIDEO_framerate);
EQ(P::kMediaTypeVideo, SPA_MEDIA_TYPE_video); EQ(P::kMediaSubtypeRaw, SPA_MEDIA_SUBTYPE_raw);
EQ(P::kChoiceNone, SPA_CHOICE_None); EQ(P::kChoiceRange, SPA_CHOICE_Range); EQ(P::kChoiceEnum, SPA_CHOICE_Enum);
EQ(P::kBuffersBuffers, SPA_PARAM_BUFFERS_buffers); EQ(P::kBuffersBlocks, SPA_PARAM_BUFFERS_blocks);
EQ(P::kBuffersSize, SPA_PARAM_BUFFERS_size); EQ(P::kBuffersStride, SPA_PARAM_BUFFERS_stride);
EQ(P::kBuffersAlign, SPA_PARAM_BUFFERS_align); EQ(P::kBuffersDataType, SPA_PARAM_BUFFERS_dataType);
EQ(P::kMetaType, SPA_PARAM_META_type); EQ(P::kMetaSize, SPA_PARAM_META_size); EQ(P::kMetaCursor, SPA_META_Cursor);
EQ(P::kDataMemPtr, SPA_DATA_MemPtr); EQ(P::kDataMemFd, SPA_DATA_MemFd); EQ(P::kDataDmaBuf, SPA_DATA_DmaBuf);
EQ(P::kVideoRGBx, SPA_VIDEO_FORMAT_RGBx); EQ(P::kVideoBGRx, SPA_VIDEO_FORMAT_BGRx); EQ(P::kVideoxRGB, SPA_VIDEO_FORMAT_xRGB);
EQ(P::kVideoxBGR, SPA_VIDEO_FORMAT_xBGR); EQ(P::kVideoRGBA, SPA_VIDEO_FORMAT_RGBA); EQ(P::kVideoBGRA, SPA_VIDEO_FORMAT_BGRA);
EQ(P::kVideoARGB, SPA_VIDEO_FORMAT_ARGB); EQ(P::kVideoABGR, SPA_VIDEO_FORMAT_ABGR); EQ(P::kVideoRGB, SPA_VIDEO_FORMAT_RGB);
EQ(P::kVideoBGR, SPA_VIDEO_FORMAT_BGR);
EQ(P::kStateError, PW_STREAM_STATE_ERROR); EQ(P::kStateUnconnected, PW_STREAM_STATE_UNCONNECTED);
EQ(P::kStateConnecting, PW_STREAM_STATE_CONNECTING); EQ(P::kStatePaused, PW_STREAM_STATE_PAUSED);
EQ(P::kStateStreaming, PW_STREAM_STATE_STREAMING);
EQ(P::kStreamEventsVersion, PW_VERSION_STREAM_EVENTS); EQ(P::kIdAny, PW_ID_ANY);
EQ(P::kDirectionInput, PW_DIRECTION_INPUT); EQ(P::kDirectionOutput, PW_DIRECTION_OUTPUT);
EQ(P::kFlagAutoconnect, PW_STREAM_FLAG_AUTOCONNECT); EQ(P::kFlagInactive, PW_STREAM_FLAG_INACTIVE);
EQ(P::kFlagMapBuffers, PW_STREAM_FLAG_MAP_BUFFERS); EQ(P::kFlagDriver, PW_STREAM_FLAG_DRIVER);

SZ(P::SpaPod, spa_pod); SZ(P::SpaChunk, spa_chunk); SZ(P::SpaData, spa_data); SZ(P::SpaMeta, spa_meta);
SZ(P::SpaBuffer, spa_buffer); SZ(P::SpaPoint, spa_point); SZ(P::SpaMetaCursor, spa_meta_cursor);
SZ(P::SpaMetaBitmap, spa_meta_bitmap); static_assert(sizeof(P::PwBuffer) <= sizeof(pw_buffer), "pwbuf"); OFF(P::PwBuffer, requested, pw_buffer, requested); SZ(P::PwStreamEvents, pw_stream_events);
OFF(P::SpaData, fd, spa_data, fd); OFF(P::SpaData, mapoffset, spa_data, mapoffset); OFF(P::SpaData, maxsize, spa_data, maxsize);
OFF(P::SpaData, data, spa_data, data); OFF(P::SpaData, chunk, spa_data, chunk);
OFF(P::SpaChunk, stride, spa_chunk, stride); OFF(P::SpaChunk, flags, spa_chunk, flags);
OFF(P::SpaBuffer, metas, spa_buffer, metas); OFF(P::SpaBuffer, datas, spa_buffer, datas);
OFF(P::SpaMetaCursor, position, spa_meta_cursor, position); OFF(P::SpaMetaCursor, hotspot, spa_meta_cursor, hotspot);
OFF(P::SpaMetaCursor, bitmapOffset, spa_meta_cursor, bitmap_offset);
OFF(P::SpaMetaBitmap, width, spa_meta_bitmap, size); OFF(P::SpaMetaBitmap, stride, spa_meta_bitmap, stride);
OFF(P::SpaMetaBitmap, offset, spa_meta_bitmap, offset);
OFF(P::PwStreamEvents, stateChanged, pw_stream_events, state_changed);
OFF(P::PwStreamEvents, paramChanged, pw_stream_events, param_changed);
OFF(P::PwStreamEvents, process, pw_stream_events, process);
OFF(P::PwStreamEvents, triggerDone, pw_stream_events, trigger_done);
static_assert(sizeof(spa_hook) <= sizeof(P::SpaHook), "hook");
static_assert(sizeof(DBusMessageIter) <= 128, "iter");
static_assert(alignof(DBusMessageIter) <= 16, "iter align");

int main()
{
    // The pods: build with mine, then compare with the real builder.
    P::PodBuilder mine;
    mine.BeginObject(P::kObjectFormat, P::kParamEnumFormat);
    mine.AddId(P::kFormatMediaType, P::kMediaTypeVideo);
    mine.AddId(P::kFormatMediaSubtype, P::kMediaSubtypeRaw);
    mine.AddIdChoiceEnum(P::kFormatVideoFormat, P::kVideoBGRx, {P::kVideoBGRx, P::kVideoRGBA, P::kVideoBGR});
    mine.AddRectangleChoiceRange(P::kFormatVideoSize, 1920, 1080, 1, 1, 16384, 16384);
    mine.AddFractionChoiceRange(P::kFormatVideoFramerate, 60, 1, 0, 1, 1000, 1);
    const P::SpaPod* a = mine.Finish();

    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod* real = (const spa_pod*)spa_pod_builder_add_object(
        &b, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(4, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_BGR),
        SPA_FORMAT_VIDEO_size,
        SPA_POD_CHOICE_RANGE_Rectangle(&SPA_RECTANGLE(1920, 1080), &SPA_RECTANGLE(1, 1), &SPA_RECTANGLE(16384, 16384)),
        SPA_FORMAT_VIDEO_framerate,
        SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION(60, 1), &SPA_FRACTION(0, 1), &SPA_FRACTION(1000, 1)));
    size_t sa = 8 + a->size, sb = SPA_POD_SIZE(real);
    bool same = sa == sb && memcmp(a, real, sa) == 0;
    printf("enum-format pod: mine %zu bytes, real %zu bytes, %s\n", sa, sb, same ? "IDENTICAL" : "DIFFERENT");

    // A real format pod parsed by mine.
    uint8_t buffer2[1024];
    spa_pod_builder b2 = SPA_POD_BUILDER_INIT(buffer2, sizeof(buffer2));
    spa_video_info_raw raw = SPA_VIDEO_INFO_RAW_INIT(.format = SPA_VIDEO_FORMAT_BGRx, .size = SPA_RECTANGLE(800, 600),
                                                     .framerate = SPA_FRACTION(30, 1));
    const spa_pod* fmt = spa_format_video_raw_build(&b2, SPA_PARAM_Format, &raw);
    P::VideoFormat parsed;
    bool ok = P::ParseVideoFormat((const P::SpaPod*)fmt, parsed);
    printf("parse real Format: %d format=%u %ux%u fps %u/%u modifier=%d\n", ok, parsed.format, parsed.width,
           parsed.height, parsed.framerateNum, parsed.framerateDenom, parsed.hasModifier);

    // Buffers + meta pods vs real
    P::PodBuilder mb;
    mb.BeginObject(P::kObjectParamBuffers, P::kParamBuffers);
    mb.AddIntChoiceRange(P::kBuffersBuffers, 4, 2, 16);
    mb.AddInt(P::kBuffersBlocks, 1);
    mb.AddInt(P::kBuffersSize, 4096);
    mb.AddInt(P::kBuffersStride, 64);
    mb.AddInt(P::kBuffersDataType, 6);
    const P::SpaPod* m1 = mb.Finish();
    uint8_t buffer3[1024];
    spa_pod_builder b3 = SPA_POD_BUILDER_INIT(buffer3, sizeof(buffer3));
    const spa_pod* r1 = (const spa_pod*)spa_pod_builder_add_object(
        &b3, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 16),
        SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1), SPA_PARAM_BUFFERS_size, SPA_POD_Int(4096), SPA_PARAM_BUFFERS_stride,
        SPA_POD_Int(64), SPA_PARAM_BUFFERS_dataType, SPA_POD_Int(6));
    printf("buffers pod: %s\n", (8 + m1->size == SPA_POD_SIZE(r1) && !memcmp(m1, r1, SPA_POD_SIZE(r1))) ? "IDENTICAL" : "DIFFERENT");

    P::PodBuilder mm;
    mm.BeginObject(P::kObjectParamMeta, P::kParamMeta);
    mm.AddId(P::kMetaType, P::kMetaCursor);
    mm.AddIntChoiceRange(P::kMetaSize, 1000, 100, 1000);
    const P::SpaPod* m2 = mm.Finish();
    uint8_t buffer4[1024];
    spa_pod_builder b4 = SPA_POD_BUILDER_INIT(buffer4, sizeof(buffer4));
    const spa_pod* r2 = (const spa_pod*)spa_pod_builder_add_object(
        &b4, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Cursor),
        SPA_PARAM_META_size, SPA_POD_CHOICE_RANGE_Int(1000, 100, 1000));
    printf("meta pod: %s\n", (8 + m2->size == SPA_POD_SIZE(r2) && !memcmp(m2, r2, SPA_POD_SIZE(r2))) ? "IDENTICAL" : "DIFFERENT");
    printf("sizeof DBusMessageIter %zu, spa_hook %zu\n", sizeof(DBusMessageIter), sizeof(spa_hook));
    return 0;
}
