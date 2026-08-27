/* rzn_frame.h -- stereo frame buffers, image I/O, and the change-event source.
 *
 * The change detector here is a plain software stand-in for a real ISP or
 * Neural Processing Engine.  On the target robot the NPE hands you changed
 * (x, y) coordinates directly; this module exists so the reference
 * implementation runs and can be measured without that hardware.  Replace
 * rzn_detect_changes() with the vendor event stream and nothing else moves.
 */
#ifndef RZN_FRAME_H
#define RZN_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rzn_pack.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Packed RGB888 image, row-major, no padding. */
typedef struct {
    int32_t  w, h;
    uint8_t *px;          /* w * h * 3 bytes */
} rzn_image;

/* The two cameras.  Both must be exactly the same resolution -- they share a
 * single retinal coordinate frame, which is what lets one spiral index
 * address both at once. */
typedef struct {
    rzn_image left;
    rzn_image right;
} rzn_stereo;

bool rzn_image_alloc(rzn_image *im, int32_t w, int32_t h);
void rzn_image_free(rzn_image *im);
void rzn_image_copy(rzn_image *dst, const rzn_image *src);

bool rzn_stereo_alloc(rzn_stereo *s, int32_t w, int32_t h);
void rzn_stereo_free(rzn_stereo *s);
void rzn_stereo_copy(rzn_stereo *dst, const rzn_stereo *src);

static inline rzn_rgb rzn_image_get(const rzn_image *im, int32_t x, int32_t y)
{
    const uint8_t *p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 3u;
    rzn_rgb c;
    c.r = p[0]; c.g = p[1]; c.b = p[2];
    return c;
}

static inline void rzn_image_set(rzn_image *im, int32_t x, int32_t y, rzn_rgb c)
{
    uint8_t *p = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 3u;
    p[0] = c.r; p[1] = c.g; p[2] = c.b;
}

/* Binary PPM (P6, maxval 255). */
bool rzn_image_load_ppm(rzn_image *im, const char *path);
bool rzn_image_save_ppm(const rzn_image *im, const char *path);

/* --- synthetic stereo scene --------------------------------------------- */

/* A rectified side-by-side rig looking at a textured back plane with one
 * solid object floating in front of it.  The object translates with time, so
 * successive frames exercise the delta path with a controllable amount of
 * change.  Right-camera pixels are the left image sampled at x + disparity,
 * so the optional disparity stage has a real, known-truth signal to find. */
typedef struct {
    int32_t w, h;
    int32_t bg_disparity;      /* disparity of the back plane, px */
    int32_t obj_disparity;     /* disparity of the foreground object, px */
    int32_t obj_w, obj_h;
    int32_t obj_x, obj_y;      /* top-left of the object, left-camera frame */
} rzn_scene;

void rzn_scene_init(rzn_scene *sc, int32_t w, int32_t h);
void rzn_scene_step(rzn_scene *sc, int32_t frame);
void rzn_scene_render(const rzn_scene *sc, rzn_stereo *out);

/* --- change events ------------------------------------------------------- */

typedef struct {
    int64_t idx;               /* foveal spiral index */
    int32_t x, y;
    bool    left_changed;
    bool    right_changed;
} rzn_event;

typedef struct {
    rzn_event *e;
    size_t     count;
    size_t     cap;
} rzn_event_list;

bool rzn_events_reserve(rzn_event_list *l, size_t cap);
void rzn_events_free(rzn_event_list *l);

/* Compare prev against cur and append one event per changed pixel.  A pixel
 * counts as changed when any channel differs by more than `threshold`.
 * Events are appended in raster order and carry no spiral index yet -- the
 * engine assigns and sorts those. */
void rzn_detect_changes(const rzn_stereo *prev, const rzn_stereo *cur,
                        int32_t threshold, rzn_event_list *out);

#ifdef __cplusplus
}
#endif
#endif /* RZN_FRAME_H */
