#include <stdio.h>
#include "status_art.h"

LV_FONT_DECLARE(pixel_operator_mono_12);
LV_FONT_DECLARE(pixel_operator_mono_16);

/* The Corne SSD1306 devicetree sets inversion-on. Match nice_oled's normal
 * palette: black pixels light up, white pixels remain dark on the panel.
 */
static void draw_text(lv_obj_t *canvas, int y, const char *text, const lv_font_t *font,
                      lv_color_t color) {
    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.font = font;
    label.color = color;
    label.align = LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(canvas, 0, y, CORNE_OLED_WIDTH, &label, text);
}

void corne_status_draw(lv_obj_t *portrait, uint8_t battery, bool usb_powered,
                       const struct corne_display_status *status) {
    const lv_color_t lit = lv_color_black();
    const lv_color_t dark = lv_color_white();
    lv_canvas_fill_bg(portrait, dark, LV_OPA_COVER);

    char text[8];
    if (usb_powered) {
        snprintf(text, sizeof(text), "USB");
    } else {
        snprintf(text, sizeof(text), "%u%%", (unsigned int)battery);
    }
    draw_text(portrait, 0, text, &pixel_operator_mono_16, lit);

    if (status->valid) {
        snprintf(text, sizeof(text), "BT%u", (unsigned int)status->profile + 1);
    } else {
        snprintf(text, sizeof(text), "BT-");
    }
    draw_text(portrait, 18, text, &pixel_operator_mono_16, lit);

    /* Display order is lower/base/raise; the underlying layer IDs stay 1/0/2.
     * Only the highest active layer is filled, matching ZMK's key lookup
     * precedence when both layer keys are held.
     */
    static const struct {
        uint8_t layer;
        const char *label;
    } rows[] = {{1, "L"}, {0, "B"}, {2, "R"}};
    for (unsigned int row = 0; row < 3; row++) {
        const bool active = status->valid && status->layer == rows[row].layer;
        const int top = 42 + (int)row * 29;
        lv_draw_rect_dsc_t circle;
        lv_draw_rect_dsc_init(&circle);
        circle.radius = LV_RADIUS_CIRCLE;
        circle.bg_color = active ? lit : dark;
        circle.bg_opa = LV_OPA_COVER;
        circle.border_color = lit;
        circle.border_width = 1;
        lv_canvas_draw_rect(portrait, 4, top, 24, 24, &circle);
        draw_text(portrait, top + 6, rows[row].label, &pixel_operator_mono_12,
                  active ? dark : lit);
    }
}

void corne_status_rotate(const lv_color_t *portrait, lv_color_t *native) {
    /* The physical panel is mounted vertically. Use two exact-size buffers
     * instead of the upstream square canvas and square rotation scratch area.
     */
    for (int y = 0; y < CORNE_OLED_WIDTH; y++) {
        for (int x = 0; x < CORNE_OLED_HEIGHT; x++) {
            native[y * CORNE_OLED_HEIGHT + x] =
                portrait[(CORNE_OLED_HEIGHT - 1 - x) * CORNE_OLED_WIDTH + y];
        }
    }
}
