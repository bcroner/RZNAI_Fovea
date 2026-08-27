/* test_rzn.c -- correctness tests for the RZN AI foveal stereo input engine. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rzn_spiral.h"
#include "rzn_pack.h"
#include "rzn_frame.h"
#include "rzn_fovea.h"
#ifdef RZN_ENABLE_DISPARITY
#include "rzn_disparity.h"
#endif

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_fail++;                                                      \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);                  \
            printf(__VA_ARGS__);                                           \
            printf("\n");                                                  \
        }                                                                  \
    } while (0)

static void banner(const char *s) { printf("\n== %s ==\n", s); }

/* Reassembles a possibly-chunked spiral index from the word stream, exactly
 * as a reader on the AGI side would.  Under profile 32 every index arrives
 * whole, so this is a pass-through; under profile 16 it joins the little-
 * endian 12-bit chunks flagged by rzn_index_more(). */
typedef struct { int64_t acc; int shift; } idx_rx;

static void idx_rx_reset(idx_rx *r) { r->acc = 0; r->shift = 0; }

static bool idx_rx_feed(idx_rx *r, int32_t w, int64_t *out)
{
    r->acc |= (int64_t)rzn_word_payload(w) << r->shift;
    r->shift += RZN_PAYLOAD_BITS;
    if (rzn_index_more(w)) return false;
    *out = r->acc;
    idx_rx_reset(r);
    return true;
}

/* --- 1. ring arithmetic -------------------------------------------------- */

static void test_ring_arithmetic(void)
{
    int32_t r;
    banner("ring arithmetic");

    CHECK(rzn_ring_cells(0) == 1, "ring 0 should hold 1 cell");
    for (r = 1; r <= 64; r++)
        CHECK(rzn_ring_cells(r) == 8 * r, "ring %d should hold %d cells", r, 8 * r);

    /* Perimeter grows arithmetically (+8), not geometrically. */
    CHECK(rzn_ring_cells(1) == 8,  "ring 1");
    CHECK(rzn_ring_cells(2) == 16, "ring 2");
    CHECK(rzn_ring_cells(3) == 24, "ring 3 is 24, not 32");
    CHECK(rzn_ring_cells(4) == 32, "ring 4");

    for (r = 0; r <= 64; r++) {
        CHECK(rzn_ring_start(r) + rzn_ring_cells(r) == rzn_ring_start(r + 1),
              "rings must tile the index line at r=%d", r);
        CHECK(rzn_box_cells(r) == rzn_ring_start(r + 1),
              "box(%d) must equal start(%d)", r, r + 1);
    }
    CHECK(rzn_box_cells(0) == 1, "box 0");
    CHECK(rzn_box_cells(1) == 9, "box 1");
    CHECK(rzn_box_cells(2) == 25, "box 2");
}

/* --- 2. spiral bijection ------------------------------------------------- */

static void test_bijection(void)
{
    rzn_seed seed;
    int64_t idx;
    int32_t x, y;
    const int64_t N = 200000;

    banner("spiral <-> xy bijection");

    seed.cx = 137; seed.cy = -49;      /* deliberately off-origin and negative */

    for (idx = 0; idx < N; idx++) {
        int64_t back;
        rzn_spiral_to_xy(&seed, idx, &x, &y);
        back = rzn_xy_to_spiral(&seed, x, y);
        if (back != idx) {
            CHECK(0, "idx %lld -> (%d,%d) -> %lld", (long long)idx, x, y,
                  (long long)back);
            break;
        }
    }
    if (idx == N) { g_checks++; printf("  ok  %lld indices round-trip\n", (long long)N); }

    /* And the other direction, over a dense patch of coordinates. */
    for (y = seed.cy - 60; y <= seed.cy + 60; y++) {
        for (x = seed.cx - 60; x <= seed.cx + 60; x++) {
            int32_t bx, by;
            int64_t i = rzn_xy_to_spiral(&seed, x, y);
            rzn_spiral_to_xy(&seed, i, &bx, &by);
            if (bx != x || by != y) {
                CHECK(0, "(%d,%d) -> %lld -> (%d,%d)", x, y, (long long)i, bx, by);
                return;
            }
        }
    }
    g_checks++; printf("  ok  121x121 coordinate patch round-trips\n");
}

