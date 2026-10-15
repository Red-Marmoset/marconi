// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Translates wx keyboard events into X11 keysyms and tracks which keys are held

#pragma once

#include <cstdint>
#include <functional>
#include <map>

class wxKeyEvent;

namespace RmVncUI
{
    class KeyMapper
    {
    public:
        using SendKey = std::function<void(uint32_t keysym, bool down)>;

        explicit KeyMapper(SendKey send) : m_send(std::move(send)) {}

        // wxEVT_KEY_DOWN. Returns true if the key was sent; false means the caller should Skip() the event so a
        // wxEVT_CHAR arrives with the character the key produces.
        bool OnKeyDown(const wxKeyEvent& event);
        // wxEVT_CHAR, for keys OnKeyDown left to the character event.
        void OnChar(const wxKeyEvent& event);
        // wxEVT_KEY_UP.
        void OnKeyUp(const wxKeyEvent& event);

        // Sends key-up for everything still held (focus lost, disconnect), so nothing sticks down remotely.
        void ReleaseAll();

        // Keysym for keys that don't produce characters (arrows, F-keys, modifiers, numpad, ...), or 0.
        static uint32_t SpecialKeysym(const wxKeyEvent& event);
        // Keysym for the character a wxEVT_CHAR carries, or 0.
        static uint32_t CharKeysym(const wxKeyEvent& event);

    private:
        static long KeyId(const wxKeyEvent& event);
        void Press(long keyId, uint32_t keysym);

        SendKey m_send;
        std::map<long, uint32_t> m_pressed; // physical key -> keysym sent for its press
        // The key whose KEY_DOWN was left for wxEVT_CHAR. The CHAR event itself carries the character, not the
        // physical key (on Windows its raw code is the character), so the press is filed under this id instead.
        long m_charKeyId = -1;
    };
} // namespace RmVncUI
