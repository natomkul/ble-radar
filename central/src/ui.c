#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "display.h"
#include "ui.h"

LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

#define UI_LINES      3
#define LABEL_X       4
#define LABEL_SCALE   1
#define VALUE_X       48
#define VALUE_Y_SHIFT 4
#define VALUE_SCALE   2
#define VALUE_CHARS   9

#define UI_STACK_SIZE 1536
#define UI_PRIORITY   10

struct ui_values
{
	int ps;
	int ifft;
	int music;
	bool music_valid;
};

K_MSGQ_DEFINE(ui_msgq, sizeof(struct ui_values), 1, 4);

static const char *const labels[UI_LINES] = {"PS:", "IFFT:", "MUSIC:"};
static const int line_y[UI_LINES] = {16, 56, 96};

static char shown[UI_LINES][VALUE_CHARS + 1];
static uint16_t shown_color[UI_LINES];

static void draw_value(int line, const char *text, uint16_t color)
{
	char padded[VALUE_CHARS + 1];

	snprintf(padded, sizeof(padded), "%*s", VALUE_CHARS, text);

	if (color != shown_color[line])
	{
		memset(shown[line], 0, sizeof(shown[line]));
		shown_color[line] = color;
	}

	for (int i = 0; i < VALUE_CHARS; i++)
	{
		if (padded[i] == shown[line][i])
		{
			continue;
		}

		char c[2] = {padded[i], '\0'};
		int err = display_text(VALUE_X + i * DISPLAY_CHAR_W(VALUE_SCALE),
				       line_y[line] - VALUE_Y_SHIFT, c, VALUE_SCALE, color,
				       DISPLAY_BLACK);

		if (err)
		{
			LOG_WRN("Draw failed (err %d)", err);
			return;
		}

		shown[line][i] = padded[i];
	}
}

static void draw_cm(int line, int cm)
{
	char text[VALUE_CHARS + 1];

	snprintf(text, sizeof(text), "%d cm", cm);
	draw_value(line, text, DISPLAY_WHITE);
}

static void ui_thread(void *p1, void *p2, void *p3)
{
	if (display_init())
	{
		return;
	}

	for (int line = 0; line < UI_LINES; line++)
	{
		display_text(LABEL_X, line_y[line], labels[line], LABEL_SCALE, DISPLAY_WHITE,
			     DISPLAY_BLACK);
		draw_value(line, "-", DISPLAY_WHITE);
	}

	struct ui_values v;

	while (true)
	{
		k_msgq_get(&ui_msgq, &v, K_FOREVER);

		draw_cm(0, v.ps);
		draw_cm(1, v.ifft);

		if (v.music_valid)
		{
			draw_cm(2, v.music);
		}
		else
		{
			draw_value(2, "n/a", DISPLAY_RED);
		}
	}
}

K_THREAD_DEFINE(ui_tid, UI_STACK_SIZE, ui_thread, NULL, NULL, NULL, UI_PRIORITY, 0, 0);

void ui_show(int ps_cm, int ifft_cm, int music_cm, bool music_valid)
{
	struct ui_values v =
	{
		.ps = ps_cm,
		.ifft = ifft_cm,
		.music = music_cm,
		.music_valid = music_valid,
	};

	k_msgq_purge(&ui_msgq);
	k_msgq_put(&ui_msgq, &v, K_NO_WAIT);
}
