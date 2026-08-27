/* rzn_agi_bridge.h -- foveal stereo source behind RZNAI_AGI's in_0 / in_1.
 *
 * The model pulls one sensor reading per cycle through in_0() / in_1(), and
 * read_sensory() wraps whatever they return into the final input word:
 *
 *     word = (reading << sensory_bits | sensor_id) << 1 | source_flag
 *
 * With sensory_bits = 1 that is `reading << 2 | sensor << 1 | 0`, which is
 * exactly the low end of a profile-16 packed word.  So a reading is simply
 * one of this engine's words with its sensor and source bits removed:
 *
 *     reading = word >> 2          (RZN_PAYLOAD_SHIFT bits)
 *
 * carrying the payload and the tag, and read_sensory() rebuilds the rest.
 *
 * One tension is worth stating plainly.  The model chooses which sensor to
 * read next from its own output (`sensor = (output >> 1) & sensor_mask`), but
 * a foveal stream is inherently sequential -- an index word belongs to both
 * cameras, and a delta burst names the camera that actually changed.  So the
 * bridge serves the next word in the stream regardless of which sensor was
 * asked for, and the word's own tag stays authoritative.  The sensor bit
 * read_sensory() writes is the model's request, not the camera the value came
 * from.  rzn_bridge_stats reports how often the two disagree, so the size of
 * the problem is measured rather than assumed.
 */
#ifndef RZN_AGI_BRIDGE_H
#define RZN_AGI_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include "rzn_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t readings;        /* in_0 / in_1 calls served */
    uint64_t index_words;
    uint64_t left_words;
    uint64_t right_words;
    uint64_t disp_words;
    uint64_t asked_left;      /* the model requested sensor 0 */
    uint64_t asked_right;     /* the model requested sensor 1 */
    uint64_t sensor_mismatch; /* request disagreed with the word's own tag */
    uint64_t frames_pushed;
    uint64_t underruns;       /* stream dry and no frame available */
} rzn_bridge_stats;

/* Open a synthetic-scene source of the given size, seeded at (sx, sy).
 * Frames are rendered on demand as the model drains the stream. */
bool rzn_bridge_open(int32_t w, int32_t h, int32_t sx, int32_t sy);
void rzn_bridge_close(void);

/* Serve one reading for the requested sensor.  This is what in_0 / in_1 call.
 * Returns 0 on underrun. */
int32_t rzn_bridge_read(int32_t sensor);

const rzn_bridge_stats *rzn_bridge_get_stats(void);

/* Frames rendered so far, and the engine behind the bridge, for reporting. */
int32_t rzn_bridge_frame_count(void);
int64_t rzn_bridge_words_emitted(void);

#ifdef __cplusplus
}
#endif
#endif /* RZN_AGI_BRIDGE_H */
