#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stdint.h>

#include "font.h"

#define DISPLAY_W 160
#define DISPLAY_H 128

#define DISPLAY_BLACK 0x0000
#define DISPLAY_RED   0xF800
#define DISPLAY_WHITE 0xFFFF

#define DISPLAY_MAX_SCALE 2

#define DISPLAY_CHAR_W(scale) ((FONT_W + 1) * (scale))
#define DISPLAY_CHAR_H(scale) (FONT_H * (scale))

int display_init(void);
int display_rect(int x, int y, int w, int h, uint16_t color);
int display_text(int x, int y, const char *text, int scale, uint16_t fg, uint16_t bg);

#endif
