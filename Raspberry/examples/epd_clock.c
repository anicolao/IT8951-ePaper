#include "../lib/Config/DEV_Config.h"
#include "../lib/e-Paper/EPD_IT8951.h"
#include "../lib/GUI/GUI_Paint.h"

#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef DEBUG_WITH_BOUNDING_BOX
#define DEBUG_WITH_BOUNDING_BOX 0
#endif

static IT8951_Dev_Info g_dev_info = {0, 0};
static UBYTE *g_mono_panel_face_buf = NULL;
static UBYTE *g_mono_area_buf = NULL;

static void cleanup_and_exit(int code)
{
    if (g_mono_panel_face_buf != NULL) { free(g_mono_panel_face_buf); g_mono_panel_face_buf = NULL; }
    if (g_mono_area_buf != NULL) { free(g_mono_area_buf); g_mono_area_buf = NULL; }
    if (g_dev_info.Panel_W != 0) {
        EPD_IT8951_Sleep();
    }
    DEV_Module_Exit();
    exit(code);
}

static void signal_handler(int signo)
{
    (void)signo;
    cleanup_and_exit(0);
}

static UWORD effective_panel_width(UWORD panel_width, const char *lut_version)
{
    if (strcmp(lut_version, "M641") == 0 || strcmp(lut_version, "M841_TFAB512") == 0) {
        return panel_width - (panel_width % 32);
    }
    return panel_width;
}

static void apply_mode(int mode)
{
    if (mode == 1 || mode == 2) {
        Paint_SetRotate(ROTATE_0);
        Paint_SetMirroring(MIRROR_HORIZONTAL);
    } else {
        Paint_SetRotate(ROTATE_0);
        Paint_SetMirroring(MIRROR_NONE);
    }
}

static void hand_end(UWORD cx, UWORD cy, double deg, UWORD len, UWORD max_w, UWORD max_h, UWORD *x2, UWORD *y2)
{
    double rad = deg * M_PI / 180.0;
    int x = (int)lround((double)cx + cos(rad) * (double)len);
    int y = (int)lround((double)cy + sin(rad) * (double)len);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)max_w) x = (int)max_w - 1;
    if (y >= (int)max_h) y = (int)max_h - 1;
    *x2 = (UWORD)x;
    *y2 = (UWORD)y;
}

static void second_hand_end(UWORD w, UWORD h, int sec, UWORD *x2, UWORD *y2)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD radius = (w < h ? w : h) / 2 - 16;
    double sec_deg = (sec * 6.0) - 90.0;
    hand_end(cx, cy, sec_deg, radius * 90 / 100, w, h, x2, y2);
}

static void minute_hand_end(UWORD w, UWORD h, int min, UWORD *x2, UWORD *y2)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD radius = (w < h ? w : h) / 2 - 16;
    double min_deg = (min * 6.0) - 90.0;
    hand_end(cx, cy, min_deg, radius * 75 / 100, w, h, x2, y2);
}

static void hour_hand_end(UWORD w, UWORD h, int hour, int min, UWORD *x2, UWORD *y2)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD radius = (w < h ? w : h) / 2 - 16;
    double hour_deg = (((hour % 12) + min / 60.0) * 30.0) - 90.0;
    hand_end(cx, cy, hour_deg, radius * 55 / 100, w, h, x2, y2);
}

static void draw_rect_outline_raw(UWORD w, UWORD h, UBYTE color)
{
    if (w == 0 || h == 0) {
        return;
    }
    for (UWORD x = 0; x < w; x++) {
        Paint_SetPixel(x, 0, color);
        Paint_SetPixel(x, h - 1, color);
    }
    for (UWORD y = 0; y < h; y++) {
        Paint_SetPixel(0, y, color);
        Paint_SetPixel(w - 1, y, color);
    }
}

