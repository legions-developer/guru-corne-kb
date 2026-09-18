#!/usr/bin/env python3
"""Render the actual OLED drawing code with the prepared LVGL dependency on macOS.

Requires host clang and Pillow. Produces enlarged, inversion-aware previews;
this exercises drawing and rotation, not Bluetooth or SSD1306 hardware.
"""

import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image, ImageDraw


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "status_art.h"

LV_FONT_DECLARE(pixel_operator_mono_16);

static lv_color_t display_buffer[128 * 32];
static lv_color_t portrait_buffer[32 * 128];
static lv_color_t native_buffer[128 * 32];

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
    (void)area;
    (void)pixels;
    lv_disp_flush_ready(driver);
}

static void save_pgm(const char *directory, const char *name, const lv_color_t *pixels,
                     unsigned int width, unsigned int height) {
    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/%s.pgm", directory, name) < sizeof(path));
    FILE *output = fopen(path, "wb");
    assert(output);
    fprintf(output, "P5\n%u %u\n255\n", width, height);
    for (unsigned int i = 0; i < width * height; i++) {
        /* The panel's inversion-on makes black LVGL pixels illuminate. */
        fputc(pixels[i].full == lv_color_black().full ? 255 : 0, output);
    }
    assert(fclose(output) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    lv_init();
    static lv_disp_draw_buf_t buffer;
    static lv_disp_drv_t driver;
    lv_disp_draw_buf_init(&buffer, display_buffer, NULL, 128 * 32);
    lv_disp_drv_init(&driver);
    driver.hor_res = 128;
    driver.ver_res = 32;
    driver.draw_buf = &buffer;
    driver.flush_cb = flush;
    lv_disp_drv_register(&driver);

    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, portrait_buffer, 32, 128, LV_IMG_CF_TRUE_COLOR);

    static const struct {
        const char *name;
        uint8_t battery;
        bool usb;
        uint8_t profile;
        uint8_t layer;
        bool valid;
    } cases[] = {
        {"full-base", 100, false, 0, 0, true},
        {"low-lower", 7, false, 4, 1, true},
        {"usb-raise", 50, true, 2, 2, true},
        {"unsynced", 75, false, 0, 0, false},
        {"zero-raise", 0, false, 0, 2, true},
        {"charged-base", 99, false, 4, 0, true},
    };
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct corne_display_status state = {
            .profile = cases[i].profile,
            .layer = cases[i].layer,
            .valid = cases[i].valid,
        };
        corne_status_draw(canvas, cases[i].battery, cases[i].usb, &state);
        corne_status_rotate(portrait_buffer, native_buffer);
        for (int y = 0; y < 128; y++) {
            for (int x = 0; x < 32; x++) {
                assert(portrait_buffer[y * 32 + x].full ==
                       native_buffer[x * 128 + 127 - y].full);
            }
        }
        save_pgm(argv[1], cases[i].name, portrait_buffer, 32, 128);
        char native_name[64];
        snprintf(native_name, sizeof(native_name), "%s-native", cases[i].name);
        save_pgm(argv[1], native_name, native_buffer, 128, 32);
    }

    lv_point_t size;
    lv_txt_get_size(&size, "100%", &pixel_operator_mono_16, 0, 0, 32, LV_TEXT_FLAG_NONE);
    assert(size.x <= 32);
    assert(size.y == pixel_operator_mono_16.line_height);
    printf("Rendered six cases; rotation verified pixel-for-pixel; 100%% fits one %dpx row.\n",
           size.x);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/corne-oled-preview"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    runtime = Path(os.environ.get("CORNE_BUILD_ENV", "/tmp/corne-oled-build")).expanduser()
    lvgl = runtime / "modules/lib/gui/lvgl"
    fonts = repo / ".zmk/modules/zmk-nice-oled/boards/shields/nice_oled/src/fonts"
    sdk = Path("/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/"
               "Developer/SDKs/MacOSX26.5.sdk")
    if not sdk.is_dir():
        raise SystemExit(f"Required host SDK is unavailable: {sdk}")
    if not (lvgl / "lv_conf_template.h").is_file():
        raise SystemExit(f"Prepared LVGL dependency is unavailable: {lvgl}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="corne-oled-preview-build-") as directory:
        work = Path(directory)
        conf = (lvgl / "lv_conf_template.h").read_text()
        conf = conf.replace('#if 0 /*Set it to "1" to enable content*/',
                            '#if 1 /*Enabled for host drawing verification*/', 1)
        conf = conf.replace("#define LV_COLOR_DEPTH 16", "#define LV_COLOR_DEPTH 1", 1)
        (work / "lv_conf.h").write_text(conf)
        (work / "preview.c").write_text(HARNESS)
        sources = sorted((lvgl / "src").rglob("*.c"))
        subprocess.run([
            "clang", "-isysroot", str(sdk), "-std=c11", "-O1",
            "-DLV_CONF_INCLUDE_SIMPLE", "-I", str(work), "-I", str(lvgl),
            "-I", str(repo / "oled"), str(work / "preview.c"),
            str(repo / "oled/status_art.c"), str(fonts / "pixel_operator_mono_12.c"),
            str(fonts / "pixel_operator_mono_16.c"), *map(str, sources),
            "-o", str(work / "preview"),
        ], check=True)
        subprocess.run([str(work / "preview"), str(output)], check=True)

    labels = [
        ("full-base", "100% / BT1 / Base"),
        ("low-lower", "7% / BT5 / Lower"),
        ("usb-raise", "USB / BT3 / Raise"),
        ("unsynced", "75% / No sync"),
        ("zero-raise", "0% / BT1 / Raise"),
        ("charged-base", "99% / BT5 / Base"),
    ]
    sheet = Image.new("RGB", (6 * 184 + 24, 596), "#e8edf2")
    draw = ImageDraw.Draw(sheet)
    draw.text((24, 15), "Actual OLED renderer - physical display colors, 4x nearest-neighbor", fill="#162130")
    for index, (name, label) in enumerate(labels):
        with Image.open(output / f"{name}.pgm") as raw:
            portrait = raw.copy()
        portrait.save(output / f"{name}.png")
        enlarged = portrait.resize((128, 512), Image.Resampling.NEAREST)
        enlarged.save(output / f"{name}-4x.png")
        x = 24 + index * 184
        draw.text((x, 45), label, fill="#162130")
        sheet.paste(enlarged.convert("RGB"), (x, 68))
        with Image.open(output / f"{name}-native.pgm") as native:
            native.save(output / f"{name}-native.png")
    sheet.save(output / "contact-sheet.png")
    print(f"Preview: {output / 'contact-sheet.png'}")


if __name__ == "__main__":
    main()
