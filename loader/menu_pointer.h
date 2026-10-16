#ifndef SF4CE_MENU_POINTER_H
#define SF4CE_MENU_POINTER_H

#include <stdint.h>
#include <math.h>

// Coordinates stay in the Vita panel's 960x544 space at every render size.
namespace menu_pointer {
class Pointer {
    uint64_t last = 0;
    bool held = false, back_held = false, armed = false;
public:
    float x = 480, y = 272;
    bool visible = false, down = false, back = false;

    void Update(uint64_t now, bool enabled, bool touch, float dx, float dy,
                bool confirm, bool cancel) {
        float seconds = last && now >= last ? (now - last) / 1000000.0f : 0;
        if (seconds > 0.05f) seconds = 0.05f;
        last = now;
        down = back = false;
        if (!enabled || touch) {
            visible = false;
            armed = false;
        } else {
            // A combat attack held while opening pause must not click a menu.
            if (!confirm && !cancel) armed = true;
            if (dx || dy) {
                const float length = sqrtf(dx * dx + dy * dy);
                if (length > 1) { dx /= length; dy /= length; }
                x += dx * 420 * seconds;
                y += dy * 420 * seconds;
                if (x < 0) x = 0;
                if (x > 959) x = 959;
                if (y < 0) y = 0;
                if (y > 543) y = 543;
                visible = true;
            }
            if (armed && confirm && !held) visible = true;
            down = armed && confirm;
            back = armed && cancel && !back_held;
        }
        held = confirm;
        back_held = cancel;
    }
};
}
#endif