static void draw_clock_background_mono(UWORD w, UWORD h)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD radius = (w < h ? w : h) / 2 - 16;

    Paint_Clear(WHITE);
    Paint_DrawCircle(cx, cy, radius, 0x00, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);

    for (int i = 0; i < 60; i++) {
        double a = (double)(i * 6 - 90);
        UWORD r1 = radius - ((i % 5 == 0) ? 16 : 8);
        UWORD r2 = radius - 2;
        UWORD tx1, ty1, tx2, ty2;
        hand_end(cx, cy, a, r1, w, h, &tx1, &ty1);
        hand_end(cx, cy, a, r2, w, h, &tx2, &ty2);
        Paint_DrawLine(tx1, ty1, tx2, ty2, 0x00, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
}

static double elapsed_seconds(const struct timespec *a, const struct timespec *b)
{
    return (double)(b->tv_sec - a->tv_sec) + ((double)(b->tv_nsec - a->tv_nsec) / 1e9);
}

static void draw_hands_mono(UWORD w, UWORD h, int hour, int min, int sec, int ox, int oy)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD hx, hy, mx, my, sx, sy;
    hour_hand_end(w, h, hour, min, &hx, &hy);
    minute_hand_end(w, h, min, &mx, &my);
    second_hand_end(w, h, sec, &sx, &sy);

    Paint_DrawLine((UWORD)((int)cx - ox), (UWORD)((int)cy - oy),
                   (UWORD)((int)hx - ox), (UWORD)((int)hy - oy),
                   0x00, DOT_PIXEL_3X3, LINE_STYLE_SOLID);
    Paint_DrawLine((UWORD)((int)cx - ox), (UWORD)((int)cy - oy),
                   (UWORD)((int)mx - ox), (UWORD)((int)my - oy),
                   0x00, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
    Paint_DrawLine((UWORD)((int)cx - ox), (UWORD)((int)cy - oy),
                   (UWORD)((int)sx - ox), (UWORD)((int)sy - oy),
                   0x00, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawCircle((UWORD)((int)cx - ox), (UWORD)((int)cy - oy), 3, 0x00, DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

static void expand_bbox(int *x0, int *y0, int *x1, int *y1, int x, int y, int margin, int max_w, int max_h)
{
    int lx = x - margin;
    int ly = y - margin;
    int rx = x + margin;
    int ry = y + margin;
    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (rx >= max_w) rx = max_w - 1;
    if (ry >= max_h) ry = max_h - 1;
    if (lx < *x0) *x0 = lx;
    if (ly < *y0) *y0 = ly;
    if (rx > *x1) *x1 = rx;
    if (ry > *y1) *y1 = ry;
}

static void align_bbox_for_1bpp(int *x0, int *x1, int max_w)
{
    if (*x0 < 0) *x0 = 0;
    if (*x1 >= max_w) *x1 = max_w - 1;
    if (*x1 < *x0) *x1 = *x0;

    // 1bpp writes are addressed as X/8, W/8.
    // In practice (matching Waveshare examples), dynamic ROIs are safest on 32px boundaries.
    *x0 &= ~31;
    if (*x0 < 0) *x0 = 0;
    *x1 = ((*x1 + 1 + 31) & ~31) - 1;
    if (*x1 >= max_w) *x1 = max_w - 1;

    // Enforce W multiple of 32 after right-edge clamp.
    if (((*x1 - *x0 + 1) & 31) != 0) {
        *x1 = *x0 + (((*x1 - *x0 + 1) + 31) & ~31) - 1;
        if (*x1 >= max_w) {
            *x1 = max_w - 1;
            while (((*x1 - *x0 + 1) & 31) != 0 && *x0 >= 32) {
                *x0 -= 32;
            }
            *x0 &= ~31;
            if (*x0 < 0) *x0 = 0;
            if (*x1 < *x0) *x1 = *x0;
            *x1 = *x0 + (((*x1 - *x0 + 1) + 31) & ~31) - 1;
            if (*x1 >= max_w) *x1 = max_w - 1;
        }
    }
}

int main(int argc, char *argv[])
{
    UWORD vcom = 0;
    int epd_mode = 0;
    double temp_vcom = 0.0;
    UWORD panel_w = 0;
    UWORD panel_h = 0;
    UDOUBLE target_addr = 0;
    UWORD roi_w = 0;
    UWORD roi_h = 0;
    UDOUBLE mono_panel_size = 0;
    UDOUBLE mono_roi_size = 0;
    UDOUBLE base_addr = 0;
    int last_hour = -1;
    int last_min = -1;
    int last_sec = -1;

    signal(SIGINT, signal_handler);

    if (argc < 2 || argc > 3) {
        Debug("Usage: sudo ./epd_clock <VCOM> [mode]\n");
        Debug("Example: sudo ./epd_clock -2.51 0\n");
        return 1;
    }

    if (DEV_Module_Init() != 0) {
        return 1;
    }

    sscanf(argv[1], "%lf", &temp_vcom);
    vcom = (UWORD)(fabs(temp_vcom) * 1000);
    if (argc == 3) {
        epd_mode = atoi(argv[2]);
    }

    g_dev_info = EPD_IT8951_Init(vcom);
    panel_w = effective_panel_width(g_dev_info.Panel_W, (const char *)g_dev_info.LUT_Version);
    panel_h = g_dev_info.Panel_H;
    target_addr = g_dev_info.Memory_Addr_L | (g_dev_info.Memory_Addr_H << 16);
    base_addr = target_addr;

    // For second-hand updates we now compute ROIs directly on full-panel coordinates.
    roi_w = panel_w;
    roi_h = panel_h;

    mono_panel_size = ((panel_w + 7) / 8) * panel_h;
    mono_roi_size = ((roi_w + 7) / 8) * roi_h;

    g_mono_panel_face_buf = (UBYTE *)malloc(mono_panel_size);
    g_mono_area_buf = (UBYTE *)malloc(mono_roi_size);
    if (g_mono_panel_face_buf == NULL || g_mono_area_buf == NULL) {
        Debug("Failed to allocate clock buffers\n");
        cleanup_and_exit(1);
    }

    EPD_IT8951_Clear_Refresh(g_dev_info, target_addr, INIT_Mode);
    {
        struct timespec t0, t1, t2, t3;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        Paint_NewImage(g_mono_panel_face_buf, panel_w, panel_h, 0, BLACK);
        Paint_SelectImage(g_mono_panel_face_buf);
        apply_mode(epd_mode);
        Paint_SetBitsPerPixel(1);
        draw_clock_background_mono(panel_w, panel_h);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        EPD_IT8951_1bp_Multi_Frame_Write(g_mono_panel_face_buf, 0, 0, panel_w, panel_h, base_addr, true);
        clock_gettime(CLOCK_MONOTONIC, &t2);
        EPD_IT8951_1bp_Multi_Frame_Refresh_Mode(0, 0, panel_w, panel_h, GC16_Mode, base_addr);
        clock_gettime(CLOCK_MONOTONIC, &t3);
        Debug("Initial base face load: compose=%.3fs write=%.3fs refresh=%.3fs total=%.3fs\n",
              elapsed_seconds(&t0, &t1),
              elapsed_seconds(&t1, &t2),
              elapsed_seconds(&t2, &t3),
              elapsed_seconds(&t0, &t3));
    }

    while (1) {
        time_t t = time(NULL);
        struct tm tm_now;
        localtime_r(&t, &tm_now);

        if (tm_now.tm_sec != last_sec || tm_now.tm_min != last_min || tm_now.tm_hour != last_hour) {
            int prev_hour = (last_hour < 0) ? tm_now.tm_hour : last_hour;
            int prev_min = (last_min < 0) ? tm_now.tm_min : last_min;
            int prev_sec = (last_sec < 0) ? tm_now.tm_sec : last_sec;
            UWORD cx = panel_w / 2, cy = panel_h / 2;
            UWORD x_old_h, y_old_h, x_old_m, y_old_m, x_old_s, y_old_s;
            UWORD x_new_h, y_new_h, x_new_m, y_new_m, x_new_s, y_new_s;
            int x0, y0, x1, y1;
            UWORD bw, bh;

            hour_hand_end(panel_w, panel_h, prev_hour, prev_min, &x_old_h, &y_old_h);
            minute_hand_end(panel_w, panel_h, prev_min, &x_old_m, &y_old_m);
            second_hand_end(panel_w, panel_h, prev_sec, &x_old_s, &y_old_s);
            hour_hand_end(panel_w, panel_h, tm_now.tm_hour, tm_now.tm_min, &x_new_h, &y_new_h);
            minute_hand_end(panel_w, panel_h, tm_now.tm_min, &x_new_m, &y_new_m);
            second_hand_end(panel_w, panel_h, tm_now.tm_sec, &x_new_s, &y_new_s);

            x0 = (int)panel_w - 1;
            y0 = (int)panel_h - 1;
            x1 = 0;
            y1 = 0;
            expand_bbox(&x0, &y0, &x1, &y1, cx, cy, 8, panel_w, panel_h);
            expand_bbox(&x0, &y0, &x1, &y1, x_old_h, y_old_h, 8, panel_w, panel_h);
            expand_bbox(&x0, &y0, &x1, &y1, x_old_m, y_old_m, 6, panel_w, panel_h);
            expand_bbox(&x0, &y0, &x1, &y1, x_old_s, y_old_s, 4, panel_w, panel_h);
            expand_bbox(&x0, &y0, &x1, &y1, x_new_h, y_new_h, 8, panel_w, panel_h);
            expand_bbox(&x0, &y0, &x1, &y1, x_new_m, y_new_m, 6, panel_w, panel_h);
            expand_bbox(&x0, &y0, &x1, &y1, x_new_s, y_new_s, 4, panel_w, panel_h);

            align_bbox_for_1bpp(&x0, &x1, panel_w);
            bw = (UWORD)(x1 - x0 + 1);
            bh = (UWORD)(y1 - y0 + 1);
            {
                UWORD abs_x = (UWORD)x0;
                UWORD abs_y = (UWORD)y0;
                Debug("A2 ROI sec %02d->%02d: x=%u y=%u w=%u h=%u (x%%32=%u w%%32=%u y%%2=%u h%%2=%u)\n",
                      prev_sec, tm_now.tm_sec,
                      abs_x, abs_y, bw, bh,
                      abs_x % 32, bw % 32, abs_y % 2, bh % 2);
            }

            Paint_NewImage(g_mono_area_buf, bw, bh, 0, BLACK);
            Paint_SelectImage(g_mono_area_buf);
            apply_mode(epd_mode);
            Paint_SetBitsPerPixel(1);
            // Start from cached 1bpp clock face for this minute, then overlay second hand.
            {
                UWORD src_stride = (panel_w + 7) / 8;
                UWORD dst_stride = (bw + 7) / 8;
                for (UWORD ry = 0; ry < bh; ry++) {
                    memcpy(g_mono_area_buf + (ry * dst_stride),
                           g_mono_panel_face_buf + ((((UWORD)y0 + ry) * src_stride)) + ((UWORD)x0 / 8),
                           dst_stride);
                }
            }
#if DEBUG_WITH_BOUNDING_BOX
            draw_rect_outline_raw(bw, bh, 0x00);
#else
            draw_hands_mono(panel_w, panel_h, tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, x0, y0);
#endif
            EPD_IT8951_1bp_Refresh(g_mono_area_buf, (UWORD)x0, (UWORD)y0, bw, bh, A2_Mode, target_addr, false);
            last_hour = tm_now.tm_hour;
            last_min = tm_now.tm_min;
            last_sec = tm_now.tm_sec;
        }

        DEV_Delay_ms(20);
    }

    return 0;
}