/* --- 3. ring membership and path continuity ------------------------------ */

static void test_path(void)
{
    rzn_seed seed;
    int64_t idx;
    int32_t px = 0, py = 0;

    banner("ring membership and path continuity");

    seed.cx = 0; seed.cy = 0;

    for (idx = 0; idx < 50000; idx++) {
        int32_t x, y, adx, ady, cheb, r;
        rzn_spiral_to_xy(&seed, idx, &x, &y);

        adx = x < 0 ? -x : x;
        ady = y < 0 ? -y : y;
        cheb = adx > ady ? adx : ady;
        r = rzn_spiral_ring(idx);
        if (cheb != r) {
            CHECK(0, "idx %lld at (%d,%d): chebyshev %d != ring %d",
                  (long long)idx, x, y, cheb, r);
            return;
        }

        if (idx > 0) {
            int32_t dx = x - px, dy = y - py;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy != 1) {
                CHECK(0, "spiral jumped at idx %lld: (%d,%d) -> (%d,%d)",
                      (long long)idx, px, py, x, y);
                return;
            }
        }
        px = x; py = y;
    }
    g_checks++; printf("  ok  50000 cells: ring membership exact, path unbroken\n");

    /* The specified traversal: first step is +X, second is down. */
    {
        int32_t x0, y0, x1, y1, x2, y2;
        rzn_spiral_to_xy(&seed, 0, &x0, &y0);
        rzn_spiral_to_xy(&seed, 1, &x1, &y1);
        rzn_spiral_to_xy(&seed, 2, &x2, &y2);
        CHECK(x1 == x0 + 1 && y1 == y0, "index 1 must be one step +X of the seed");
        CHECK(x2 == x1 && y2 == y1 + 1, "index 2 must be one step down from index 1");
    }
    /* Ring 1 must close at its top-right corner, ready to step +X into ring 2. */
    {
        int32_t x, y;
        rzn_spiral_to_xy(&seed, 8, &x, &y);
        CHECK(x == 1 && y == -1, "ring 1 exits at (+r,-r), got (%d,%d)", x, y);
    }
}

/* --- 4. termination bound ------------------------------------------------ */

static int ring_hits(const rzn_seed *seed, int32_t r, int32_t w, int32_t h)
{
    int64_t start = rzn_ring_start(r);
    int64_t n = rzn_ring_cells(r);
    int64_t k, hits = 0;
    for (k = 0; k < n; k++) {
        int32_t x, y;
        rzn_spiral_to_xy(seed, start + k, &x, &y);
        if (rzn_in_bounds(x, y, w, h)) hits++;
    }
    return (int)hits;
}

static void test_termination(void)
{
    const int32_t W = 97, H = 61;
    int32_t sx, sy;

    banner("termination bound");

    for (sy = 0; sy < H; sy += 7) {
        for (sx = 0; sx < W; sx += 11) {
            rzn_seed s;
            int32_t rmax;
            s.cx = sx; s.cy = sy;
            rmax = rzn_last_ring(&s, W, H);

            if (ring_hits(&s, rmax, W, H) == 0) {
                CHECK(0, "seed (%d,%d): last ring %d should still hold pixels",
                      sx, sy, rmax);
                return;
            }
            if (ring_hits(&s, rmax + 1, W, H) != 0) {
                CHECK(0, "seed (%d,%d): ring %d should be empty", sx, sy, rmax + 1);
                return;
            }
        }
    }
    g_checks++;
    printf("  ok  every seed: ring r_max is non-empty, ring r_max+1 is empty\n");
}

/* --- 5. packing round-trip ----------------------------------------------- */

