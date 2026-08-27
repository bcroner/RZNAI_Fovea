/* rzn_frame.c -- frame buffers, PPM I/O, synthetic scene, change detection. */

#include "rzn_frame.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --- buffers ------------------------------------------------------------- */

bool rzn_image_alloc(rzn_image *im, int32_t w, int32_t h)
{
    im->w = w; im->h = h; im->px = NULL;
    if (w <= 0 || h <= 0) return false;
    im->px = (uint8_t *)calloc((size_t)w * (size_t)h * 3u, 1u);
    return im->px != NULL;
}

void rzn_image_free(rzn_image *im)
{
    free(im->px);
    im->px = NULL;
    im->w = im->h = 0;
}

void rzn_image_copy(rzn_image *dst, const rzn_image *src)
{
    memcpy(dst->px, src->px, (size_t)src->w * (size_t)src->h * 3u);
}

bool rzn_stereo_alloc(rzn_stereo *s, int32_t w, int32_t h)
{
    if (!rzn_image_alloc(&s->left, w, h)) return false;
    if (!rzn_image_alloc(&s->right, w, h)) {
        rzn_image_free(&s->left);
        return false;
    }
    return true;
}

void rzn_stereo_free(rzn_stereo *s)
{
    rzn_image_free(&s->left);
    rzn_image_free(&s->right);
}

void rzn_stereo_copy(rzn_stereo *dst, const rzn_stereo *src)
{
    rzn_image_copy(&dst->left, &src->left);
    rzn_image_copy(&dst->right, &src->right);
}

/* --- PPM ----------------------------------------------------------------- */

static int rzn_ppm_next_int(FILE *f, int *out)
{
    int c, v = 0, got = 0;
    for (;;) {
        c = fgetc(f);
        if (c == EOF) { if (got) { *out = v; return 1; } return 0; }
        if (c == '#') { while (c != '\n' && c != EOF) c = fgetc(f); continue; }
        if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); got = 1; continue; }
        if (got) { *out = v; return 1; }
        /* whitespace before the number: keep scanning */
    }
}

bool rzn_image_load_ppm(rzn_image *im, const char *path)
{
    FILE *f = fopen(path, "rb");
    int w = 0, h = 0, maxv = 0;
    char magic[3] = {0, 0, 0};
    size_t need;

    if (!f) return false;
    if (fread(magic, 1, 2, f) != 2 || magic[0] != 'P' || magic[1] != '6') {
        fclose(f); return false;
    }
    if (!rzn_ppm_next_int(f, &w) || !rzn_ppm_next_int(f, &h) ||
        !rzn_ppm_next_int(f, &maxv) || maxv != 255) {
        fclose(f); return false;
    }
    if (!rzn_image_alloc(im, w, h)) { fclose(f); return false; }

    need = (size_t)w * (size_t)h * 3u;
    if (fread(im->px, 1, need, f) != need) {
        rzn_image_free(im); fclose(f); return false;
    }
    fclose(f);
    return true;
}

bool rzn_image_save_ppm(const rzn_image *im, const char *path)
{
    FILE *f = fopen(path, "wb");
    size_t need;
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", im->w, im->h);
    need = (size_t)im->w * (size_t)im->h * 3u;
    if (fwrite(im->px, 1, need, f) != need) { fclose(f); return false; }
    fclose(f);
    return true;
}

/* --- synthetic scene ----------------------------------------------------- */

