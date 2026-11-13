#include "ui/input.hpp"

namespace monhex::ui
{
    namespace
    {
        constexpr u64 DIRECTIONS = HidNpadButton_AnyUp | HidNpadButton_AnyDown |
                                   HidNpadButton_AnyLeft | HidNpadButton_AnyRight;
        constexpr u64 REPEAT_DELAY_NS    = 350'000'000;
        constexpr u64 REPEAT_INTERVAL_NS = 80'000'000;

        u64 nowNs(void) { return armTicksToNs(armGetSystemTick()); }
    }

    void Input::init(void)
    {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&mPad);
        hidInitializeTouchScreen();
    }

    void Input::update(void)
    {
        padUpdate(&mPad);
        u64 down = padGetButtonsDown(&mPad);
        u64 held = padGetButtons(&mPad);
        mPressed = down;

        // Repeat a held direction after a short delay
        u64 now     = nowNs();
        u64 heldDir = held & DIRECTIONS;
        if (down & DIRECTIONS)
        {
            mRepeatButton = down & DIRECTIONS;
            mHeldSince    = now;
            mLastRepeat   = now;
        }
        else if (heldDir && (heldDir & mRepeatButton))
        {
            if (now - mHeldSince > REPEAT_DELAY_NS && now - mLastRepeat > REPEAT_INTERVAL_NS)
            {
                mPressed |= heldDir & mRepeatButton;
                mLastRepeat = now;
            }
        }
        else
        {
            mRepeatButton = 0;
        }

        HidTouchScreenState touch{};
        mTapped = false;
        if (hidGetTouchScreenStates(&touch, 1) && touch.count > 0)
        {
            if (!mTouching)
            {
                mTapped = true;
                mTouchX = static_cast<int>(touch.touches[0].x);
                mTouchY = static_cast<int>(touch.touches[0].y);
            }
            mTouching = true;
        }
        else
        {
            mTouching = false;
        }
    }
}
