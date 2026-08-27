/* rzn_fovea.h -- RZN AI foveal stereo input engine.
 *
 * Reference implementation of the foveal spiral stereo input path for robots
 * running the RZN AI AGI model (Festivitly, Grab A Bot).
 *
 * Two cameras of identical resolution, rigidly mounted coplanar and side by
 * side, share one retinal coordinate frame.  The robot brain nominates an
 * attention seed (X, Y).  From that seed the engine walks an expanding
 * square-ring spiral; at every cell that lands on a physically present pixel
 * it feeds the AGI the colour values from BOTH cameras at that one address.
 * Cells that fall outside the sensor are skipped.  When an entire ring
 * produces no physically present pixel, the initial I-frame is complete.
 *
 * Thereafter the engine runs event-driven: an ISP or Neural Processing Engine
 * reports which pixels changed, and only those addresses are re-fed, ordered
 * fovea-first by spiral index.  That sparsity is where the throughput win
 * comes from, and it is scene-dependent -- rzn_fovea_stats reports the
 * measured ratio per frame and cumulatively so it can be characterised
 * honestly rather than assumed.
 *
 * Two properties worth stating plainly:
 *
 *   1. The spiral address is attention-relative and resolution-independent.
 *      The same object at the same offset from fixation lands on the same
 *      spiral index no matter where on the sensor it sits.
 *
 *   2. Feeding both cameras at one retinal address makes disparity implicit,
 *      not computed.  The AGI must learn correspondence.  The optional
 *      disparity stage (RZN_ENABLE_DISPARITY) exists only so the alternative
 *      can be measured.
 */
#ifndef RZN_FOVEA_H
#define RZN_FOVEA_H

#include <stdint.h>
#include <stdbool.h>
#include "rzn_spiral.h"
#include "rzn_pack.h"
#include "rzn_frame.h"

#ifdef RZN_ENABLE_DISPARITY
#include "rzn_disparity.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RZN_FRAME_NONE   = 0,
    RZN_FRAME_INTRA  = 1,   /* full foveal spiral scan */
    RZN_FRAME_DELTA  = 2    /* changed pixels only */
} rzn_frame_kind;

typedef struct {
    rzn_frame_kind kind;
    int64_t cells_visited;    /* spiral cells stepped over, hits + misses */
    int64_t pixels_emitted;   /* addresses actually fed to the AGI */
    int64_t words_emitted;    /* __int32 words handed to the sink */
    int32_t rings_scanned;    /* I-frame only */
    int64_t baseline_words;   /* what a dense full-frame push would have cost */
    bool    stalled;          /* sink applied back-pressure */
} rzn_frame_stats;

typedef struct {
    int64_t frames;
    int64_t words_emitted;
    int64_t baseline_words;
    int64_t pixels_emitted;
} rzn_total_stats;

typedef struct {
    int32_t        w, h;
    rzn_seed       seed;
    int32_t        change_threshold;   /* per-channel, 0 = any difference */
    bool           need_intra;         /* set on init and on seed change */

    rzn_packer     packer;
    rzn_stereo     prev;
    bool           have_prev;
    rzn_event_list events;

#ifdef RZN_ENABLE_DISPARITY
    rzn_disparity_cfg disp;
    bool              emit_disparity;
#endif

    rzn_frame_stats last;
    rzn_total_stats totals;
} rzn_fovea;

/* Allocate and configure.  Fails if the sensor is too large for the 28-bit
 * spiral index payload, or on allocation failure. */
bool rzn_fovea_init(rzn_fovea *f, int32_t w, int32_t h,
                    int32_t seed_x, int32_t seed_y,
                    rzn_sink_fn sink, void *ctx);

void rzn_fovea_free(rzn_fovea *f);

/* Move the attention seed.  Per the reference design this invalidates the
 * existing foveal state and forces a fresh I-frame on the next submit; the
 * cheaper re-index path is left for a later revision (see the note in
 * rzn_fovea.c).  A no-op if the seed is unchanged. */
void rzn_fovea_set_seed(rzn_fovea *f, int32_t seed_x, int32_t seed_y);

/* Force the next submit to produce an I-frame. */
void rzn_fovea_request_intra(rzn_fovea *f);

/* Feed one stereo frame.  Produces an I-frame if one is pending, otherwise a
 * delta against the previous frame.  Returns false only on a hard error;
 * sink back-pressure is reported via stats.stalled. */
bool rzn_fovea_submit(rzn_fovea *f, const rzn_stereo *cur);

const rzn_frame_stats *rzn_fovea_last_stats(const rzn_fovea *f);
const rzn_total_stats *rzn_fovea_totals(const rzn_fovea *f);

/* Words a dense, non-foveal push of one full stereo frame would cost, under
 * the same packing.  The comparison baseline for the sparsity ratio. */
int64_t rzn_fovea_baseline_words(const rzn_fovea *f);

#ifdef __cplusplus
}
#endif
#endif /* RZN_FOVEA_H */
