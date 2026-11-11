#pragma once

#include "ConsoleLine.h"

#include <cstdint>

namespace rpsui::settings_menu
{
    // Physical keyboard edits shared by every line: the engine's WM_CHAR path gives one
    // UTF-16 unit per key, and the editing keys work the same in the console and the searches.
    void typeCharacter(ConsoleLine& line, std::uint32_t utf16Unit);
    // Returns false for keys the caller owns, such as Enter and Page Up.
    [[nodiscard]] bool applyEditKey(ConsoleLine& line, std::uint32_t virtualKey);

    // Draws a line as a field at the cursor and advances past it. `scroll` keeps the caret in
    // view between frames; the caret blinks only while `editing`.
    void drawLineField(const ConsoleLine& line, float width, float height, float fontSize,
        const char* placeholder, float& scroll, bool editing);

    /**
     * The on-screen keyboard. It fills the window it is drawn in, which is the settings menu's keyboard panel
     * under the Config panel, and types into whichever line the caller passes that frame. Shift
     * and Caps belong to the keyboard, so they carry over when the target line changes.
     */
    class OnScreenKeyboard
    {
    public:
        // Returns true on the frame Enter is pressed; the caller decides what Enter means.
        [[nodiscard]] bool draw(ConsoleLine& line);

    private:
        bool _shift{ false };
        bool _caps{ false };
    };
}
