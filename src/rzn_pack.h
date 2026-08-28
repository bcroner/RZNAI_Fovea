/* rzn_pack.h -- packing of foveal stereo events into RZNAI_AGI input words.
 *
 * The AGI model (bcroner/RZNAI_AGI) consumes one __int32 per cycle, pushed
 * through AGI_Sys::Input_Queue.  Two of its fields constrain what we may put
 * in a word:
 *
 *   sensory_bits = 1   low bits reserved to identify the sensor -- exactly
 *                      what a two-camera stereo rig needs.
 *   in_sz              perform_iann() decodes only this many bits per word.
 *                      Compile the model with RZNAI_AGI_IN_SZ to match the
 *                      profile chosen here: 32/32 or 16/16.
 *
 * Bit 0 is not ours either.  cycle() keys the Knowledge Bank on (input >> 1)
 * and recovers the source from the low bit; both recall paths set it to 1, so
 * a sensory word must always leave it 0.  The sensor id sits directly above
 * it, which is what `recall_rwdv = (output >> 1) & 0x1` selects.
 *
 * Every layout decision lives in this file; the spiral engine never sees a
 * bit, and the public packer API is identical across profiles.
 *
 * ---------------------------------------------------------------------------
 * TWO PROFILES.  Select with -DRZN_PACK_PROFILE=32 (default) or =16.
 * ---------------------------------------------------------------------------
 *
 * Profile 16 -- fits a model built with in_sz = 16, at a cost in precision:
 * RGB444 colour, and sensors capped at ~4093 px by the 24-bit chunked index.
 * Measured against the real model, RGB444 leaves it effectively stuck on one
 * camera -- see harness/COLOUR_DEPTH.md.
 *
 *   bits 15..4   payload      12 bits
 *   bits 3..2    tag
 *   bit 1        sensor id
 *   bit 0        0            source flag: 0 = sensor, 1 = recall
 *
 * Profile 32 -- DEFAULT.  Lossless RGB888.  Requires the model to be built
 * with RZNAI_AGI_IN_SZ=32, which doubles hidden_sz (= in_sz * In_Q_ct * 2)
 * from 224 to 448 units and costs about 3.8x the compute per cycle.  The two
 * settings MUST match: a profile-32 word decoded at in_sz 16 shows the model
 * part of one colour channel instead of all three.
 *
 *   bit 31       0            kept clear, so words stay non-negative
 *   bits 30..29  tag
 *   bits 28..2   payload      27 bits
 *   bit 1        sensor id
 *   bit 0        0            source flag
 *
 * Payload by tag:
 *   RZN_TAG_INDEX  spiral index.  Profile 32 carries it whole in 27 bits
 *                  (sensors to ~11585 px).  Profile 16 sends it as up to two
 *                  little-endian 12-bit chunks -- 24 bits, sensors to ~4096
 *                  px -- using the sensor bit as a "more chunks follow" flag,
 *                  since an index word belongs to both cameras and has no
 *                  sensor of its own.  rzn_index_more() reads that flag and is
 *                  always false under profile 32, so one reader handles both.
 *   RZN_TAG_LEFT   colour.  Profile 32: RGB888, exact.  Profile 16: RGB444,
 *                  the high nibble of each channel.
 *   RZN_TAG_RIGHT  as above.
 *   RZN_TAG_DISP   disparity + RZN_DISP_BIAS, optional stage only.
 *
 * Index elision: an index word is emitted only when the event's spiral index
 * is not (previous index + 1).  An I-frame scan is contiguous by
 * construction, so this drops the steady-state cost to two words per pixel.
 */
#ifndef RZN_PACK_H
#define RZN_PACK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RZN_PACK_PROFILE
#define RZN_PACK_PROFILE 32
#endif

typedef enum {
    RZN_TAG_INDEX = 0,
    RZN_TAG_LEFT  = 1,
    RZN_TAG_RIGHT = 2,
    RZN_TAG_DISP  = 3
} rzn_word_tag;

/* --- fields shared by both profiles -------------------------------------- */

#define RZN_SOURCE_SENSOR   0        /* bit 0: this word came from a sensor */
#define RZN_SOURCE_RECALL   1        /* the model's own recall words        */

#define RZN_SENSOR_SHIFT    1
#define RZN_SENSOR_BITS     1        /* == AGI_Sys::sensory_bits            */
#define RZN_SENSOR_MASK     ((uint32_t)((1u << RZN_SENSOR_BITS) - 1u))

#define RZN_SENSOR_LEFT     0
#define RZN_SENSOR_RIGHT    1

#define RZN_TAG_BITS        2

/* --- profile-specific field placement ------------------------------------ */

#if RZN_PACK_PROFILE == 32

#define RZN_PACK_WORD_BITS  32
#define RZN_TAG_SHIFT       29
#define RZN_PAYLOAD_SHIFT   2
#define RZN_PAYLOAD_BITS    27
#define RZN_INDEX_CHUNKS    1

#elif RZN_PACK_PROFILE == 16

#define RZN_PACK_WORD_BITS  16
#define RZN_TAG_SHIFT       2
#define RZN_PAYLOAD_SHIFT   4
#define RZN_PAYLOAD_BITS    12
#define RZN_INDEX_CHUNKS    2

#else
#error "RZN_PACK_PROFILE must be 32 or 16"
#endif

