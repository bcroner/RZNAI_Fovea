/* rzn_fovea.c -- foveal spiral stereo input engine. */

#include "rzn_fovea.h"
#include <stdlib.h>
#include <string.h>

/* Words a dense push costs per pixel: one index word cannot be elided for the
 * first pixel, but a raster sweep is contiguous, so steady state is two
 * colour words per pixel. */
#define RZN_DENSE_WORDS_PER_PIXEL 2

int64_t rzn_fovea_baseline_words(const rzn_fovea *f)
{
    return (int64_t)f->w * (int64_t)f->h * RZN_DENSE_WORDS_PER_PIXEL + 1;
}

bool rzn_fovea_init(rzn_fovea *f, int32_t w, int32_t h,
                    int32_t seed_x, int32_t seed_y,
                    rzn_sink_fn sink, void *ctx)
{
    int32_t r_max;

    memset(f, 0, sizeof(*f));

    if (w <= 0 || h <= 0) return false;
    if (seed_x < 0 || seed_y < 0 || seed_x >= w || seed_y >= h) return false;

    f->w = w;
    f->h = h;
    f->seed.cx = seed_x;
    f->seed.cy = seed_y;
    f->change_threshold = 0;
    f->need_intra = true;
    f->have_prev = false;

    /* The scan runs one ring past the last ring that can hold a pixel, so the
     * largest index the packer will ever see is box_cells(r_max + 1) - 1. */
    r_max = rzn_last_ring(&f->seed, w, h);
    if (rzn_box_cells(r_max + 1) - 1 > RZN_MAX_SPIRAL_INDEX) return false;

    if (!rzn_stereo_alloc(&f->prev, w, h)) return false;

    rzn_packer_init(&f->packer, sink, ctx);

#ifdef RZN_ENABLE_DISPARITY
    rzn_disparity_default(&f->disp);
    f->emit_disparity = true;
#endif

    return true;
}

void rzn_fovea_free(rzn_fovea *f)
{
    rzn_stereo_free(&f->prev);
    rzn_events_free(&f->events);
    memset(f, 0, sizeof(*f));
}

void rzn_fovea_request_intra(rzn_fovea *f)
{
    f->need_intra = true;
}

void rzn_fovea_set_seed(rzn_fovea *f, int32_t seed_x, int32_t seed_y)
{
    if (seed_x == f->seed.cx && seed_y == f->seed.cy) return;
    if (seed_x < 0 || seed_y < 0 || seed_x >= f->w || seed_y >= f->h) return;

    f->seed.cx = seed_x;
    f->seed.cy = seed_y;

    /* Default policy: a seed change invalidates the foveal address space, so
     * we re-anchor with a full I-frame.  This is the conservative choice and
     * is trivially correct.
     *
     * The cheaper alternative -- keep the pixel data and re-emit only the
     * index remapping -- is possible because rzn_xy_to_spiral() is O(1), so
     * every already-known pixel's new index is one call away.  It is not
     * enabled here because it changes what the AGI observes across the
     * transition, and that should be measured before it becomes the default. */
    f->need_intra = true;
}

/* --- I-frame ------------------------------------------------------------- */

static void rzn_emit_intra(rzn_fovea *f, const rzn_stereo *cur)
{
    rzn_frame_stats *st = &f->last;
    int32_t r = 0;

    rzn_packer_reset(&f->packer);
    f->packer.words_out = 0;

    st->kind           = RZN_FRAME_INTRA;
    st->cells_visited  = 0;
    st->pixels_emitted = 0;
    st->rings_scanned  = 0;

    for (;;) {
        int64_t start = rzn_ring_start(r);
        int64_t n     = rzn_ring_cells(r);
        int64_t hits  = 0;
        int64_t k;

        for (k = 0; k < n; k++) {
            int64_t idx = start + k;
            int32_t x, y;

            rzn_spiral_to_xy(&f->seed, idx, &x, &y);
            st->cells_visited++;

            if (!rzn_in_bounds(x, y, f->w, f->h))
                continue;          /* outside the sensor: step to the next cell */

            hits++;

            if (!rzn_pack_pixel(&f->packer, idx,
                                true,  rzn_image_get(&cur->left,  x, y),
                                true,  rzn_image_get(&cur->right, x, y)))
                goto done;

            st->pixels_emitted++;

#ifdef RZN_ENABLE_DISPARITY
            if (f->emit_disparity) {
                int32_t d = rzn_disparity_at(cur, &f->disp, x, y);
                if (d != RZN_DISPARITY_NONE && !rzn_pack_disparity(&f->packer, d))
                    goto done;
            }
#endif
        }

        st->rings_scanned = r;

        /* An entire ring with no physically present pixel ends the I-frame.
         * Sound because the rings are nested squares and the sensor is a
         * convex rectangle containing the seed: once a ring clears the
         * sensor, every larger ring does too. */
        if (r > 0 && hits == 0)
            break;

        r++;
    }

done:
    st->words_emitted  = (int64_t)f->packer.words_out;
    st->baseline_words = rzn_fovea_baseline_words(f);
    st->stalled        = f->packer.stalled;
}

