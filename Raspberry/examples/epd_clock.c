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
static UBYTE *g_roi_buf = NULL;

static void cleanup_and_exit(int code)
{
    if (g_full_buf != NULL) {
        free(g_full_buf);
        g_full_buf = NULL;
    }
    if (g_roi_buf != NULL) {
        free(g_roi_buf);
        g_roi_buf = NULL;
    }
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

static void draw_clock_scene(UWORD w, UWORD h, struct tm *tm_now, UBYTE second_color)
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
    double min_deg = ((tm_now->tm_min + tm_now->tm_sec / 60.0) * 6.0) - 90.0;
    double sec_deg = (tm_now->tm_sec * 6.0) - 90.0;

    hand_end(cx, cy, hour_deg, radius * 55 / 100, &x2, &y2);
    Paint_DrawLine(cx, cy, x2, y2, 0x00, DOT_PIXEL_3X3, LINE_STYLE_SOLID);

    hand_end(cx, cy, min_deg, radius * 75 / 100, &x2, &y2);
    Paint_DrawLine(cx, cy, x2, y2, 0x10, DOT_PIXEL_2X2, LINE_STYLE_SOLID);

    hand_end(cx, cy, sec_deg, radius * 90 / 100, &x2, &y2);
    Paint_DrawLine(cx, cy, x2, y2, second_color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);

    Paint_DrawCircle(cx, cy, 4, 0x00, DOT_PIXEL_1X1, DRAW_FILL_FULL);
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
    int last_min = -1;
    int last_sec = -1;
    const UBYTE sec_phase[3] = {0xC0, 0x00, 0xF0};

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
    g_roi_buf = (UBYTE *)malloc(roi_size);
    if (g_roi_buf == NULL) {
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

            Paint_NewImage(g_roi_buf, roi_w, roi_h, 0, BLACK);
            Paint_SelectImage(g_roi_buf);
            apply_mode(epd_mode);
            Paint_SetBitsPerPixel(4);
            draw_clock_scene(roi_w, roi_h, &tm_now, 0x00);

            EPD_IT8951_4bp_Refresh(g_full_buf, 0, 0, panel_w, panel_h, false, target_addr, true);
            EPD_IT8951_4bp_Refresh(g_roi_buf, roi_x, roi_y, roi_w, roi_h, false, target_addr, true);

            last_min = tm_now.tm_min;
            last_sec = tm_now.tm_sec;
            Debug("Minute refresh at %02d:%02d\n", tm_now.tm_hour, tm_now.tm_min);
        }

        if (tm_now.tm_sec != last_sec) {
            for (int p = 0; p < 3; p++) {
                Paint_NewImage(g_roi_buf, roi_w, roi_h, 0, BLACK);
                Paint_SelectImage(g_roi_buf);
                apply_mode(epd_mode);
                Paint_SetBitsPerPixel(4);
                draw_clock_scene(roi_w, roi_h, &tm_now, sec_phase[p]);
                EPD_IT8951_4bp_Refresh(g_roi_buf, roi_x, roi_y, roi_w, roi_h, false, target_addr, true);
                if (p < 2) {
                    DEV_Delay_ms(150);
                }
            }
            last_sec = tm_now.tm_sec;
        }

        DEV_Delay_ms(20);
    }

    return 0;
}