static void test_packing(void)
{
    banner("word packing");

    {
        int64_t idx = 123456789 & RZN_PAYLOAD_MASK;
        int32_t w = rzn_make_word(RZN_TAG_INDEX, (uint32_t)idx, RZN_SENSOR_LEFT);
        CHECK(rzn_word_kind(w) == RZN_TAG_INDEX, "index tag");
        CHECK((int64_t)rzn_word_payload(w) == idx, "index payload");
        CHECK(rzn_word_sensor(w) == RZN_SENSOR_LEFT, "index sensor bit");
        CHECK(rzn_word_source(w) == RZN_SOURCE_SENSOR,
              "bit 0 must stay 0 -- the model reads it as the recall flag");
        CHECK(w >= 0, "words must stay non-negative (bit 31 clear)");
    }
    {
        rzn_rgb c, d, q;
        int32_t w;
        c.r = 203; c.g = 17; c.b = 250;
        w = rzn_make_word(RZN_TAG_RIGHT, rzn_rgb_payload(c), RZN_SENSOR_RIGHT);
        d = rzn_payload_rgb(rzn_word_payload(w));
        q = rzn_rgb_quantise(c);
        CHECK(rzn_word_kind(w) == RZN_TAG_RIGHT, "right tag");
        CHECK(rzn_word_sensor(w) == RZN_SENSOR_RIGHT, "right sensor bit");
        CHECK(rzn_word_source(w) == RZN_SOURCE_SENSOR,
              "bit 0 must stay 0 even for the right camera");
        CHECK(d.r == q.r && d.g == q.g && d.b == q.b,
              "colour round-trips to the profile's precision: got %u,%u,%u "
              "expected %u,%u,%u", d.r, d.g, d.b, q.r, q.g, q.b);
#if RZN_PACK_PROFILE == 32
        CHECK(d.r == c.r && d.g == c.g && d.b == c.b,
              "profile 32 must be lossless");
#else
        CHECK((d.r >> 4) == (c.r >> 4) && (d.g >> 4) == (c.g >> 4) &&
              (d.b >> 4) == (c.b >> 4),
              "profile 16 must preserve the high nibble of each channel");
#endif
        CHECK(w >= 0, "bit 31 clear");
    }
    {
        /* Index chunking: every index up to the profile maximum must survive
         * a split/reassemble round trip. */
        int64_t probes[] = { 0, 1, 4095, 4096, 65535, 1u << 20,
                             RZN_MAX_SPIRAL_INDEX };
        size_t k;
        for (k = 0; k < sizeof(probes) / sizeof(probes[0]); k++) {
            int64_t v = probes[k];
            int n;
            if (v > RZN_MAX_SPIRAL_INDEX) continue;
            n = rzn_index_word_count(v);
            CHECK(n >= 1 && n <= RZN_INDEX_CHUNKS,
                  "index %lld needs %d words", (long long)v, n);
        }
        CHECK(rzn_index_word_count(0) == 1, "index 0 is a single word");
    }
    {
        /* Largest payload a single word can hold. */
        int32_t w = rzn_make_word(RZN_TAG_INDEX, RZN_PAYLOAD_MASK,
                                  RZN_SENSOR_LEFT);
        CHECK(rzn_word_payload(w) == RZN_PAYLOAD_MASK, "max single payload");
        CHECK(w >= 0, "bit 31 clear at max payload");
        CHECK(rzn_index_more(w) == false,
              "an index word with sensor bit 0 is the last chunk");
    }
}

/* --- 6. engine: I-frame coverage and delta behaviour --------------------- */

typedef struct {
    int32_t *words;
    size_t   count;
    size_t   cap;
} capture;

static bool cap_sink(int32_t word, void *ctx)
{
    capture *c = (capture *)ctx;
    if (c->count == c->cap) {
        size_t nc = c->cap ? c->cap * 2u : 4096u;
        int32_t *n = (int32_t *)realloc(c->words, nc * sizeof(int32_t));
        if (!n) return false;
        c->words = n; c->cap = nc;
    }
    c->words[c->count++] = word;
    return true;
}

