/* Shared display/input geometry, installed as sys/hakios.viewport.h by drivers/map. */
#ifndef HAKIOS_VIEWPORT_H
#define HAKIOS_VIEWPORT_H
#include <stdint.h>

struct hakios_viewport { uint32_t x, y, width, height; };
int hakios_scanout_viewport(struct hakios_viewport *, uint32_t *, uint32_t *);

static inline int
hakios_viewport_fit(uint32_t sw, uint32_t sh, uint32_t pw, uint32_t ph, struct hakios_viewport *v)
{
    uint32_t w = pw, h = ph;
    if (!sw || !sh || !pw || !ph || sw > 4096 || sh > 4096 || pw > 4096 || ph > 4096) return 0;
    if ((uint64_t)sw * ph < (uint64_t)sh * pw) {
        w = (uint32_t)(((uint64_t)sw * ph + 2u * sh) / (4u * sh)) * 4u;
        if (w > pw) w = pw;
    } else if ((uint64_t)sw * ph > (uint64_t)sh * pw) {
        h = (uint32_t)(((uint64_t)sh * pw + 2u * sw) / (4u * sw)) * 4u;
        if (h > ph) h = ph;
    }
    if (w < 16 || h < 16) return 0;
    *v = (struct hakios_viewport){ (pw - w) / 2, (ph - h) / 2, w, h };
    return 1;
}

static inline uint32_t
hakios_viewport_coordinate(uint32_t value, uint32_t origin, uint32_t length)
{
    if (length < 2 || value <= origin) return 0;
    if (value - origin >= length - 1) return 65535;
    return (uint32_t)((uint64_t)(value - origin) * 65535 / (length - 1));
}
#endif
