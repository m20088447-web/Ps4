#pragma once

#include <stdint.h>

/*
 * DualShock 4 input mapping for PhoneCast (PS4 / GoldHEN).
 *
 * The PS4 delivers controller state through the native libScePad API as a
 * button bitmask plus 0..255 analog stick bytes (128 = centered). These
 * values are fixed by the console's pad ABI (the same as ORBIS_PAD_BUTTON_*
 * in OpenOrbis' <orbis/Pad.h>), so they do not depend on SDL's joystick layer
 * or on any SDL GameController mapping database.
 *
 * This header has no SDK dependency so it can be unit-tested on a host PC.
 */

// Button bits in OrbisPadData::buttons.
constexpr uint32_t DS4_PAD_L3        = 0x00000002;
constexpr uint32_t DS4_PAD_R3        = 0x00000004;
constexpr uint32_t DS4_PAD_OPTIONS   = 0x00000008;
constexpr uint32_t DS4_PAD_UP        = 0x00000010;
constexpr uint32_t DS4_PAD_RIGHT     = 0x00000020;
constexpr uint32_t DS4_PAD_DOWN      = 0x00000040;
constexpr uint32_t DS4_PAD_LEFT      = 0x00000080;
constexpr uint32_t DS4_PAD_L2        = 0x00000100;
constexpr uint32_t DS4_PAD_R2        = 0x00000200;
constexpr uint32_t DS4_PAD_L1        = 0x00000400;
constexpr uint32_t DS4_PAD_R1        = 0x00000800;
constexpr uint32_t DS4_PAD_TRIANGLE  = 0x00001000;
constexpr uint32_t DS4_PAD_CIRCLE    = 0x00002000;
constexpr uint32_t DS4_PAD_CROSS     = 0x00004000;
constexpr uint32_t DS4_PAD_SQUARE    = 0x00008000;
constexpr uint32_t DS4_PAD_TOUCHPAD  = 0x00100000;

constexpr uint8_t  DS4_STICK_CENTER   = 128;
constexpr uint8_t  DS4_STICK_DEADZONE = 64;   // ~50% travel before it counts

constexpr uint32_t DS4_REPEAT_FIRST_MS = 400; // delay before a held direction repeats
constexpr uint32_t DS4_REPEAT_NEXT_MS  = 150; // repeat interval afterwards

enum class Ds4Action {
    None,
    Previous,
    Next,
    Confirm,
    Back,
    Options
};

// Direction from the D-pad bits and the left stick. D-pad wins over the stick.
// Up/Left -> Previous, Down/Right -> Next (the menu is a single vertical list).
inline Ds4Action ds4DirectionAction(uint32_t buttons, uint8_t stickX, uint8_t stickY) {
    if (buttons & (DS4_PAD_UP | DS4_PAD_LEFT)) return Ds4Action::Previous;
    if (buttons & (DS4_PAD_DOWN | DS4_PAD_RIGHT)) return Ds4Action::Next;

    const int dx = static_cast<int>(stickX) - DS4_STICK_CENTER;
    const int dy = static_cast<int>(stickY) - DS4_STICK_CENTER;
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    if (ax < DS4_STICK_DEADZONE && ay < DS4_STICK_DEADZONE) return Ds4Action::None;

    // Use the dominant axis so a diagonal push gives one predictable move.
    const int dominant = ay >= ax ? dy : dx;
    return dominant < 0 ? Ds4Action::Previous : Ds4Action::Next;
}

// Per-controller state: previous buttons (for press edges) and repeat timing.
struct Ds4State {
    uint32_t prevButtons = 0;
    Ds4Action heldDirection = Ds4Action::None;
    uint32_t nextRepeatMs = 0;
};

// Call right after a controller (re)connects. Buttons that are already held at
// that moment (for example the Cross press that launched the app) are treated
// as "already down" so they cannot trigger a phantom Confirm.
inline void ds4Reset(Ds4State &state, uint32_t currentButtons) {
    state.prevButtons = currentButtons;
    state.heldDirection = Ds4Action::None;
    state.nextRepeatMs = 0;
}

// Feed one pad sample. Writes up to 4 actions to `out` and returns the count.
// Cross/Circle/Options fire once per press; directions fire on press and then
// repeat while held.
inline int ds4Poll(Ds4State &state, uint32_t buttons, uint8_t stickX, uint8_t stickY,
                   uint32_t nowMs, Ds4Action out[4]) {
    int count = 0;
    const uint32_t pressed = buttons & ~state.prevButtons;
    state.prevButtons = buttons;

    if (pressed & DS4_PAD_CROSS) out[count++] = Ds4Action::Confirm;
    if (pressed & DS4_PAD_CIRCLE) out[count++] = Ds4Action::Back;
    if (pressed & DS4_PAD_OPTIONS) out[count++] = Ds4Action::Options;

    const Ds4Action direction = ds4DirectionAction(buttons, stickX, stickY);
    if (direction == Ds4Action::None) {
        state.heldDirection = Ds4Action::None;
    } else if (direction != state.heldDirection) {
        state.heldDirection = direction;
        state.nextRepeatMs = nowMs + DS4_REPEAT_FIRST_MS;
        out[count++] = direction;
    } else if (static_cast<int32_t>(nowMs - state.nextRepeatMs) >= 0) {
        state.nextRepeatMs = nowMs + DS4_REPEAT_NEXT_MS;
        out[count++] = direction;
    }
    return count;
}
