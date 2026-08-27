/* rzn_agi_sink.c -- ring buffer between the foveal engine and the AGI. */

#include "rzn_agi_sink.h"
#include <stdlib.h>
#include <string.h>

#ifdef RZN_HAVE_RZNAI_AGI
#include "RZNAI_AGI.hpp"
#endif

bool rzn_agi_sink_init(rzn_agi_sink *s, size_t capacity_words)
{
    memset(s, 0, sizeof(*s));
    if (capacity_words < 2) return false;
    s->buf = (int32_t *)malloc(capacity_words * sizeof(int32_t));
    if (!s->buf) return false;
    s->cap = capacity_words;
    return true;
}

void rzn_agi_sink_free(rzn_agi_sink *s)
{
    free(s->buf);
    memset(s, 0, sizeof(*s));
}

size_t rzn_agi_sink_pending(const rzn_agi_sink *s)
{
    return (s->tail + s->cap - s->head) % s->cap;
}

bool rzn_agi_sink_push(int32_t word, void *ctx)
{
    rzn_agi_sink *s = (rzn_agi_sink *)ctx;
    size_t next = (s->tail + 1u) % s->cap;

    if (next == s->head) {          /* full: one slot always left empty */
        s->dropped++;
        return false;
    }
    s->buf[s->tail] = word;
    s->tail = next;
    s->accepted++;
    return true;
}

bool rzn_agi_sink_pop(rzn_agi_sink *s, int32_t *word)
{
    if (s->head == s->tail) return false;
    *word = s->buf[s->head];
    s->head = (s->head + 1u) % s->cap;
    return true;
}

#ifdef RZN_HAVE_RZNAI_AGI
size_t rzn_agi_sink_drain(rzn_agi_sink *s, struct AGI_Sys_tag *stm)
{
    size_t n = 0;
    int32_t word;
    while (rzn_agi_sink_pop(s, &word)) {
        stm->Current_Input = word;
        cycle(stm);             /* shifts Input_Queue and advances the model */
        n++;
    }
    return n;
}
#endif
