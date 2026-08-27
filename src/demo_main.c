/* demo_main.c -- drives the foveal stereo engine and measures what it costs.
 *
 * Usage:
 *   rzn_demo [-w W] [-h H] [-x SEED_X] [-y SEED_Y] [-n FRAMES] [-t THRESHOLD]
 *            [-l LEFT.ppm -r RIGHT.ppm]   feed a real stereo pair instead
 *            [-d]                          dump frame 0 as PPM pairs
 *
 * With no image arguments it renders a synthetic rectified stereo scene with a
 * moving foreground object, which is enough to characterise the sparsity of
 * the delta path without hardware.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rzn_fovea.h"
#include "rzn_agi_sink.h"

static void usage(void)
{
    printf("usage: rzn_demo [-w W] [-h H] [-x X] [-y Y] [-n FRAMES] "
           "[-t THRESHOLD] [-l LEFT.ppm -r RIGHT.ppm] [-d]\n");
}

int main(int argc, char **argv)
{
    int32_t W = 320, H = 240;
    int32_t sx = -1, sy = -1;
    int32_t frames = 24;
    int32_t threshold = 0;
    const char *left_path = NULL, *right_path = NULL;
    int dump = 0;
    int i;

    rzn_fovea    f;
    rzn_agi_sink sink;
    rzn_stereo   cur;
    rzn_scene    sc;
    int64_t      total_pixels = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w") && i + 1 < argc) W = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i + 1 < argc) H = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-x") && i + 1 < argc) sx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-y") && i + 1 < argc) sy = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) threshold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) left_path = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) right_path = argv[++i];
        else if (!strcmp(argv[i], "-d")) dump = 1;
        else { usage(); return 2; }
    }

    if (left_path || right_path) {
        if (!left_path || !right_path) {
            printf("error: -l and -r must be given together\n");
            return 2;
        }
        if (!rzn_image_load_ppm(&cur.left, left_path)) {
            printf("error: cannot read %s (binary P6 PPM, maxval 255)\n", left_path);
            return 1;
        }
        if (!rzn_image_load_ppm(&cur.right, right_path)) {
            printf("error: cannot read %s\n", right_path);
            return 1;
        }
        if (cur.left.w != cur.right.w || cur.left.h != cur.right.h) {
            printf("error: the two cameras must have identical resolution "
                   "(%dx%d vs %dx%d)\n",
                   cur.left.w, cur.left.h, cur.right.w, cur.right.h);
            return 1;
        }
        W = cur.left.w;
        H = cur.left.h;
        frames = 1;
    } else {
        if (!rzn_stereo_alloc(&cur, W, H)) { printf("error: out of memory\n"); return 1; }
        rzn_scene_init(&sc, W, H);
    }

    if (sx < 0) sx = W / 2;
    if (sy < 0) sy = H / 2;

    if (!rzn_agi_sink_init(&sink, (size_t)W * (size_t)H * 4u + 16u)) {
        printf("error: cannot allocate the AGI input buffer\n");
        return 1;
    }
    if (!rzn_fovea_init(&f, W, H, sx, sy, rzn_agi_sink_push, &sink)) {
        printf("error: engine init failed -- bad seed, or the sensor needs a "
               "spiral index wider than this profile's %d bits\n",
               RZN_PAYLOAD_BITS * RZN_INDEX_CHUNKS);
        return 1;
    }
    f.change_threshold = threshold;

    printf("RZN AI foveal stereo input engine\n");
    printf("  sensor      %d x %d  (two cameras, identical resolution)\n", W, H);
    printf("  seed        (%d, %d)\n", sx, sy);
    printf("  last ring   %d\n", rzn_last_ring(&f.seed, W, H));
    printf("  threshold   %d\n", threshold);
    printf("  packing     profile %d -- %d-bit payload, %s colour, "
           "index in up to %d word%s\n",
           RZN_PACK_PROFILE, RZN_PAYLOAD_BITS,
           RZN_PACK_PROFILE == 32 ? "RGB888" : "RGB444",
           RZN_INDEX_CHUNKS, RZN_INDEX_CHUNKS == 1 ? "" : "s");
#ifdef RZN_ENABLE_DISPARITY
    printf("  disparity   ON  (search %d..%d px, %dx%d window)\n",
           f.disp.min_disparity, f.disp.max_disparity,
           f.disp.window * 2 + 1, f.disp.window * 2 + 1);
#else
    printf("  disparity   off (built without RZN_ENABLE_DISPARITY)\n");
#endif
    printf("\n%-6s %-6s %10s %10s %10s %12s\n",
           "frame", "kind", "pixels", "words", "baseline", "vs dense");
    printf("------------------------------------------------------------------\n");

    for (i = 0; i < frames; i++) {
        const rzn_frame_stats *st;
        double ratio;

        if (!left_path) {
            rzn_scene_step(&sc, i);
            rzn_scene_render(&sc, &cur);
        }

        if (dump && i == 0) {
            rzn_image_save_ppm(&cur.left,  "frame0_left.ppm");
            rzn_image_save_ppm(&cur.right, "frame0_right.ppm");
        }

        if (!rzn_fovea_submit(&f, &cur)) {
            printf("error: submit failed on frame %d\n", i);
            return 1;
        }

        /* The AGI would consume these one per cycle; here we just drain. */
        {
            int32_t w;
            while (rzn_agi_sink_pop(&sink, &w)) { /* feed AGI_Sys::Current_Input */ }
        }

        st = rzn_fovea_last_stats(&f);
        ratio = st->words_emitted
              ? (double)st->baseline_words / (double)st->words_emitted
              : 0.0;
        total_pixels += st->pixels_emitted;

        printf("%-6d %-6s %10lld %10lld %10lld %11.1fx%s\n",
               i,
               st->kind == RZN_FRAME_INTRA ? "I" : "delta",
               (long long)st->pixels_emitted,
               (long long)st->words_emitted,
               (long long)st->baseline_words,
               ratio,
               st->stalled ? "  (STALLED)" : "");
    }

    {
        const rzn_total_stats *t = rzn_fovea_totals(&f);
        double ratio = t->words_emitted
                     ? (double)t->baseline_words / (double)t->words_emitted
                     : 0.0;
        printf("------------------------------------------------------------------\n");
        printf("%lld frames: %lld words emitted vs %lld dense -- %.1fx overall\n",
               (long long)t->frames, (long long)t->words_emitted,
               (long long)t->baseline_words, ratio);
        printf("%lld pixel addresses fed to the AGI (%lld dense equivalent)\n",
               (long long)total_pixels, (long long)t->frames * W * H);
        if (t->frames > 1) {
            const rzn_frame_stats *st = rzn_fovea_last_stats(&f);
            (void)st;
            printf("\nThe ratio is scene-dependent by construction: a still scene\n"
                   "approaches zero cost, a full-field pan approaches dense cost.\n"
                   "Quote it against a named workload, not in the abstract.\n");
        }
    }

    rzn_fovea_free(&f);
    rzn_agi_sink_free(&sink);
    rzn_stereo_free(&cur);
    return 0;
}
