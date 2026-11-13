// Buttons with key repeat for held directions, plus touch taps
#pragma once

#include <switch.h>

namespace monhex::ui
{
    class Input
    {
    public:
        void init(void);
        void update(void);

        // Newly pressed this frame (directions also repeat while held)
        bool pressed(u64 buttons) const { return (mPressed & buttons) != 0; }
        u64 all(void) const { return mPressed; }

        // A finger touched the screen this frame
        bool tapped(void) const { return mTapped; }
        int touchX(void) const { return mTouchX; }
        bool tappedIn(int x, int y, int w, int h) const
        {
            return mTapped && mTouchX >= x && mTouchX < x + w && mTouchY >= y && mTouchY < y + h;
        }

    private:
        PadState mPad{};
        u64 mPressed      = 0;
        u64 mHeldSince    = 0; // ticks when the current held direction started
        u64 mLastRepeat   = 0;
        u64 mRepeatButton = 0;
        bool mTouching    = false;
        bool mTapped      = false;
        int mTouchX = 0, mTouchY = 0;
    };
}