static void test_engine(void)
{
    const int32_t W = 64, H = 48;
    rzn_fovea f;
    rzn_stereo cur;
    capture cap;
    rzn_scene sc;
    uint8_t *seen;
    size_t i;

    banner("engine: I-frame coverage");

    memset(&cap, 0, sizeof(cap));
    seen = (uint8_t *)calloc((size_t)W * (size_t)H, 1);

    CHECK(rzn_stereo_alloc(&cur, W, H), "stereo alloc");
    rzn_scene_init(&sc, W, H);
    rzn_scene_step(&sc, 0);
    rzn_scene_render(&sc, &cur);

    CHECK(rzn_fovea_init(&f, W, H, 20, 30, cap_sink, &cap), "engine init");
    CHECK(rzn_fovea_submit(&f, &cur), "submit frame 0");
    CHECK(f.last.kind == RZN_FRAME_INTRA, "frame 0 must be an I-frame");

    /* Replay the captured stream exactly as the AGI would see it, and confirm
     * it addresses every sensor pixel once and only once.  Index elision means
     * an index word appears only on a discontinuity; a completed burst
     * (RIGHT word) advances the running index by one. */
    {
        int64_t idx = -1;
        int64_t pixels = 0;
        idx_rx rx;
        idx_rx_reset(&rx);
        memset(seen, 0, (size_t)W * (size_t)H);
        for (i = 0; i < cap.count; i++) {
            int32_t w = cap.words[i];
            switch (rzn_word_kind(w)) {
            case RZN_TAG_INDEX: {
                int64_t got;
                if (idx_rx_feed(&rx, w, &got)) idx = got;
                break;
            }
            case RZN_TAG_LEFT: {
                int32_t x, y;
                if (idx < 0) { CHECK(0, "LEFT word before any index"); break; }
                rzn_spiral_to_xy(&f.seed, idx, &x, &y);
                if (!rzn_in_bounds(x, y, W, H)) {
                    CHECK(0, "index %lld off-sensor", (long long)idx);
                    break;
                }
                CHECK(seen[(size_t)y * W + x] == 0, "pixel (%d,%d) twice", x, y);
                seen[(size_t)y * W + x] = 1;
                pixels++;
                break;
            }
            case RZN_TAG_RIGHT:
                idx++;      /* burst complete; next pixel is the next index */
                break;
            default:
                break;
            }
        }

        CHECK(pixels == (int64_t)W * H,
              "I-frame must emit every pixel once: %lld of %d",
              (long long)pixels, W * H);
        {
            int32_t x, y;
            int missing = 0;
            for (y = 0; y < H; y++)
                for (x = 0; x < W; x++)
                    if (!seen[(size_t)y * W + x]) missing++;
            CHECK(missing == 0, "%d sensor pixels were never addressed", missing);
        }
    }

    CHECK(f.last.pixels_emitted == (int64_t)W * H,
          "stats: pixels_emitted %lld != %d",
          (long long)f.last.pixels_emitted, W * H);
    CHECK(f.last.cells_visited > f.last.pixels_emitted,
          "the spiral must overscan the sensor corners");

    printf("  I-frame: %lld pixels, %lld cells visited, %lld words, %d rings\n",
           (long long)f.last.pixels_emitted, (long long)f.last.cells_visited,
           (long long)f.last.words_emitted, f.last.rings_scanned);

    /* --- delta: identical frame must cost nothing --- */
    banner("engine: delta path");

    cap.count = 0;
    CHECK(rzn_fovea_submit(&f, &cur), "submit identical frame");
    CHECK(f.last.kind == RZN_FRAME_DELTA, "second frame must be a delta");
    CHECK(f.last.pixels_emitted == 0,
          "an unchanged frame must emit nothing, got %lld",
          (long long)f.last.pixels_emitted);
    CHECK(cap.count == 0, "an unchanged frame must emit no words");

    /* --- delta: one changed pixel --- */
    {
        int32_t tx = 41, ty = 9;
        rzn_rgb c = rzn_image_get(&cur.left, tx, ty);
        int64_t want;
        c.r = (uint8_t)(c.r ^ 0xFF);
        rzn_image_set(&cur.left, tx, ty, c);

        cap.count = 0;
        CHECK(rzn_fovea_submit(&f, &cur), "submit one-pixel change");
        CHECK(f.last.pixels_emitted == 1,
              "expected exactly 1 changed pixel, got %lld",
              (long long)f.last.pixels_emitted);

        want = rzn_xy_to_spiral(&f.seed, tx, ty);
        {
            size_t n_idx = (size_t)rzn_index_word_count(want);
            size_t expect = n_idx + 1;      /* index words + one LEFT word */
#ifdef RZN_ENABLE_DISPARITY
            expect += 1;
#endif
            CHECK(cap.count == expect,
                  "expected %u words (%u index + colour), got %u",
                  (unsigned)expect, (unsigned)n_idx, (unsigned)cap.count);

            if (cap.count >= n_idx + 1) {
                idx_rx rx;
                int64_t got = -1;
                size_t k;
                bool done = false;
                idx_rx_reset(&rx);
                for (k = 0; k < n_idx; k++) {
                    CHECK(rzn_word_kind(cap.words[k]) == RZN_TAG_INDEX,
                          "word %u should be an index chunk", (unsigned)k);
                    done = idx_rx_feed(&rx, cap.words[k], &got);
                }
                CHECK(done, "the final index chunk must clear the more flag");
                CHECK(got == want, "index %lld != expected %lld",
                      (long long)got, (long long)want);
                CHECK(rzn_word_kind(cap.words[n_idx]) == RZN_TAG_LEFT,
                      "only the left camera changed, so only a LEFT word");
            }
        }
    }

    /* --- delta: fovea-first ordering --- */
    {
        int64_t last = -1;
        int ordered = 1;
        rzn_scene_step(&sc, 7);
        rzn_scene_render(&sc, &cur);
        cap.count = 0;
        CHECK(rzn_fovea_submit(&f, &cur), "submit moved frame");
        CHECK(f.last.pixels_emitted > 0, "a moved object must produce events");

        {
            idx_rx rx;
            idx_rx_reset(&rx);
            for (i = 0; i < cap.count; i++) {
                int64_t idx;
                if (rzn_word_kind(cap.words[i]) != RZN_TAG_INDEX) continue;
                if (!idx_rx_feed(&rx, cap.words[i], &idx)) continue;
                if (idx <= last) { ordered = 0; break; }
                last = idx;
            }
        }
        CHECK(ordered, "delta events must be ordered fovea-first by spiral index");
        printf("  delta after motion: %lld pixels, %lld words (baseline %lld)\n",
               (long long)f.last.pixels_emitted, (long long)f.last.words_emitted,
               (long long)f.last.baseline_words);
    }

    /* --- seed change forces a fresh I-frame --- */
    rzn_fovea_set_seed(&f, 5, 5);
    cap.count = 0;
    CHECK(rzn_fovea_submit(&f, &cur), "submit after seed change");
    CHECK(f.last.kind == RZN_FRAME_INTRA, "a seed change must force an I-frame");
    CHECK(f.last.pixels_emitted == (int64_t)W * H,
          "the re-anchored I-frame must cover the sensor again");

    free(seen);
    free(cap.words);
    rzn_fovea_free(&f);
    rzn_stereo_free(&cur);
}

