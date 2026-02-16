#include "../lib/Config/DEV_Config.h"
#include "../lib/e-Paper/EPD_IT8951.h"

#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IT8951_Dev_Info g_dev_info = {0, 0};

static double monotonic_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void wait_for_display_ready(void)
{
    while (DEV_Digital_Read(EPD_BUSY_PIN) == 0) {
        DEV_Delay_ms(1);
    }
}

static void cleanup_and_exit(int code)
{
    DEV_Module_Exit();
    exit(code);
}

static void signal_handler(int signo)
{
    (void)signo;
    cleanup_and_exit(0);
}

static int read_pgm_token(FILE *fp, char *token, size_t token_size)
{
    int c = 0;
    size_t idx = 0;

    do {
        c = fgetc(fp);
        if (c == '#') {
            do {
                c = fgetc(fp);
            } while (c != '\n' && c != EOF);
        }
    } while ((c == ' ' || c == '\t' || c == '\r' || c == '\n') && c != EOF);

    if (c == EOF) {
        return -1;
    }

    while (c != EOF && c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '#') {
        if (idx + 1 >= token_size) {
            return -1;
        }
        token[idx++] = (char)c;
        c = fgetc(fp);
    }

    token[idx] = '\0';

    if (c == '#') {
        do {
            c = fgetc(fp);
        } while (c != '\n' && c != EOF);
    }

    return 0;
}

static int load_pgm_grayscale(const char *path, UBYTE **pixels, UWORD *width, UWORD *height)
{
    FILE *fp = fopen(path, "rb");
    char token[64];
    bool is_ascii = false;
    long w = 0;
    long h = 0;
    long maxval = 0;
    size_t pixel_count = 0;
    UBYTE *out = NULL;
    int c = 0;

    if (fp == NULL) {
        Debug("Failed to open input file: %s\n", path);
        return -1;
    }

    if (read_pgm_token(fp, token, sizeof(token)) != 0) {
        fclose(fp);
        return -1;
    }
    if (strcmp(token, "P5") == 0) {
        is_ascii = false;
    } else if (strcmp(token, "P2") == 0) {
        is_ascii = true;
    } else {
        Debug("Input must be PGM (P2 ASCII or P5 binary)\n");
        fclose(fp);
        return -1;
    }

    if (read_pgm_token(fp, token, sizeof(token)) != 0) {
        fclose(fp);
        return -1;
    }
    w = strtol(token, NULL, 10);

    if (read_pgm_token(fp, token, sizeof(token)) != 0) {
        fclose(fp);
        return -1;
    }
    h = strtol(token, NULL, 10);

    if (read_pgm_token(fp, token, sizeof(token)) != 0) {
        fclose(fp);
        return -1;
    }
    maxval = strtol(token, NULL, 10);

    if (w <= 0 || h <= 0 || w > 65535 || h > 65535 || maxval <= 0 || maxval > 65535) {
        Debug("Invalid PGM header values\n");
        fclose(fp);
        return -1;
    }

    do {
        c = fgetc(fp);
        if (c == '#') {
            do {
                c = fgetc(fp);
            } while (c != '\n' && c != EOF);
        }
    } while (c == ' ' || c == '\t' || c == '\r' || c == '\n');
    if (c == EOF) {
        fclose(fp);
        return -1;
    }
    ungetc(c, fp);

    pixel_count = (size_t)w * (size_t)h;
    out = (UBYTE *)malloc(pixel_count);
    if (out == NULL) {
        Debug("Out of memory while loading image\n");
        fclose(fp);
        return -1;
    }

    if (is_ascii) {
        for (size_t i = 0; i < pixel_count; i++) {
            long sample = 0;
            if (read_pgm_token(fp, token, sizeof(token)) != 0) {
                Debug("PGM file is shorter than expected\n");
                free(out);
                fclose(fp);
                return -1;
            }
            sample = strtol(token, NULL, 10);
            if (sample < 0) {
                sample = 0;
            } else if (sample > maxval) {
                sample = maxval;
            }

            if (maxval == 255) {
                out[i] = (UBYTE)sample;
            } else {
                out[i] = (UBYTE)((sample * 255 + (maxval / 2)) / maxval);
            }
        }
    } else {
        if (maxval <= 255) {
            UBYTE *tmp = (UBYTE *)malloc(pixel_count);
            if (tmp == NULL) {
                free(out);
                fclose(fp);
                return -1;
            }

            if (fread(tmp, 1, pixel_count, fp) != pixel_count) {
                Debug("PGM file is shorter than expected\n");
                free(tmp);
                free(out);
                fclose(fp);
                return -1;
            }

            for (size_t i = 0; i < pixel_count; i++) {
                if (maxval == 255) {
                    out[i] = tmp[i];
                } else {
                    out[i] = (UBYTE)((tmp[i] * 255 + (maxval / 2)) / maxval);
                }
            }

            free(tmp);
        } else {
            for (size_t i = 0; i < pixel_count; i++) {
                int hi = fgetc(fp);
                int lo = fgetc(fp);
                unsigned int sample;
                if (hi == EOF || lo == EOF) {
                    Debug("PGM file is shorter than expected\n");
                    free(out);
                    fclose(fp);
                    return -1;
                }
                sample = ((unsigned int)hi << 8) | (unsigned int)lo;
                out[i] = (UBYTE)((sample * 255 + (maxval / 2)) / maxval);
            }
        }
    }

    fclose(fp);
    *pixels = out;
    *width = (UWORD)w;
    *height = (UWORD)h;
    return 0;
}

