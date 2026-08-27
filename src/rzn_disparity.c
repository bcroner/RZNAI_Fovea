/* rzn_disparity.c -- 1-D SAD block match along the epipolar scanline. */

#include "rzn_disparity.h"
#include <limits.h>

void rzn_disparity_default(rzn_disparity_cfg *cfg)
{
    cfg->min_disparity = 0;
    cfg->max_disparity = 48;
    cfg->window        = 3;      /* 7 x 7 support */
}

static int32_t rzn_sad(const rzn_stereo *s, int32_t x, int32_t y,
                       int32_t d, int32_t win)
{
    int32_t sum = 0, dy, dx;
    for (dy = -win; dy <= win; dy++) {
        int32_t yy = y + dy;
        if (yy < 0 || yy >= s->left.h) return INT32_MAX;
        for (dx = -win; dx <= win; dx++) {
            int32_t lx = x + dx;
            int32_t rx = x + dx - d;
            rzn_rgb a, b;
            int32_t e0, e1, e2;
            if (lx < 0 || lx >= s->left.w) return INT32_MAX;
            if (rx < 0 || rx >= s->right.w) return INT32_MAX;
            a = rzn_image_get(&s->left,  lx, yy);
            b = rzn_image_get(&s->right, rx, yy);
            e0 = (int32_t)a.r - (int32_t)b.r;
            e1 = (int32_t)a.g - (int32_t)b.g;
            e2 = (int32_t)a.b - (int32_t)b.b;
            sum += (e0 < 0 ? -e0 : e0) + (e1 < 0 ? -e1 : e1)
                 + (e2 < 0 ? -e2 : e2);
        }
    }
    return sum;
}

int32_t rzn_disparity_at(const rzn_stereo *s, const rzn_disparity_cfg *cfg,
                         int32_t x, int32_t y)
{
    int32_t best_d = RZN_DISPARITY_NONE;
    int32_t best_c = INT32_MAX;
    int32_t d;

    for (d = cfg->min_disparity; d <= cfg->max_disparity; d++) {
        int32_t c = rzn_sad(s, x, y, d, cfg->window);
        if (c == INT32_MAX) continue;
        if (c < best_c) { best_c = c; best_d = d; }
    }
    return best_d;
}
