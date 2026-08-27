/* rzn_pack.c -- RZNAI_AGI input-word packing. */

#include "rzn_pack.h"

void rzn_packer_init(rzn_packer *p, rzn_sink_fn sink, void *ctx)
{
    p->sink        = sink;
    p->ctx         = ctx;
    p->last_index  = -1;
    p->have_last   = false;
    p->elide_index = true;
    p->words_out   = 0;
    p->stalled     = false;
}

void rzn_packer_reset(rzn_packer *p)
{
    p->last_index = -1;
    p->have_last  = false;
    p->stalled    = false;
}

static bool rzn_emit(rzn_packer *p, int32_t word)
{
    if (p->stalled) return false;
    if (!p->sink(word, p->ctx)) {
        p->stalled = true;
        return false;
    }
    p->words_out++;
    return true;
}

/* Split an index into little-endian payload-sized chunks.  One chunk under
 * profile 32; up to RZN_INDEX_CHUNKS under profile 16. */
static int rzn_index_chunks(int64_t idx, uint32_t *out)
{
    uint64_t v = (uint64_t)idx;
    int n = 0;
    do {
        out[n++] = (uint32_t)(v & RZN_PAYLOAD_MASK);
        v >>= RZN_PAYLOAD_BITS;
    } while (v != 0 && n < RZN_INDEX_CHUNKS);
    return n;
}

int rzn_index_word_count(int64_t idx)
{
    uint32_t chunks[RZN_INDEX_CHUNKS];
    if (idx < 0 || idx > RZN_MAX_SPIRAL_INDEX) return 0;
    return rzn_index_chunks(idx, chunks);
}

static bool rzn_emit_index(rzn_packer *p, int64_t idx)
{
    uint32_t chunks[RZN_INDEX_CHUNKS];
    int n = rzn_index_chunks(idx, chunks);
    int i;

    for (i = 0; i < n; i++) {
        /* An index word has no camera of its own, so the sensor bit carries
         * the continuation flag instead. */
        uint32_t more = (i + 1 < n) ? 1u : 0u;
        if (!rzn_emit(p, rzn_make_word(RZN_TAG_INDEX, chunks[i], more)))
            return false;
    }
    return true;
}

bool rzn_pack_pixel(rzn_packer *p, int64_t idx,
                    bool send_left,  rzn_rgb left,
                    bool send_right, rzn_rgb right)
{
    bool contiguous;

    if (idx < 0 || idx > RZN_MAX_SPIRAL_INDEX) return false;

    contiguous = p->elide_index && p->have_last && (idx == p->last_index + 1);

    if (!contiguous) {
        if (!rzn_emit_index(p, idx))
            return false;
    }

    /* The index is now anchored; record it even if a colour word later
     * stalls, so a resumed stream stays consistent. */
    p->last_index = idx;
    p->have_last  = true;

    if (send_left) {
        if (!rzn_emit(p, rzn_make_word(RZN_TAG_LEFT, rzn_rgb_payload(left),
                                       RZN_SENSOR_LEFT)))
            return false;
    }
    if (send_right) {
        if (!rzn_emit(p, rzn_make_word(RZN_TAG_RIGHT, rzn_rgb_payload(right),
                                       RZN_SENSOR_RIGHT)))
            return false;
    }
    return true;
}

bool rzn_pack_disparity(rzn_packer *p, int32_t disparity)
{
    int32_t d = disparity + RZN_DISP_BIAS;
    if (d < 0) d = 0;
    if ((uint32_t)d > RZN_PAYLOAD_MASK) d = (int32_t)RZN_PAYLOAD_MASK;
    return rzn_emit(p, rzn_make_word(RZN_TAG_DISP, (uint32_t)d,
                                     RZN_SENSOR_LEFT));
}
