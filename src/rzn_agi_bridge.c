/* rzn_agi_bridge.c -- foveal stereo source behind RZNAI_AGI's in_0 / in_1. */

#include "rzn_agi_bridge.h"
#include "rzn_fovea.h"
#include "rzn_agi_sink.h"
#include <string.h>

static struct {
    bool             open;
    rzn_fovea        fovea;
    rzn_agi_sink     sink;
    rzn_stereo       frame;
    rzn_scene        scene;
    int32_t          frame_no;
    rzn_bridge_stats stats;
} g;

bool rzn_bridge_open(int32_t w, int32_t h, int32_t sx, int32_t sy)
{
    rzn_bridge_close();
    memset(&g, 0, sizeof(g));

    if (!rzn_stereo_alloc(&g.frame, w, h))
        return false;

    /* One frame of foveal output is at most a few words per pixel; size the
     * ring generously so a whole I-frame fits without back-pressure. */
    if (!rzn_agi_sink_init(&g.sink, (size_t)w * (size_t)h * 4u + 64u)) {
        rzn_stereo_free(&g.frame);
        return false;
    }

    if (!rzn_fovea_init(&g.fovea, w, h, sx, sy, rzn_agi_sink_push, &g.sink)) {
        rzn_agi_sink_free(&g.sink);
        rzn_stereo_free(&g.frame);
        return false;
    }

    rzn_scene_init(&g.scene, w, h);
    g.frame_no = 0;
    g.open = true;
    return true;
}

void rzn_bridge_close(void)
{
    if (!g.open) return;
    rzn_fovea_free(&g.fovea);
    rzn_agi_sink_free(&g.sink);
    rzn_stereo_free(&g.frame);
    g.open = false;
}

/* Render and submit the next frame when the model has drained the stream. */
static void rzn_bridge_refill(void)
{
    rzn_scene_step(&g.scene, g.frame_no);
    rzn_scene_render(&g.scene, &g.frame);
    if (rzn_fovea_submit(&g.fovea, &g.frame)) {
        g.frame_no++;
        g.stats.frames_pushed++;
    }
}

int32_t rzn_bridge_read(int32_t sensor)
{
    int32_t word;
    rzn_word_tag tag;
    uint32_t word_sensor;

    if (!g.open) return 0;

    if (sensor == RZN_SENSOR_RIGHT) g.stats.asked_right++;
    else                            g.stats.asked_left++;

    if (rzn_agi_sink_pending(&g.sink) == 0) {
        rzn_bridge_refill();

        /* A frame with nothing changed legitimately produces no words.  Keep
         * stepping the scene rather than reporting a false underrun. */
        {
            int guard = 0;
            while (rzn_agi_sink_pending(&g.sink) == 0 && guard++ < 64)
                rzn_bridge_refill();
        }

        if (rzn_agi_sink_pending(&g.sink) == 0) {
            g.stats.underruns++;
            return 0;
        }
    }

    if (!rzn_agi_sink_pop(&g.sink, &word)) {
        g.stats.underruns++;
        return 0;
    }

    g.stats.readings++;

    tag = rzn_word_kind(word);
    switch (tag) {
    case RZN_TAG_INDEX: g.stats.index_words++; break;
    case RZN_TAG_LEFT:  g.stats.left_words++;  break;
    case RZN_TAG_RIGHT: g.stats.right_words++; break;
    case RZN_TAG_DISP:  g.stats.disp_words++;  break;
    }

    /* An index word carries a continuation flag in the sensor bit rather than
     * a camera, so it cannot disagree with the request. */
    if (tag == RZN_TAG_LEFT || tag == RZN_TAG_RIGHT) {
        word_sensor = rzn_word_sensor(word);
        if ((int32_t)word_sensor != sensor)
            g.stats.sensor_mismatch++;
    }

    /* Hand back the word minus its sensor and source bits; read_sensory()
     * puts those back on. */
    return (int32_t)((uint32_t)word >> RZN_PAYLOAD_SHIFT);
}

const rzn_bridge_stats *rzn_bridge_get_stats(void)
{
    return &g.stats;
}

int32_t rzn_bridge_frame_count(void)
{
    return g.frame_no;
}

int64_t rzn_bridge_words_emitted(void)
{
    return g.open ? (int64_t)g.fovea.totals.words_emitted : 0;
}