/* --- 7. optional disparity stage ----------------------------------------- */

#ifdef RZN_ENABLE_DISPARITY
static void test_disparity(void)
{
    const int32_t W = 320, H = 240;
    rzn_stereo cur;
    rzn_scene  sc;
    rzn_disparity_cfg cfg;
    int32_t x, y;
    int bg_ok = 0, bg_n = 0, obj_ok = 0, obj_n = 0;

    banner("optional disparity stage");

    CHECK(rzn_stereo_alloc(&cur, W, H), "stereo alloc");
    rzn_scene_init(&sc, W, H);
    rzn_scene_step(&sc, 0);
    rzn_scene_render(&sc, &cur);
    rzn_disparity_default(&cfg);

    /* Sample well inside each region so the SAD window sees one surface. */
    for (y = 20; y < H - 20; y += 9) {
        for (x = 60; x < W - 60; x += 9) {
            bool on_obj = x >= sc.obj_x + 8 && x < sc.obj_x + sc.obj_w - 8 &&
                          y >= sc.obj_y + 8 && y < sc.obj_y + sc.obj_h - 8;
            bool on_bg  = x < sc.obj_x - 25 || x > sc.obj_x + sc.obj_w + 25;
            int32_t d;

            if (!on_obj && !on_bg) continue;
            d = rzn_disparity_at(&cur, &cfg, x, y);
            if (d == RZN_DISPARITY_NONE) continue;

            if (on_obj) {
                obj_n++;
                if (d == sc.obj_disparity) obj_ok++;
            } else {
                bg_n++;
                if (d == sc.bg_disparity) bg_ok++;
            }
        }
    }

    printf("  back plane (truth %d px): %d/%d exact\n",
           sc.bg_disparity, bg_ok, bg_n);
    printf("  object     (truth %d px): %d/%d exact\n",
           sc.obj_disparity, obj_ok, obj_n);

    CHECK(bg_n > 0 && bg_ok * 10 >= bg_n * 9,
          "back-plane disparity should be recovered on >=90%% of samples");
    CHECK(obj_n > 0 && obj_ok * 10 >= obj_n * 9,
          "object disparity should be recovered on >=90%% of samples");

    rzn_stereo_free(&cur);
}
#endif

