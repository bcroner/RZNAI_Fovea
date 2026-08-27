/* rzn_spiral.h -- Foveal square-ring spiral address space.
 *
 * RZN AI -- reference implementation.
 *
 * Defines a bijection between a linear "spiral index" and a 2D sensor
 * coordinate, centred on an attention seed (cx, cy) chosen by the robot
 * brain.  Both directions are closed-form O(1); nothing here searches or
 * iterates over the image.
 *
 * Ring r is the set of cells at Chebyshev distance r from the seed:
 *
 *     ring 0 : 1 cell   (the seed itself)
 *     ring r : 8*r cells                       (r >= 1)
 *     rings 0..r-1 together : (2r-1)^2 cells
 *
 * Note that the ring perimeter grows arithmetically (1, 8, 16, 24, 32, ...),
 * not geometrically.  The enclosing box at ring r is (2r+1)^2 cells.
 *
 * Traversal order within a ring, matching the specified scan procedure
 * ("extend outward on +X by one, then proceed down"):
 *
 *     enter at (cx+r, cy-r+1)   <- one step +X from the previous ring's exit
 *     DOWN   the right edge  to (cx+r, cy+r)      2r cells (incl. entry)
 *     LEFT   the bottom edge to (cx-r, cy+r)      2r cells
 *     UP     the left edge   to (cx-r, cy-r)      2r cells
 *     RIGHT  the top edge    to (cx+r, cy-r)      2r cells
 *                                                 -------
 *                                                 8r cells
 *
 * The ring exits at (cx+r, cy-r); one more step +X enters ring r+1 at
 * (cx+r+1, cy-r), so the whole scan is a single unbroken path.
 */
#ifndef RZN_SPIRAL_H
#define RZN_SPIRAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Attention seed: the fixation point the robot brain nominates. */
typedef struct {
    int32_t cx;
    int32_t cy;
} rzn_seed;

/* Number of cells on ring r. */
static inline int64_t rzn_ring_cells(int32_t r)
{
    return (r == 0) ? 1 : (int64_t)8 * r;
}

/* Spiral index of the first cell of ring r (== total cells in rings 0..r-1). */
static inline int64_t rzn_ring_start(int32_t r)
{
    int64_t k = (int64_t)2 * r - 1;
    return (r == 0) ? 0 : k * k;
}

/* Total cells enclosed by rings 0..r inclusive. */
static inline int64_t rzn_box_cells(int32_t r)
{
    int64_t k = (int64_t)2 * r + 1;
    return k * k;
}

/* spiral index -> sensor coordinate.  O(1). */
void rzn_spiral_to_xy(const rzn_seed *seed, int64_t idx, int32_t *x, int32_t *y);

/* sensor coordinate -> spiral index.  O(1).  Exact inverse of the above. */
int64_t rzn_xy_to_spiral(const rzn_seed *seed, int32_t x, int32_t y);

/* Ring containing a spiral index (Chebyshev radius).  O(1). */
int32_t rzn_spiral_ring(int64_t idx);

/* The last ring that can still contain a sensor pixel, for a seed inside a
 * w x h sensor.  Ring rzn_last_ring()+1 is guaranteed empty, which is what
 * terminates the I-frame scan.
 *
 * Because the rings are nested squares and the sensor is a convex rectangle
 * containing the seed, the first ring with zero hits guarantees every larger
 * ring also has zero hits -- so "scan until an empty ring" is sound. */
int32_t rzn_last_ring(const rzn_seed *seed, int32_t w, int32_t h);

/* True if (x, y) lands on a physically present sensor pixel. */
static inline bool rzn_in_bounds(int32_t x, int32_t y, int32_t w, int32_t h)
{
    return x >= 0 && y >= 0 && x < w && y < h;
}

#ifdef __cplusplus
}
#endif
#endif /* RZN_SPIRAL_H */
