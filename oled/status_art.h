#pragma once

#include <lvgl.h>
#include "status_relay.h"

#define CORNE_OLED_WIDTH 32
#define CORNE_OLED_HEIGHT 128

void corne_status_draw(lv_obj_t *portrait, uint8_t battery, bool usb_powered,
                       const struct corne_display_status *status);
void corne_status_rotate(const lv_color_t *portrait, lv_color_t *native);
