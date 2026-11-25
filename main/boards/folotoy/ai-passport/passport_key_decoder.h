#ifndef PASSPORT_KEY_DECODER_H_
#define PASSPORT_KEY_DECODER_H_

#include <cstdint>

// Pure-logic key-lock decoder for the 3-key ADC resistor ladder on GPIO0.
// Prevents transient contact bounce during hold from sweeping across adjacent
// voltage windows (matches FoloToy/ai-passport bsp_button.c).
struct PassportKeyDecoder {
    int locked = -1;     // Currently locked key index (0=UP, 1=DOWN, 2=OK), or -1
    int candidate = -1;  // Candidate window being timed
    int64_t since_us = 0;
    bool pending = false;
    uint32_t glitches = 0;

    static int WindowOf(int mv) {
        if (mv < 0) return -1;
        if (mv < 150) return 0;   // UP   (~0 mV)
        if (mv < 447) return 1;   // DOWN (~300 mV)
        if (mv < 1900) return 2;  // OK   (~595 mV)
        return -1;                // Released (~3300 mV)
    }

    void Reset() {
        locked = -1;
        candidate = -1;
        since_us = 0;
        pending = false;
    }

    int Feed(int window, int64_t now_us, int confirm_ms = 40, int hold_ms = 120) {
        if (window == locked) {
            if (pending && locked >= 0) {
                ++glitches;
            }
            pending = false;
            candidate = -1;
            return locked;
        }
        if (!pending || window != candidate) {
            pending = true;
            candidate = window;
            since_us = now_us;
        }
        const int64_t need_us =
            static_cast<int64_t>(locked < 0 ? confirm_ms : hold_ms) * 1000LL;
        if (now_us - since_us >= need_us) {
            locked = candidate;
            pending = false;
            candidate = -1;
        }
        return locked;
    }
};

#endif  // PASSPORT_KEY_DECODER_H_