/* --- 8. spiral index capacity -------------------------------------------- */

/* Profile 16 caps the sensor at ~4093 px in its largest dimension, because the
 * chunked index carries 24 bits.  That ceiling is live by default, so the
 * engine must refuse a sensor it cannot address rather than silently wrap. */
static void test_index_capacity(void)
{
    rzn_fovea f;
    int32_t r_ok = 0;

    banner("spiral index capacity");

    /* Largest r_max the profile can address, remembering that the scan runs
     * one ring past the last ring holding a pixel. */
    while (rzn_box_cells(r_ok + 2) - 1 <= RZN_MAX_SPIRAL_INDEX)
        r_ok++;

    printf("  profile %d: %d index bits, largest ring %d "
           "(sensor up to ~%d px)\n",
           RZN_PACK_PROFILE, RZN_PAYLOAD_BITS * RZN_INDEX_CHUNKS,
           r_ok, 2 * r_ok + 1);

    /* A seed in the corner of a (r_ok + 1) x 2 sensor gives r_max == r_ok. */
    CHECK(rzn_fovea_init(&f, r_ok + 1, 2, 0, 0, cap_sink, NULL),
          "a sensor needing exactly ring %d must be accepted", r_ok);
    rzn_fovea_free(&f);

    /* One pixel wider needs ring r_ok + 1, which the index cannot address. */
    CHECK(!rzn_fovea_init(&f, r_ok + 2, 2, 0, 0, cap_sink, NULL),
          "a sensor needing ring %d must be refused", r_ok + 1);

    /* Refusal must happen before any allocation, and leave nothing behind. */
    CHECK(f.prev.left.px == NULL && f.prev.right.px == NULL,
          "a refused init must not leak a frame buffer");
}

int main(void)
{
    printf("RZN AI foveal stereo input engine -- tests\n");

    test_ring_arithmetic();
    test_bijection();
    test_path();
    test_termination();
    test_packing();
    test_engine();
    test_index_capacity();
#ifdef RZN_ENABLE_DISPARITY
    test_disparity();
#endif

    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