/* Deterministic value noise, so runs are reproducible and comparable. */
static uint32_t rzn_hash2(int32_t x, int32_t y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static rzn_rgb rzn_backdrop(int32_t x, int32_t y)
{
    /* Coarse blocks plus a gradient: enough texture for block matching to
     * lock onto, without per-pixel noise that would defeat it. */
    uint32_t n = rzn_hash2(x >> 3, y >> 3);
    rzn_rgb c;
    c.r = (uint8_t)(40 + (n & 0x3Fu) + (x % 90));
    c.g = (uint8_t)(50 + ((n >> 8) & 0x3Fu) + (y / 6) % 60);
    c.b = (uint8_t)(70 + ((n >> 16) & 0x3Fu));
    return c;
}

void rzn_scene_init(rzn_scene *sc, int32_t w, int32_t h)
{
    sc->w = w;
    sc->h = h;
    sc->bg_disparity  = 3;
    sc->obj_disparity = 17;
    sc->obj_w = w / 6;
    sc->obj_h = h / 5;
    sc->obj_x = w / 8;
    sc->obj_y = h / 2 - sc->obj_h / 2;
}

void rzn_scene_step(rzn_scene *sc, int32_t frame)
{
    /* Slow horizontal drift with a gentle vertical bob. */
    int32_t span = sc->w - sc->obj_w - sc->w / 4;
    if (span < 1) span = 1;
    sc->obj_x = sc->w / 8 + (frame * 3) % span;
    sc->obj_y = sc->h / 2 - sc->obj_h / 2 + ((frame / 4) % 7) - 3;
}

static bool rzn_scene_hit(const rzn_scene *sc, int32_t x, int32_t y)
{
    return x >= sc->obj_x && x < sc->obj_x + sc->obj_w &&
           y >= sc->obj_y && y < sc->obj_y + sc->obj_h;
}

/* Colour and disparity of the surface visible at left-camera pixel (x, y). */
static void rzn_scene_sample(const rzn_scene *sc, int32_t x, int32_t y,
                             rzn_rgb *col, int32_t *disp)
{
    if (rzn_scene_hit(sc, x, y)) {
        uint32_t n = rzn_hash2((x - sc->obj_x) >> 2, (y - sc->obj_y) >> 2);
        col->r = (uint8_t)(200 + (n & 0x1Fu));
        col->g = (uint8_t)(90 + ((n >> 8) & 0x1Fu));
        col->b = (uint8_t)(60 + ((n >> 16) & 0x1Fu));
        *disp = sc->obj_disparity;
    } else {
        *col = rzn_backdrop(x, y);
        *disp = sc->bg_disparity;
    }
}

void rzn_scene_render(const rzn_scene *sc, rzn_stereo *out)
{
    int32_t x, y;

    for (y = 0; y < sc->h; y++) {
        for (x = 0; x < sc->w; x++) {
            rzn_rgb c;
            int32_t d;
            rzn_scene_sample(sc, x, y, &c, &d);
            rzn_image_set(&out->left, x, y, c);
        }
    }

    /* Right camera: the same surface appears shifted left by its disparity.
     * Lay down the back plane first, then forward-project the foreground, so
     * disocclusions fill with backdrop -- which is what a real rig sees. */
    for (y = 0; y < sc->h; y++) {
        for (x = 0; x < sc->w; x++)
            rzn_image_set(&out->right, x, y,
                          rzn_backdrop(x + sc->bg_disparity, y));
    }
    for (y = 0; y < sc->h; y++) {
        for (x = 0; x < sc->w; x++) {
            rzn_rgb c;
            int32_t d, rx;
            if (!rzn_scene_hit(sc, x, y)) continue;
            rzn_scene_sample(sc, x, y, &c, &d);
            rx = x - d;
            if (rx >= 0 && rx < sc->w)
                rzn_image_set(&out->right, rx, y, c);
        }
    }
}

/* --- change events ------------------------------------------------------- */

bool rzn_events_reserve(rzn_event_list *l, size_t cap)
{
    rzn_event *n;
    if (l->cap >= cap) return true;
    n = (rzn_event *)realloc(l->e, cap * sizeof(rzn_event));
    if (!n) return false;
    l->e = n;
    l->cap = cap;
    return true;
}

void rzn_events_free(rzn_event_list *l)
{
    free(l->e);
    l->e = NULL;
    l->count = l->cap = 0;
}

static bool rzn_px_differs(const rzn_image *a, const rzn_image *b,
                           int32_t x, int32_t y, int32_t thr)
{
    const uint8_t *p = a->px + ((size_t)y * (size_t)a->w + (size_t)x) * 3u;
    const uint8_t *q = b->px + ((size_t)y * (size_t)b->w + (size_t)x) * 3u;
    int32_t d0 = (int32_t)p[0] - (int32_t)q[0];
    int32_t d1 = (int32_t)p[1] - (int32_t)q[1];
    int32_t d2 = (int32_t)p[2] - (int32_t)q[2];
    if (d0 < 0) d0 = -d0;
    if (d1 < 0) d1 = -d1;
    if (d2 < 0) d2 = -d2;
    return d0 > thr || d1 > thr || d2 > thr;
}

void rzn_detect_changes(const rzn_stereo *prev, const rzn_stereo *cur,
                        int32_t threshold, rzn_event_list *out)
{
    int32_t x, y;
    out->count = 0;
    for (y = 0; y < cur->left.h; y++) {
        for (x = 0; x < cur->left.w; x++) {
            bool cl = rzn_px_differs(&prev->left,  &cur->left,  x, y, threshold);
            bool cr = rzn_px_differs(&prev->right, &cur->right, x, y, threshold);
            if (!cl && !cr) continue;
            if (out->count == out->cap &&
                !rzn_events_reserve(out, out->cap ? out->cap * 2u : 1024u))
                return;
            out->e[out->count].idx           = 0;
            out->e[out->count].x             = x;
            out->e[out->count].y             = y;
            out->e[out->count].left_changed  = cl;
            out->e[out->count].right_changed = cr;
            out->count++;
        }
    }
}