static UWORD effective_panel_width(UWORD panel_width, const char *lut_version)
{
    if (strcmp(lut_version, "M641") == 0 || strcmp(lut_version, "M841_TFAB512") == 0) {
        return panel_width - (panel_width % 32);
    }
    return panel_width;
}

static void pack_gray8_rows_to_4bpp(
    const UBYTE *src_gray8,
    UWORD src_stride,
    UBYTE *dst_4bpp,
    UWORD width,
    UWORD rows)
{
    UWORD bytes_per_row_4bpp = (width + 1) / 2;
    for (UWORD y = 0; y < rows; y++) {
        const UBYTE *src_row = src_gray8 + (size_t)y * src_stride;
        UBYTE *dst_row = dst_4bpp + (size_t)y * bytes_per_row_4bpp;
        for (UWORD x = 0; x < width; x += 2) {
            UBYTE even_gray = src_row[x] & 0xF0;
            UBYTE odd_gray = (x + 1 < width) ? (src_row[x + 1] & 0xF0) : 0xF0;
            // Match GUI_Paint 4bpp packing: even pixel in low nibble, odd pixel in high nibble.
            dst_row[x / 2] = odd_gray | (even_gray >> 4);
        }
    }
}

int main(int argc, char *argv[])
{
    const UWORD incremental_lines = 32;
    const UWORD full_settle_ms = 1500;
    const UWORD incremental_settle_ms = 0;
    const UWORD sleep_exit_delay_ms = 500;
    int argi = 1;
    bool incremental_mode = false;
    UWORD vcom = 0;
    int epd_mode = 0;
    UDOUBLE target_addr = 0;
    UWORD panel_w = 0;
    UWORD panel_h = 0;
    UWORD draw_w = 0;
    UWORD draw_h = 0;
    UWORD img_w = 0;
    UWORD img_h = 0;
    UBYTE *img_pixels = NULL;
    UBYTE *full_4bpp_buf = NULL;
    UBYTE *chunk_4bpp_buf = NULL;
    double temp_vcom = 0.0;
    double wall_start_s = 0.0;
    double wall_end_s = 0.0;
    double update_start_s = 0.0;
    double update_end_s = 0.0;
    double init_end_s = 0.0;
    double clear_end_s = 0.0;
    double load_end_s = 0.0;
    double prep_end_s = 0.0;

    signal(SIGINT, signal_handler);

    if (argi < argc && strcmp(argv[argi], "--incremental") == 0) {
        incremental_mode = true;
        argi++;
    }

    if ((argc - argi) < 2 || (argc - argi) > 3) {
        Debug("Usage: sudo ./epd_pgm [--incremental] <VCOM> <image.pgm> [mode]\n");
        Debug("Example: sudo ./epd_pgm --incremental -2.51 ./pic/input.pgm 0\n");
        return 1;
    }

    wall_start_s = monotonic_seconds();

    if (DEV_Module_Init() != 0) {
        return 1;
    }

    sscanf(argv[argi], "%lf", &temp_vcom);
    vcom = (UWORD)(fabs(temp_vcom) * 1000);
    if ((argc - argi) == 3) {
        epd_mode = atoi(argv[argi + 2]);
    }

    g_dev_info = EPD_IT8951_Init(vcom);
    panel_w = effective_panel_width(g_dev_info.Panel_W, (const char *)g_dev_info.LUT_Version);
    panel_h = g_dev_info.Panel_H;
    target_addr = g_dev_info.Memory_Addr_L | (g_dev_info.Memory_Addr_H << 16);
    init_end_s = monotonic_seconds();

    // Clear first to minimize ghosting from previously displayed content.
    EPD_IT8951_Clear_Refresh(g_dev_info, target_addr, INIT_Mode);
    clear_end_s = monotonic_seconds();

    if (load_pgm_grayscale(argv[argi + 1], &img_pixels, &img_w, &img_h) != 0) {
        cleanup_and_exit(1);
    }
    load_end_s = monotonic_seconds();
    Debug("Loaded PGM: %ux%u\n", img_w, img_h);

    draw_w = (img_w < panel_w) ? img_w : panel_w;
    draw_h = (img_h < panel_h) ? img_h : panel_h;
    Debug("Panel: %ux%u, draw area: %ux%u at (0,0)\n", panel_w, panel_h, draw_w, draw_h);
    prep_end_s = monotonic_seconds();
    if (epd_mode != 0) {
        Debug("Note: mode %d currently has no effect in direct PGM path\n", epd_mode);
    }

    update_start_s = monotonic_seconds();
    if (incremental_mode) {
        UWORD bytes_per_row_4bpp = (draw_w + 1) / 2;
        size_t chunk_buf_size = (size_t)bytes_per_row_4bpp * incremental_lines;

        Debug("Refresh mode: incremental 4bpp packed (%u lines/chunk)\n", incremental_lines);
        chunk_4bpp_buf = (UBYTE *)malloc(chunk_buf_size);
        if (chunk_4bpp_buf == NULL) {
            Debug("Failed to allocate incremental chunk buffer\n");
            free(img_pixels);
            img_pixels = NULL;
            cleanup_and_exit(1);
        }

        for (UWORD y0 = 0; y0 < draw_h; y0 += incremental_lines) {
            UWORD chunk_h = (y0 + incremental_lines <= draw_h) ? incremental_lines : (draw_h - y0);

            pack_gray8_rows_to_4bpp(img_pixels + (size_t)y0 * img_w, img_w, chunk_4bpp_buf, draw_w, chunk_h);

            EPD_IT8951_4bp_Refresh(chunk_4bpp_buf, 0, y0, draw_w, chunk_h, false, target_addr, true);
            if (((y0 + chunk_h) % 32 == 0) || (y0 + chunk_h == draw_h)) {
                double pct = ((double)(y0 + chunk_h) * 100.0) / (double)draw_h;
                Debug("Progress: %u/%u lines (%.1f%%)\n", y0 + chunk_h, draw_h, pct);
            }
        }

        free(chunk_4bpp_buf);
        chunk_4bpp_buf = NULL;
    } else {
        UWORD bytes_per_row_4bpp = (draw_w + 1) / 2;
        size_t full_buf_size = (size_t)bytes_per_row_4bpp * draw_h;

        Debug("Refresh mode: full frame packed 4bpp\n");
        full_4bpp_buf = (UBYTE *)malloc(full_buf_size);
        if (full_4bpp_buf == NULL) {
            Debug("Failed to allocate full-frame 4bpp buffer\n");
            free(img_pixels);
            img_pixels = NULL;
            cleanup_and_exit(1);
        }

        pack_gray8_rows_to_4bpp(img_pixels, img_w, full_4bpp_buf, draw_w, draw_h);
        EPD_IT8951_4bp_Refresh(full_4bpp_buf, 0, 0, draw_w, draw_h, false, target_addr, true);
        free(full_4bpp_buf);
        full_4bpp_buf = NULL;
    }

    // Ensure display update completes on panel before exiting module I/O.
    wait_for_display_ready();
    DEV_Delay_ms(incremental_mode ? incremental_settle_ms : full_settle_ms);
    update_end_s = monotonic_seconds();

    free(img_pixels);
    img_pixels = NULL;

    wall_end_s = monotonic_seconds();
    Debug("Init time: %.3f seconds\n", init_end_s - wall_start_s);
    Debug("Clear time: %.3f seconds\n", clear_end_s - init_end_s);
    Debug("PGM load time: %.3f seconds\n", load_end_s - clear_end_s);
    Debug("Pre-update prep time: %.3f seconds\n", prep_end_s - load_end_s);
    Debug("Image update time: %.3f seconds\n", update_end_s - update_start_s);
    Debug("Total wall clock (init->finish): %.3f seconds\n", wall_end_s - wall_start_s);

    EPD_IT8951_Sleep();
    DEV_Delay_ms(sleep_exit_delay_ms);
    DEV_Module_Exit();
    return 0;
}
