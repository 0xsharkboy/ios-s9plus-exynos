#include <assert.h>
#include <stdio.h>
#include "../drivers/gpu/hakios.viewport.h"

int main(void)
{
    struct hakios_viewport v;
    assert(hakios_viewport_fit(1125, 2436, 1440, 2960, &v));
    assert(v.x == 36 && v.y == 0 && v.width == 1368 && v.height == 2960);
    assert(hakios_viewport_coordinate(0, v.x, v.width) == 0);
    assert(hakios_viewport_coordinate(36, v.x, v.width) == 0);
    assert(hakios_viewport_coordinate(1403, v.x, v.width) == 65535);
    assert(hakios_viewport_coordinate(1439, v.x, v.width) == 65535);
    uint32_t previous = 0;
    for (uint32_t x = 0; x < 1440; x++) {
        uint32_t mapped = hakios_viewport_coordinate(x, v.x, v.width);
        assert(mapped >= previous && mapped <= 65535);
        previous = mapped;
    }
    assert(hakios_viewport_fit(2436, 1125, 2960, 1440, &v));
    assert(v.x == 0 && v.y == 36 && v.width == 2960 && v.height == 1368);
    assert(hakios_viewport_fit(1440, 2960, 1440, 2960, &v));
    assert(v.x == 0 && v.y == 0 && v.width == 1440 && v.height == 2960);
    assert(!hakios_viewport_fit(0, 2436, 1440, 2960, &v));
    assert(!hakios_viewport_fit(4097, 2436, 1440, 2960, &v));
    assert(!hakios_viewport_fit(16, 4096, 4096, 16, &v));
    assert(hakios_viewport_coordinate(30, 36, 0) == 0);
    unsigned cases = 0;
    for (uint32_t sw = 16; sw <= 4096; sw += 127) {
        for (uint32_t sh = 16; sh <= 4096; sh += 131) {
            if (!hakios_viewport_fit(sw, sh, 1440, 2960, &v)) continue;
            assert(v.width >= 16 && v.height >= 16);
            assert(v.x + v.width <= 1440 && v.y + v.height <= 2960);
            assert(v.width == 1440 || v.height == 2960);
            /* Rounding to four pixels introduces at most two pixels of error. */
            double aspect_error = (double)v.width * sh / sw - v.height;
            if (v.width == 1440) assert(aspect_error > -2.1 && aspect_error < 2.1);
            double horizontal_error = (double)v.height * sw / sh - v.width;
            if (v.height == 2960) assert(horizontal_error > -2.1 && horizontal_error < 2.1);
            cases++;
        }
    }
    printf("Viewport geometry, edge touches, invalid sizes and %u bounded fits passed\n", cases);
}
