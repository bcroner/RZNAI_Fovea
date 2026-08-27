/* rzn_spiral.c -- closed-form foveal spiral <-> sensor coordinate mapping. */

#include "rzn_spiral.h"

/* Integer square root, no floating point (so the mapping is bit-exact and
 * reproducible across compilers and targets). */
static uint32_t rzn_isqrt64(uint64_t n)
{
    uint64_t x, y;
    if (n == 0) return 0;
    x = n;
    y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return (uint32_t)x;
}

int32_t rzn_spiral_ring(int64_t idx)
{
    uint32_t s;
    if (idx <= 0) return 0;
    /* Ring r spans [(2r-1)^2, (2r+1)^2).  With s = floor(sqrt(idx)),
     * r = (s + 1) / 2 in integer arithmetic. */
    s = rzn_isqrt64((uint64_t)idx);
    return (int32_t)((s + 1) / 2);
}

void rzn_spiral_to_xy(const rzn_seed *seed, int64_t idx, int32_t *x, int32_t *y)
{
    int32_t r;
    int64_t t, leg;

    if (idx <= 0) {
        *x = seed->cx;
        *y = seed->cy;
        return;
    }

    r = rzn_spiral_ring(idx);
    t = idx - rzn_ring_start(r);   /* 0 <= t < 8r */
    leg = 2 * (int64_t)r;

    if (t < leg) {
        /* DOWN the right edge, from (cx+r, cy-r+1) to (cx+r, cy+r). */
        *x = seed->cx + r;
        *y = (int32_t)(seed->cy - r + 1 + t);
    } else if (t < 2 * leg) {
        /* LEFT along the bottom edge, from (cx+r-1, cy+r) to (cx-r, cy+r). */
        *x = (int32_t)(seed->cx + r - 1 - (t - leg));
        *y = seed->cy + r;
    } else if (t < 3 * leg) {
        /* UP the left edge, from (cx-r, cy+r-1) to (cx-r, cy-r). */
        *x = seed->cx - r;
        *y = (int32_t)(seed->cy + r - 1 - (t - 2 * leg));
    } else {
        /* RIGHT along the top edge, from (cx-r+1, cy-r) to (cx+r, cy-r). */
        *x = (int32_t)(seed->cx - r + 1 + (t - 3 * leg));
        *y = seed->cy - r;
    }
}

int64_t rzn_xy_to_spiral(const rzn_seed *seed, int32_t x, int32_t y)
{
    int32_t dx = x - seed->cx;
    int32_t dy = y - seed->cy;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;
    int32_t r = adx > ady ? adx : ady;
    int64_t base, leg, t;

    if (r == 0) return 0;

    base = rzn_ring_start(r);
    leg  = 2 * (int64_t)r;

    /* The four guards below are mutually exclusive: each corner belongs to
     * exactly one leg (it is always that leg's final cell). */
    if (dx == r && dy >= -r + 1) {
        t = (int64_t)dy + r - 1;                      /* DOWN   */
    } else if (dy == r && dx <= r - 1) {
        t = leg + ((int64_t)r - 1 - dx);              /* LEFT   */
    } else if (dx == -r && dy <= r - 1) {
        t = 2 * leg + ((int64_t)r - 1 - dy);          /* UP     */
    } else {
        t = 3 * leg + ((int64_t)dx + r - 1);          /* RIGHT  */
    }

    return base + t;
}

int32_t rzn_last_ring(const rzn_seed *seed, int32_t w, int32_t h)
{
    int32_t a = seed->cx;
    int32_t b = w - 1 - seed->cx;
    int32_t c = seed->cy;
    int32_t d = h - 1 - seed->cy;
    int32_t m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    if (d > m) m = d;
    return m < 0 ? 0 : m;
}