#define RZN_PAYLOAD_MASK    ((uint32_t)((1u << RZN_PAYLOAD_BITS) - 1u))

/* Largest spiral index the profile can address. */
#define RZN_MAX_SPIRAL_INDEX \
    ((int64_t)(((uint64_t)1 << (RZN_PAYLOAD_BITS * RZN_INDEX_CHUNKS)) - 1u))

/* Disparity is stored biased so the payload stays unsigned. */
#if RZN_PACK_PROFILE == 32
#define RZN_DISP_BIAS       32768
#else
#define RZN_DISP_BIAS       0        /* a rectified side-by-side rig has d >= 0 */
#endif

typedef struct { uint8_t r, g, b; } rzn_rgb;

/* --- word construction / inspection -------------------------------------- */

static inline int32_t rzn_make_word(rzn_word_tag tag, uint32_t payload,
                                    uint32_t sensor)
{
    uint32_t w = ((uint32_t)tag << RZN_TAG_SHIFT)
               | ((payload & RZN_PAYLOAD_MASK) << RZN_PAYLOAD_SHIFT)
               | ((sensor & RZN_SENSOR_MASK) << RZN_SENSOR_SHIFT)
               | RZN_SOURCE_SENSOR;
    return (int32_t)w;
}

static inline rzn_word_tag rzn_word_kind(int32_t word)
{
    return (rzn_word_tag)(((uint32_t)word >> RZN_TAG_SHIFT)
                          & ((1u << RZN_TAG_BITS) - 1u));
}

static inline uint32_t rzn_word_payload(int32_t word)
{
    return ((uint32_t)word >> RZN_PAYLOAD_SHIFT) & RZN_PAYLOAD_MASK;
}

static inline uint32_t rzn_word_sensor(int32_t word)
{
    return ((uint32_t)word >> RZN_SENSOR_SHIFT) & RZN_SENSOR_MASK;
}

/* 0 = this word came from a sensor, 1 = from recall.  Always 0 here. */
static inline uint32_t rzn_word_source(int32_t word)
{
    return (uint32_t)word & 0x1u;
}

/* For an INDEX word: another chunk of the same index follows.  Always false
 * under profile 32, which carries the index whole. */
static inline bool rzn_index_more(int32_t word)
{
    return rzn_word_sensor(word) != 0u;
}

/* --- colour -------------------------------------------------------------- */

#if RZN_PACK_PROFILE == 32

static inline uint32_t rzn_rgb_payload(rzn_rgb c)
{
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static inline rzn_rgb rzn_payload_rgb(uint32_t p)
{
    rzn_rgb c;
    c.r = (uint8_t)((p >> 16) & 0xFFu);
    c.g = (uint8_t)((p >> 8) & 0xFFu);
    c.b = (uint8_t)(p & 0xFFu);
    return c;
}

#else /* RGB444: keep the high nibble of each channel */

static inline uint32_t rzn_rgb_payload(rzn_rgb c)
{
    return ((uint32_t)(c.r >> 4) << 8)
         | ((uint32_t)(c.g >> 4) << 4)
         |  (uint32_t)(c.b >> 4);
}

static inline rzn_rgb rzn_payload_rgb(uint32_t p)
{
    /* n * 0x11 spreads a nibble back over the full byte range: 0 -> 0,
     * 15 -> 255, evenly spaced. */
    rzn_rgb c;
    c.r = (uint8_t)(((p >> 8) & 0xFu) * 0x11u);
    c.g = (uint8_t)(((p >> 4) & 0xFu) * 0x11u);
    c.b = (uint8_t)((p & 0xFu) * 0x11u);
    return c;
}

#endif

/* The colour a channel value becomes after a round trip through this profile.
 * Exact under profile 32; nibble-quantised under profile 16. */
static inline rzn_rgb rzn_rgb_quantise(rzn_rgb c)
{
    return rzn_payload_rgb(rzn_rgb_payload(c));
}

/* --- sink ---------------------------------------------------------------- */

/* Where packed words go.  Return false to signal back-pressure; the engine
 * stops emitting and reports a short write. */
typedef bool (*rzn_sink_fn)(int32_t word, void *ctx);

/* --- packer -------------------------------------------------------------- */

typedef struct {
    rzn_sink_fn sink;
    void       *ctx;
    int64_t     last_index;      /* spiral index of the previous event */
    bool        have_last;
    bool        elide_index;     /* default true */
    uint64_t    words_out;
    bool        stalled;         /* sink refused a word */
} rzn_packer;

void rzn_packer_init(rzn_packer *p, rzn_sink_fn sink, void *ctx);

/* Forget index continuity.  Call at the start of every I-frame and every
 * delta frame -- a new burst must always re-anchor. */
void rzn_packer_reset(rzn_packer *p);

/* Emit one stereo pixel event at spiral index idx.
 * send_left / send_right select which camera payloads are included; during an
 * I-frame both are true, in the delta path only the cameras that actually
 * changed are sent. */
bool rzn_pack_pixel(rzn_packer *p, int64_t idx,
                    bool send_left,  rzn_rgb left,
                    bool send_right, rzn_rgb right);

/* Emit an optional disparity word for the current pixel. */
bool rzn_pack_disparity(rzn_packer *p, int32_t disparity);

/* Words an index at this position costs, for cost accounting and tests. */
int rzn_index_word_count(int64_t idx);

#ifdef __cplusplus
}
#endif
#endif /* RZN_PACK_H */
