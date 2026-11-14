#include "ds4_controls.h"

#include <cassert>

static int poll1(Ds4State &s, uint32_t b, uint8_t x, uint8_t y, uint32_t t, Ds4Action *first) {
    Ds4Action out[4];
    const int n = ds4Poll(s, b, x, y, t, out);
    if (n > 0 && first) *first = out[0];
    return n;
}

int main() {
    const uint8_t C = DS4_STICK_CENTER;
    Ds4Action a = Ds4Action::None;

    // Face buttons fire once per press, not while held.
    {
        Ds4State s;
        assert(poll1(s, DS4_PAD_CROSS, C, C, 0, &a) == 1 && a == Ds4Action::Confirm);
        assert(poll1(s, DS4_PAD_CROSS, C, C, 16, nullptr) == 0);
        assert(poll1(s, 0, C, C, 32, nullptr) == 0);
        assert(poll1(s, DS4_PAD_CIRCLE, C, C, 48, &a) == 1 && a == Ds4Action::Back);
        assert(poll1(s, 0, C, C, 64, nullptr) == 0);
        assert(poll1(s, DS4_PAD_OPTIONS, C, C, 80, &a) == 1 && a == Ds4Action::Options);
    }

    // Unmapped buttons do nothing.
    {
        Ds4State s;
        assert(poll1(s, DS4_PAD_SQUARE | DS4_PAD_TRIANGLE | DS4_PAD_L1 | DS4_PAD_R1 |
                        DS4_PAD_L2 | DS4_PAD_R2 | DS4_PAD_TOUCHPAD | DS4_PAD_L3 | DS4_PAD_R3,
                     C, C, 0, nullptr) == 0);
    }

    // D-pad directions.
    {
        Ds4State s;
        assert(poll1(s, DS4_PAD_UP, C, C, 0, &a) == 1 && a == Ds4Action::Previous);
        poll1(s, 0, C, C, 10, nullptr);
        assert(poll1(s, DS4_PAD_LEFT, C, C, 20, &a) == 1 && a == Ds4Action::Previous);
        poll1(s, 0, C, C, 30, nullptr);
        assert(poll1(s, DS4_PAD_DOWN, C, C, 40, &a) == 1 && a == Ds4Action::Next);
        poll1(s, 0, C, C, 50, nullptr);
        assert(poll1(s, DS4_PAD_RIGHT, C, C, 60, &a) == 1 && a == Ds4Action::Next);
    }

    // Left stick directions and deadzone (0..255, 128 centered; up = low value).
    {
        Ds4State s;
        assert(poll1(s, 0, C, 0, 0, &a) == 1 && a == Ds4Action::Previous);   // up
        poll1(s, 0, C, C, 10, nullptr);
        assert(poll1(s, 0, C, 255, 20, &a) == 1 && a == Ds4Action::Next);    // down
        poll1(s, 0, C, C, 30, nullptr);
        assert(poll1(s, 0, 0, C, 40, &a) == 1 && a == Ds4Action::Previous);  // left
        poll1(s, 0, C, C, 50, nullptr);
        assert(poll1(s, 0, 255, C, 60, &a) == 1 && a == Ds4Action::Next);    // right
        poll1(s, 0, C, C, 70, nullptr);
        assert(poll1(s, 0, C + DS4_STICK_DEADZONE - 1, C, 80, nullptr) == 0); // inside deadzone
        assert(poll1(s, 0, C, C - DS4_STICK_DEADZONE + 1, 90, nullptr) == 0);
        // Real DS4 sticks rest slightly off-center: must not drift the menu.
        assert(poll1(s, 0, 131, 124, 100, nullptr) == 0);
    }

    // Held direction repeats after the initial delay, then at the interval.
    {
        Ds4State s;
        assert(poll1(s, DS4_PAD_DOWN, C, C, 1000, &a) == 1 && a == Ds4Action::Next);
        assert(poll1(s, DS4_PAD_DOWN, C, C, 1000 + DS4_REPEAT_FIRST_MS - 1, nullptr) == 0);
        assert(poll1(s, DS4_PAD_DOWN, C, C, 1000 + DS4_REPEAT_FIRST_MS, &a) == 1 && a == Ds4Action::Next);
        assert(poll1(s, DS4_PAD_DOWN, C, C, 1000 + DS4_REPEAT_FIRST_MS + DS4_REPEAT_NEXT_MS - 1, nullptr) == 0);
        assert(poll1(s, DS4_PAD_DOWN, C, C, 1000 + DS4_REPEAT_FIRST_MS + DS4_REPEAT_NEXT_MS, &a) == 1);
        // Release then press again fires immediately.
        poll1(s, 0, C, C, 2000, nullptr);
        assert(poll1(s, DS4_PAD_DOWN, C, C, 2001, &a) == 1 && a == Ds4Action::Next);
    }

    // Changing direction fires immediately.
    {
        Ds4State s;
        assert(poll1(s, DS4_PAD_DOWN, C, C, 0, &a) == 1 && a == Ds4Action::Next);
        assert(poll1(s, DS4_PAD_UP, C, C, 5, &a) == 1 && a == Ds4Action::Previous);
    }

    // A button already held at connect time (the Cross press that launched the
    // app) must not trigger Confirm until it is released and pressed again.
    {
        Ds4State s;
        ds4Reset(s, DS4_PAD_CROSS);
        assert(poll1(s, DS4_PAD_CROSS, C, C, 0, nullptr) == 0);
        assert(poll1(s, 0, C, C, 16, nullptr) == 0);
        assert(poll1(s, DS4_PAD_CROSS, C, C, 32, &a) == 1 && a == Ds4Action::Confirm);
    }

    // Press + direction in the same sample yields both actions.
    {
        Ds4State s;
        Ds4Action out[4];
        assert(ds4Poll(s, DS4_PAD_CROSS | DS4_PAD_DOWN, C, C, 0, out) == 2);
        assert(out[0] == Ds4Action::Confirm && out[1] == Ds4Action::Next);
    }

    // Timer wraparound does not break repeat logic.
    {
        Ds4State s;
        const uint32_t t0 = 0xFFFFFFF0u;
        assert(poll1(s, DS4_PAD_DOWN, C, C, t0, &a) == 1);
        assert(poll1(s, DS4_PAD_DOWN, C, C, t0 + DS4_REPEAT_FIRST_MS, &a) == 1);
    }
    return 0;
}
