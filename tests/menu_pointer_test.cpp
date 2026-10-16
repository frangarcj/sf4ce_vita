#include "../loader/menu_pointer.h"
#include <assert.h>

int main() {
    menu_pointer::Pointer p;
    p.Update(1000000, false, false, 0, 0, true, false);
    p.Update(1050000, true, false, 0, 0, true, false);
    assert(!p.down && !p.visible);
    p.Update(1100000, true, false, 0, 0, false, false);
    p.Update(1150000, true, false, 1, 0, true, false);
    assert(p.down && p.visible && p.x == 501);
    p.Update(1200000, true, false, 0, 0, true, false);
    assert(p.down); // Hold/drag uses the same DOWN/MOVE/UP path as touch.
    p.Update(1250000, true, false, 0, 0, false, true);
    assert(!p.down && p.back);
    p.Update(1300000, true, false, 0, 0, false, true);
    assert(!p.back);
    p.Update(1350000, true, true, 1, 0, true, false);
    assert(!p.down && !p.visible);
    p.Update(1400000, true, false, 0, 0, true, false);
    assert(!p.down); // Touch takes priority until confirm is released.
    p.Update(1450000, true, false, 0, 0, false, false);
    p.Update(9000000, true, false, 1, 0, false, false);
    assert(p.x == 522); // A blocking load cannot fling the pointer.
    for (int i = 0; i < 100; ++i)
        p.Update(9050000 + i * 50000, true, false, 1, 1, false, false);
    assert(p.x == 959 && p.y == 543);
    p.Update(15000000, false, false, 0, 0, true, true);
    assert(!p.down && !p.back && !p.visible);
}
