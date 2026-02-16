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

static IT8951_Dev_Info g_dev_info = {0, 0};
static UBYTE *g_full_buf = NULL;
static UBYTE *g_face_buf = NULL;
static UBYTE *g_roi_buf = NULL;
static UBYTE *g_mono_face_buf = NULL;
static UBYTE *g_mono_area_buf = NULL;

static void cleanup_and_exit(int code)
{
    if (g_full_buf != NULL) {
        free(g_full_buf);
        g_full_buf = NULL;
    }
    if (g_face_buf != NULL) { free(g_face_buf); g_face_buf = NULL; }
    if (g_roi_buf != NULL) { free(g_roi_buf); g_roi_buf = NULL; }
    if (g_mono_face_buf != NULL) { free(g_mono_face_buf); g_mono_face_buf = NULL; }
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

static void hand_end(UWORD cx, UWORD cy, double deg, UWORD len, UWORD *x2, UWORD *y2)
{
    double rad = deg * M_PI / 180.0;
    int x = (int)lround((double)cx + cos(rad) * (double)len);
    int y = (int)lround((double)cy + sin(rad) * (double)len);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    *x2 = (UWORD)x;
    *y2 = (UWORD)y;
}

static void draw_clock_face(UWORD w, UWORD h, struct tm *tm_now)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD radius = (w < h ? w : h) / 2 - 16;
    UWORD x2, y2;

    Paint_Clear(WHITE);
    Paint_DrawCircle(cx, cy, radius, 0x00, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);

    for (int i = 0; i < 60; i++) {
        double a = (double)(i * 6 - 90);
        UWORD r1 = radius - ((i % 5 == 0) ? 16 : 8);
        UWORD r2 = radius - 2;
        UWORD tx1, ty1, tx2, ty2;
        hand_end(cx, cy, a, r1, &tx1, &ty1);
        hand_end(cx, cy, a, r2, &tx2, &ty2);
        Paint_DrawLine(tx1, ty1, tx2, ty2, (i % 5 == 0) ? 0x00 : 0x90, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }

    double hour_deg = (((tm_now->tm_hour % 12) + tm_now->tm_min / 60.0) * 30.0) - 90.0;
    double min_deg = (tm_now->tm_min * 6.0) - 90.0;

    hand_end(cx, cy, hour_deg, radius * 55 / 100, &x2, &y2);
    Paint_DrawLine(cx, cy, x2, y2, 0x00, DOT_PIXEL_3X3, LINE_STYLE_SOLID);

    hand_end(cx, cy, min_deg, radius * 75 / 100, &x2, &y2);
    Paint_DrawLine(cx, cy, x2, y2, 0x10, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

    Paint_DrawCircle(cx, cy, 4, 0x00, DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

static void second_hand_end(UWORD w, UWORD h, int sec, UWORD *x2, UWORD *y2)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD radius = (w < h ? w : h) / 2 - 16;
    double sec_deg = (sec * 6.0) - 90.0;
    hand_end(cx, cy, sec_deg, radius * 90 / 100, x2, y2);
}

static void draw_second_hand(UWORD w, UWORD h, int sec, UBYTE color, int ox, int oy)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD x2, y2;
    second_hand_end(w, h, sec, &x2, &y2);
    Paint_DrawLine((UWORD)((int)cx - ox), (UWORD)((int)cy - oy),
                   (UWORD)((int)x2 - ox), (UWORD)((int)y2 - oy),
                   color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    Paint_DrawCircle((UWORD)((int)cx - ox), (UWORD)((int)cy - oy), 3, 0x00, DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

static void draw_clock_face_mono(UWORD w, UWORD h, struct tm *tm_now)
{
    UWORD cx = w / 2;
    UWORD cy = h / 2;
    UWORD radius = (w < h ? w : h) / 2 - 16;
    UWORD x2, y2;

    Paint_Clear(WHITE);
    Paint_DrawCircle(cx, cy, radius, 0x00, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);

    for (int i = 0; i < 60; i++) {
        double a = (double)(i * 6 - 90);
        UWORD r1 = radius - ((i % 5 == 0) ? 16 : 8);
        UWORD r2 = radius - 2;
        UWORD tx1, ty1, tx2, ty2;
        hand_end(cx, cy, a, r1, &tx1, &ty1);
        hand_end(cx, cy, a, r2, &tx2, &ty2);
        Paint_DrawLine(tx1, ty1, tx2, ty2, 0x00, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }

    {
        double hour_deg = (((tm_now->tm_hour % 12) + tm_now->tm_min / 60.0) * 30.0) - 90.0;
        double min_deg = (tm_now->tm_min * 6.0) - 90.0;
        hand_end(cx, cy, hour_deg, radius * 55 / 100, &x2, &y2);
        Paint_DrawLine(cx, cy, x2, y2, 0x00, DOT_PIXEL_3X3, LINE_STYLE_SOLID);
        hand_end(cx, cy, min_deg, radius * 75 / 100, &x2, &y2);
        Paint_DrawLine(cx, cy, x2, y2, 0x00, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
    }

    Paint_DrawCircle(cx, cy, 4, 0x00, DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

static void align_bbox_for_1bpp(int *x0, int *x1, int max_w)
{
    int w = *x1 - *x0 + 1;
    int aw;

    if (*x0 < 0) *x0 = 0;
    if (*x1 >= max_w) *x1 = max_w - 1;

    w = *x1 - *x0 + 1;
    aw = (w + 31) & ~31;
    if (aw < 32) aw = 32;
    if (aw > max_w) aw = max_w & ~31;
    if (aw <= 0) aw = max_w;

    *x0 &= ~31;
    if (*x0 < 0) *x0 = 0;
    if (*x0 + aw > max_w) {
        *x0 = max_w - aw;
        if (*x0 < 0) *x0 = 0;
    }
    *x1 = *x0 + aw - 1;
    if (*x1 >= max_w) *x1 = max_w - 1;
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
    UWORD roi_x = 0;
    UWORD roi_y = 0;
    UDOUBLE full_size = 0;
    UDOUBLE roi_size = 0;
    UDOUBLE mono_full_size = 0;
    int last_min = -1;
    int last_sec = -1;
    const UBYTE second_color = 0x00;

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

    full_size = ((panel_w * 4 % 8 == 0) ? (panel_w * 4 / 8) : (panel_w * 4 / 8 + 1)) * panel_h;
    g_full_buf = (UBYTE *)malloc(full_size);
    if (g_full_buf == NULL) {
        Debug("Failed to allocate full frame buffer\n");
        cleanup_and_exit(1);
    }

    roi_w = (panel_w * 8) / 10;
    roi_h = (panel_h * 8) / 10;
    roi_w = roi_w - (roi_w % 2);
    roi_h = roi_h - (roi_h % 2);
    roi_x = (panel_w - roi_w) / 2;
    roi_y = (panel_h - roi_h) / 2;

    roi_size = ((roi_w * 4 % 8 == 0) ? (roi_w * 4 / 8) : (roi_w * 4 / 8 + 1)) * roi_h;
    g_face_buf = (UBYTE *)malloc(roi_size);
    g_roi_buf = (UBYTE *)malloc(roi_size);
    mono_full_size = ((roi_w + 7) / 8) * roi_h;
    g_mono_face_buf = (UBYTE *)malloc(mono_full_size);
    g_mono_area_buf = (UBYTE *)malloc(mono_full_size);
    if (g_face_buf == NULL || g_roi_buf == NULL || g_mono_face_buf == NULL || g_mono_area_buf == NULL) {
        Debug("Failed to allocate ROI buffer\n");
        cleanup_and_exit(1);
    }

    EPD_IT8951_Clear_Refresh(g_dev_info, target_addr, INIT_Mode);

    while (1) {
        time_t t = time(NULL);
        struct tm tm_now;
        localtime_r(&t, &tm_now);

        if (tm_now.tm_min != last_min) {
            Paint_NewImage(g_full_buf, panel_w, panel_h, 0, BLACK);
            Paint_SelectImage(g_full_buf);
            apply_mode(epd_mode);
            Paint_SetBitsPerPixel(4);
            Paint_Clear(WHITE);

            Paint_NewImage(g_face_buf, roi_w, roi_h, 0, BLACK);
            Paint_SelectImage(g_face_buf);
            apply_mode(epd_mode);
            Paint_SetBitsPerPixel(4);
            draw_clock_face(roi_w, roi_h, &tm_now);

            // Build 1bpp cached face once per minute for low-flash second updates.
            Paint_NewImage(g_mono_face_buf, roi_w, roi_h, 0, BLACK);
            Paint_SelectImage(g_mono_face_buf);
            apply_mode(epd_mode);
            Paint_SetBitsPerPixel(1);
            draw_clock_face_mono(roi_w, roi_h, &tm_now);

            memcpy(g_roi_buf, g_face_buf, (size_t)roi_size);
            Paint_NewImage(g_roi_buf, roi_w, roi_h, 0, BLACK);
            Paint_SelectImage(g_roi_buf);
            apply_mode(epd_mode);
            Paint_SetBitsPerPixel(4);
            draw_second_hand(roi_w, roi_h, tm_now.tm_sec, 0x00, 0, 0);

            EPD_IT8951_4bp_Refresh(g_full_buf, 0, 0, panel_w, panel_h, false, target_addr, true);
            EPD_IT8951_4bp_Refresh(g_roi_buf, roi_x, roi_y, roi_w, roi_h, false, target_addr, true);

            last_min = tm_now.tm_min;
            last_sec = tm_now.tm_sec;
            Debug("Minute refresh at %02d:%02d\n", tm_now.tm_hour, tm_now.tm_min);
        }

        if (tm_now.tm_sec != last_sec) {
            int prev_sec = (last_sec < 0) ? tm_now.tm_sec : last_sec;
            UWORD cx = roi_w / 2, cy = roi_h / 2;
            UWORD xp, yp, xn, yn;
            int margin = 8;
            int x0, y0, x1, y1;
            UWORD bw, bh;

            second_hand_end(roi_w, roi_h, prev_sec, &xp, &yp);
            second_hand_end(roi_w, roi_h, tm_now.tm_sec, &xn, &yn);

            x0 = (int)cx; if ((int)xp < x0) x0 = xp; if ((int)xn < x0) x0 = xn; x0 -= margin;
            y0 = (int)cy; if ((int)yp < y0) y0 = yp; if ((int)yn < y0) y0 = yn; y0 -= margin;
            x1 = (int)cx; if ((int)xp > x1) x1 = xp; if ((int)xn > x1) x1 = xn; x1 += margin;
            y1 = (int)cy; if ((int)yp > y1) y1 = yp; if ((int)yn > y1) y1 = yn; y1 += margin;

            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 >= (int)roi_w) x1 = (int)roi_w - 1;
            if (y1 >= (int)roi_h) y1 = (int)roi_h - 1;

            align_bbox_for_1bpp(&x0, &x1, roi_w);
            bw = (UWORD)(x1 - x0 + 1);
            bh = (UWORD)(y1 - y0 + 1);

            {
                UWORD src_wb = (roi_w + 7) / 8;
                UWORD dst_wb = (bw + 7) / 8;
                for (UWORD yy = 0; yy < bh; yy++) {
                    memcpy(g_mono_area_buf + (size_t)yy * dst_wb,
                           g_mono_face_buf + (size_t)(y0 + yy) * src_wb + (x0 / 8),
                           dst_wb);
                }
            }
            Paint_NewImage(g_mono_area_buf, bw, bh, 0, BLACK);
            Paint_SelectImage(g_mono_area_buf);
            apply_mode(epd_mode);
            Paint_SetBitsPerPixel(1);
            draw_second_hand(roi_w, roi_h, tm_now.tm_sec, second_color, x0, y0);
            EPD_IT8951_1bp_Refresh(g_mono_area_buf, roi_x + (UWORD)x0, roi_y + (UWORD)y0, bw, bh, A2_Mode, target_addr, true);
            last_sec = tm_now.tm_sec;
        }

        DEV_Delay_ms(20);
    }

    return 0;
}
