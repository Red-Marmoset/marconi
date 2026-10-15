// Copyright (c) 2026 Red Marmoset Studios Ltd.
// X11 keysym values used by RFB KeyEvent messages

#pragma once

#include <cstdint>

namespace RmVnc
{
    namespace Keysym
    {
        constexpr uint32_t BackSpace = 0xff08;
        constexpr uint32_t Tab = 0xff09;
        constexpr uint32_t Return = 0xff0d;
        constexpr uint32_t Pause = 0xff13;
        constexpr uint32_t ScrollLock = 0xff14;
        constexpr uint32_t SysReq = 0xff15;
        constexpr uint32_t Escape = 0xff1b;
        constexpr uint32_t Delete = 0xffff;

        constexpr uint32_t Home = 0xff50;
        constexpr uint32_t Left = 0xff51;
        constexpr uint32_t Up = 0xff52;
        constexpr uint32_t Right = 0xff53;
        constexpr uint32_t Down = 0xff54;
        constexpr uint32_t PageUp = 0xff55;
        constexpr uint32_t PageDown = 0xff56;
        constexpr uint32_t End = 0xff57;

        constexpr uint32_t Print = 0xff61;
        constexpr uint32_t Insert = 0xff63;
        constexpr uint32_t Menu = 0xff67;
        constexpr uint32_t Help = 0xff6a;
        constexpr uint32_t Break = 0xff6b;
        constexpr uint32_t NumLock = 0xff7f;

        constexpr uint32_t KP_Enter = 0xff8d;
        constexpr uint32_t KP_Home = 0xff95;
        constexpr uint32_t KP_Left = 0xff96;
        constexpr uint32_t KP_Up = 0xff97;
        constexpr uint32_t KP_Right = 0xff98;
        constexpr uint32_t KP_Down = 0xff99;
        constexpr uint32_t KP_PageUp = 0xff9a;
        constexpr uint32_t KP_PageDown = 0xff9b;
        constexpr uint32_t KP_End = 0xff9c;
        constexpr uint32_t KP_Begin = 0xff9d;
        constexpr uint32_t KP_Insert = 0xff9e;
        constexpr uint32_t KP_Delete = 0xff9f;
        constexpr uint32_t KP_Multiply = 0xffaa;
        constexpr uint32_t KP_Add = 0xffab;
        constexpr uint32_t KP_Separator = 0xffac;
        constexpr uint32_t KP_Subtract = 0xffad;
        constexpr uint32_t KP_Decimal = 0xffae;
        constexpr uint32_t KP_Divide = 0xffaf;
        constexpr uint32_t KP_0 = 0xffb0; // KP_1..KP_9 follow
        constexpr uint32_t KP_Equal = 0xffbd;

        constexpr uint32_t F1 = 0xffbe; // F2..F35 follow

        constexpr uint32_t Shift_L = 0xffe1;
        constexpr uint32_t Shift_R = 0xffe2;
        constexpr uint32_t Control_L = 0xffe3;
        constexpr uint32_t Control_R = 0xffe4;
        constexpr uint32_t CapsLock = 0xffe5;
        constexpr uint32_t Meta_L = 0xffe7;
        constexpr uint32_t Meta_R = 0xffe8;
        constexpr uint32_t Alt_L = 0xffe9;
        constexpr uint32_t Alt_R = 0xffea;
        constexpr uint32_t Super_L = 0xffeb;
        constexpr uint32_t Super_R = 0xffec;
        constexpr uint32_t ISO_Level3_Shift = 0xfe03; // AltGr

        // Keysym for a Unicode character: Latin-1 maps directly, everything else uses the 0x01000000 range.
        // Control characters produce 0 (no keysym); callers map Tab/Return/etc. explicitly.
        constexpr uint32_t FromUnicode(uint32_t codepoint)
        {
            if ((codepoint >= 0x20 && codepoint <= 0x7e) || (codepoint >= 0xa0 && codepoint <= 0xff))
            {
                return codepoint;
            }
            if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0) || codepoint > 0x10ffff)
            {
                return 0;
            }
            return 0x01000000u | codepoint;
        }
    } // namespace Keysym
} // namespace RmVnc
