/* rzn_disparity.h -- optional epipolar disparity stage.
 *
 * OFF BY DEFAULT.  Compile with -DRZN_ENABLE_DISPARITY=1 to include it.
 *
 * The default design is deliberately "pure": the engine hands the AGI the raw
 * binocular pair at each retinal address and lets correspondence be learned.
 * This module exists so the two can be measured against each other, not
 * because depth needs to be precomputed.
 *
 * Because the rig is coplanar and side-by-side, the images are rectified by
 * construction: a surface point at left-camera (x, y) appears in the right
 * camera at (x - d, y) for some d >= 0.  Correspondence therefore never
 * leaves the scanline, which makes the search a 1-D SAD over a small window.
 */
#ifndef RZN_DISPARITY_H
#define RZN_DISPARITY_H

#include <stdint.h>
#include "rzn_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t min_disparity;   /* inclusive, px */
    int32_t max_disparity;   /* inclusive, px */
    int32_t window;          /* half-width of the SAD window, px */
} rzn_disparity_cfg;

void rzn_disparity_default(rzn_disparity_cfg *cfg);

/* Best-matching disparity for left-camera pixel (x, y), or
 * RZN_DISPARITY_NONE when no candidate is usable (e.g. too close to an edge). */
#define RZN_DISPARITY_NONE INT32_MIN

int32_t rzn_disparity_at(const rzn_stereo *s, const rzn_disparity_cfg *cfg,
                         int32_t x, int32_t y);

#ifdef __cplusplus
}
#endif
#endif /* RZN_DISPARITY_H */