/* --- delta --------------------------------------------------------------- */

static int rzn_event_cmp(const void *a, const void *b)
{
    const rzn_event *ea = (const rzn_event *)a;
    const rzn_event *eb = (const rzn_event *)b;
    if (ea->idx < eb->idx) return -1;
    if (ea->idx > eb->idx) return 1;
    return 0;
}

static void rzn_emit_delta(rzn_fovea *f, const rzn_stereo *cur)
{
    rzn_frame_stats *st = &f->last;
    size_t i;

    rzn_packer_reset(&f->packer);
    f->packer.words_out = 0;

    st->kind           = RZN_FRAME_DELTA;
    st->cells_visited  = 0;
    st->pixels_emitted = 0;
    st->rings_scanned  = 0;

    /* Stand-in for the ISP / NPE event stream. */
    rzn_detect_changes(&f->prev, cur, f->change_threshold, &f->events);

    /* Fovea-first ordering: the AGI sees changes nearest the attention point
     * before anything in the periphery.  rzn_xy_to_spiral() is O(1), so this
     * costs one call per event plus the sort -- no search over the image. */
    for (i = 0; i < f->events.count; i++)
        f->events.e[i].idx = rzn_xy_to_spiral(&f->seed,
                                              f->events.e[i].x,
                                              f->events.e[i].y);

    if (f->events.count > 1)
        qsort(f->events.e, f->events.count, sizeof(rzn_event), rzn_event_cmp);

    for (i = 0; i < f->events.count; i++) {
        const rzn_event *ev = &f->events.e[i];

        st->cells_visited++;

        if (!rzn_pack_pixel(&f->packer, ev->idx,
                            ev->left_changed,
                            rzn_image_get(&cur->left,  ev->x, ev->y),
                            ev->right_changed,
                            rzn_image_get(&cur->right, ev->x, ev->y)))
            break;

        st->pixels_emitted++;

#ifdef RZN_ENABLE_DISPARITY
        if (f->emit_disparity) {
            int32_t d = rzn_disparity_at(cur, &f->disp, ev->x, ev->y);
            if (d != RZN_DISPARITY_NONE && !rzn_pack_disparity(&f->packer, d))
                break;
        }
#endif
    }

    st->words_emitted  = (int64_t)f->packer.words_out;
    st->baseline_words = rzn_fovea_baseline_words(f);
    st->stalled        = f->packer.stalled;
}

/* --- driver -------------------------------------------------------------- */

bool rzn_fovea_submit(rzn_fovea *f, const rzn_stereo *cur)
{
    if (!cur || cur->left.w != f->w || cur->left.h != f->h ||
        cur->right.w != f->w || cur->right.h != f->h)
        return false;

    if (f->need_intra || !f->have_prev) {
        rzn_emit_intra(f, cur);
        f->need_intra = false;
    } else {
        rzn_emit_delta(f, cur);
    }

    rzn_stereo_copy(&f->prev, cur);
    f->have_prev = true;

    f->totals.frames++;
    f->totals.words_emitted  += f->last.words_emitted;
    f->totals.baseline_words += f->last.baseline_words;
    f->totals.pixels_emitted += f->last.pixels_emitted;

    return true;
}

const rzn_frame_stats *rzn_fovea_last_stats(const rzn_fovea *f)
{
    return &f->last;
}

const rzn_total_stats *rzn_fovea_totals(const rzn_fovea *f)
{
    return &f->totals;
}
