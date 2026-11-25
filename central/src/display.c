#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "display.h"
#include "font.h"

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

#define SSD1333_ADDR  0x3C
#define I2C_CTRL_CMD  0x00
#define I2C_CTRL_DATA 0x40
#define I2C_CHUNK     64
#define I2C_RETRIES   3
#define COL_OFFSET    8

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c21));
static const struct gpio_dt_spec reset_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), reset_gpios);

static int i2c_write_retry(const uint8_t *buf, size_t len)
{
	int err = 0;

	for (int attempt = 0; attempt < I2C_RETRIES; attempt++)
	{
		err = i2c_write(i2c_dev, buf, len, SSD1333_ADDR);

		if (!err)
		{
			return 0;
		}

		i2c_recover_bus(i2c_dev);
		k_msleep(2);
	}

	return err;
}

static int write_data(const uint8_t *data, size_t len)
{
	uint8_t buf[1 + I2C_CHUNK];

	while (len > 0)
	{
		size_t chunk = MIN(len, I2C_CHUNK);

		buf[0] = I2C_CTRL_DATA;
		memcpy(&buf[1], data, chunk);

		int err = i2c_write_retry(buf, chunk + 1);

		if (err)
		{
			return err;
		}

		data += chunk;
		len -= chunk;
	}

	return 0;
}

static int write_cmd(const uint8_t *cmd, size_t len)
{
	uint8_t buf[2] = {I2C_CTRL_CMD, cmd[0]};
	int err = i2c_write_retry(buf, sizeof(buf));

	if (err)
	{
		return err;
	}

	return write_data(&cmd[1], len - 1);
}

static int display_reset(void)
{
	if (!device_is_ready(i2c_dev))
	{
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&reset_gpio))
	{
		LOG_ERR("Reset GPIO not ready");
		return -ENODEV;
	}

	int err = gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_ACTIVE);

	if (err)
	{
		LOG_ERR("Reset GPIO configure failed (err %d)", err);
		return err;
	}

	k_msleep(10);
	gpio_pin_set_dt(&reset_gpio, 0);
	k_msleep(100);

	return 0;
}

static int set_window(int x, int y, int w, int h)
{
	if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > DISPLAY_W || y + h > DISPLAY_H)
	{
		return -EINVAL;
	}

	const uint8_t col_addr[] = {0x15, COL_OFFSET + x, COL_OFFSET + x + w - 1};
	const uint8_t row_addr[] = {0x75, y, y + h - 1};
	const uint8_t write_ram[] = {0x5C};

	int err = write_cmd(col_addr, sizeof(col_addr));

	if (!err)
	{
		err = write_cmd(row_addr, sizeof(row_addr));
	}

	if (!err)
	{
		err = write_cmd(write_ram, sizeof(write_ram));
	}

	return err;
}

int display_rect(int x, int y, int w, int h, uint16_t color)
{
	static uint8_t line[DISPLAY_W * 2];

	int err = set_window(x, y, w, h);

	if (err)
	{
		return err;
	}

	for (int i = 0; i < w; i++)
	{
		line[2 * i] = color >> 8;
		line[2 * i + 1] = color & 0xFF;
	}

	for (int row = 0; row < h; row++)
	{
		err = write_data(line, w * 2);

		if (err)
		{
			return err;
		}
	}

	return 0;
}

static int draw_char(int x, int y, char c, int scale, uint16_t fg, uint16_t bg)
{
	static uint8_t pixels[DISPLAY_CHAR_W(DISPLAY_MAX_SCALE) * DISPLAY_CHAR_H(DISPLAY_MAX_SCALE) * 2];
	const uint8_t *glyph = font_glyph(c);
	int w = DISPLAY_CHAR_W(scale);
	int h = DISPLAY_CHAR_H(scale);

	for (int py = 0; py < h; py++)
	{
		uint8_t row = glyph ? glyph[py / scale] : 0;

		for (int px = 0; px < w; px++)
		{
			int col = px / scale;
			bool on = col < FONT_W && ((row >> (FONT_W - 1 - col)) & 1);
			uint16_t color = on ? fg : bg;
			int i = 2 * (py * w + px);

			pixels[i] = color >> 8;
			pixels[i + 1] = color & 0xFF;
		}
	}

	int err = set_window(x, y, w, h);

	if (err)
	{
		return err;
	}

	return write_data(pixels, w * h * 2);
}

int display_text(int x, int y, const char *text, int scale, uint16_t fg, uint16_t bg)
{
	if (scale < 1 || scale > DISPLAY_MAX_SCALE)
	{
		return -EINVAL;
	}

	for (; *text; text++, x += DISPLAY_CHAR_W(scale))
	{
		int err = draw_char(x, y, *text, scale, fg, bg);

		if (err)
		{
			return err;
		}
	}

	return 0;
}

int display_init(void)
{
	static const uint8_t unlock[]        = {0xFD, 0x12};
	static const uint8_t display_off[]   = {0xAE};
	static const uint8_t remap[]         = {0xA0, 0x74};
	static const uint8_t start_line[]    = {0xA1, 0x00};
	static const uint8_t offset[]        = {0xA2, 0x30};
	static const uint8_t normal[]        = {0xA6};
	static const uint8_t phase_length[]  = {0xB1, 0x22};
	static const uint8_t display_clock[] = {0xB3, 0xD1};
	static const uint8_t contrast[]      = {0xC1, 0x45, 0x43, 0x90};
	static const uint8_t mux_ratio[]     = {0xCA, 0x7F};
	static const uint8_t display_on[]    = {0xAF};

	static const struct
	{
		const uint8_t *cmd;
		size_t len;
	} seq[] =
	{
		{unlock, sizeof(unlock)},
		{display_off, sizeof(display_off)},
		{remap, sizeof(remap)},
		{start_line, sizeof(start_line)},
		{offset, sizeof(offset)},
		{normal, sizeof(normal)},
		{phase_length, sizeof(phase_length)},
		{display_clock, sizeof(display_clock)},
		{contrast, sizeof(contrast)},
		{mux_ratio, sizeof(mux_ratio)},
		{display_on, sizeof(display_on)},
	};

	int err = display_reset();

	if (err)
	{
		return err;
	}

	for (size_t i = 0; i < ARRAY_SIZE(seq); i++)
	{
		err = write_cmd(seq[i].cmd, seq[i].len);

		if (err)
		{
			LOG_ERR("Init command 0x%02x failed (err %d)", seq[i].cmd[0], err);
			return err;
		}
	}

	err = display_rect(0, 0, DISPLAY_W, DISPLAY_H, DISPLAY_BLACK);

	if (err)
	{
		LOG_ERR("Clear failed (err %d)", err);
		return err;
	}

	LOG_INF("Display initialized");
	return 0;
}
