/* rzn_agi_sink.h -- bridge from the foveal engine to RZNAI_AGI's Input_Queue.
 *
 * The AGI model consumes one __int32 per cycle.  This sink buffers packed
 * words and hands them over one at a time, so the engine can emit a whole
 * frame's worth of events without being coupled to the AGI's cycle rate.
 *
 * Building against the real model:
 *
 *     cl /DRZN_HAVE_RZNAI_AGI /I<path-to-RZNAI_AGI> ...
 *
 * Without that define this compiles standalone and simply counts words, which
 * is what the demo and the tests use.
 */
#ifndef RZN_AGI_SINK_H
#define RZN_AGI_SINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rzn_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t *buf;
    size_t   cap;
    size_t   head;     /* next word to hand to the AGI */
    size_t   tail;     /* next free slot */
    uint64_t accepted;
    uint64_t dropped;  /* words refused because the buffer was full */
} rzn_agi_sink;

bool rzn_agi_sink_init(rzn_agi_sink *s, size_t capacity_words);
void rzn_agi_sink_free(rzn_agi_sink *s);

/* rzn_sink_fn.  Returns false when full, which the packer treats as
 * back-pressure and reports through rzn_frame_stats.stalled. */
bool rzn_agi_sink_push(int32_t word, void *ctx);

size_t rzn_agi_sink_pending(const rzn_agi_sink *s);

/* Pop the next word for the AGI.  Returns false when empty. */
bool rzn_agi_sink_pop(rzn_agi_sink *s, int32_t *word);

/* Drain everything currently buffered into the AGI's Input_Queue, one word
 * per cycle.  Compiled out unless RZN_HAVE_RZNAI_AGI is defined. */
#ifdef RZN_HAVE_RZNAI_AGI
struct AGI_Sys_tag;
size_t rzn_agi_sink_drain(rzn_agi_sink *s, struct AGI_Sys_tag *stm);
#endif

/* Reference for the corrected sensory read.
 *
 * RZNAI_AGI's read_sensory() masks with 0x0 twice, zeroing everything it
 * assembles.  The layout the rest of the file agrees on -- and the one
 * rzn_pack.h produces -- is:
 *
 *     word = ((payload << sensory_bits) | sensor_id) << 1 | source_flag
 *
 * with source_flag 0 for a sensor reading and 1 for a recall reading.  Both
 * recall paths already build their word this way; cycle() keys the Knowledge
 * Bank on (input >> 1) and reads the source back out of bit 0.
 *
 * See ../patches/0001-fix-input-word-assembly.patch. */
#define RZN_SOURCE_FLAG_SENSOR 0
#define RZN_SOURCE_FLAG_RECALL 1

static inline int32_t rzn_sensory_word(int32_t payload, int32_t sensor,
                                       int32_t sensory_bits)
{
    uint32_t mask = (sensory_bits >= 32)
                  ? 0xFFFFFFFFu
                  : (uint32_t)((1u << sensory_bits) - 1u);
    uint32_t w = ((uint32_t)payload << sensory_bits)
               | ((uint32_t)sensor & mask);
    return (int32_t)((w << 1) | RZN_SOURCE_FLAG_SENSOR);
}

#ifdef __cplusplus
}
#endif
#endif /* RZN_AGI_SINK_H */
