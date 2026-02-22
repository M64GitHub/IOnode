/**
 * @file i2c_display.cpp
 * @brief SSD1306 OLED display driver — text-only with 5x7 font + template engine
 *
 * Raw Wire.h communication, no external libraries.
 * Supports 128x64 (8 lines) and 128x32 (4 lines), 21 chars per line.
 * Template engine replaces {device_name} tokens with live sensor readings.
 */

#include "i2c_devices.h"
#include "devices.h"
#include <Wire.h>
#include <WiFi.h>

extern bool g_debug;
extern char cfg_device_name[32];

/*============================================================================
 * 5x7 ASCII Font (characters 32-127, 5 bytes each)
 *============================================================================*/

static const uint8_t FONT_5X7[] PROGMEM = {
    0x00,0x00,0x00,0x00,0x00, /* 32 ' ' */
    0x00,0x00,0x5F,0x00,0x00, /* 33 '!' */
    0x00,0x07,0x00,0x07,0x00, /* 34 '"' */
    0x14,0x7F,0x14,0x7F,0x14, /* 35 '#' */
    0x24,0x2A,0x7F,0x2A,0x12, /* 36 '$' */
    0x23,0x13,0x08,0x64,0x62, /* 37 '%' */
    0x36,0x49,0x55,0x22,0x50, /* 38 '&' */
    0x00,0x05,0x03,0x00,0x00, /* 39 ''' */
    0x00,0x1C,0x22,0x41,0x00, /* 40 '(' */
    0x00,0x41,0x22,0x1C,0x00, /* 41 ')' */
    0x08,0x2A,0x1C,0x2A,0x08, /* 42 '*' */
    0x08,0x08,0x3E,0x08,0x08, /* 43 '+' */
    0x00,0x50,0x30,0x00,0x00, /* 44 ',' */
    0x08,0x08,0x08,0x08,0x08, /* 45 '-' */
    0x00,0x60,0x60,0x00,0x00, /* 46 '.' */
    0x20,0x10,0x08,0x04,0x02, /* 47 '/' */
    0x3E,0x51,0x49,0x45,0x3E, /* 48 '0' */
    0x00,0x42,0x7F,0x40,0x00, /* 49 '1' */
    0x42,0x61,0x51,0x49,0x46, /* 50 '2' */
    0x21,0x41,0x45,0x4B,0x31, /* 51 '3' */
    0x18,0x14,0x12,0x7F,0x10, /* 52 '4' */
    0x27,0x45,0x45,0x45,0x39, /* 53 '5' */
    0x3C,0x4A,0x49,0x49,0x30, /* 54 '6' */
    0x01,0x71,0x09,0x05,0x03, /* 55 '7' */
    0x36,0x49,0x49,0x49,0x36, /* 56 '8' */
    0x06,0x49,0x49,0x29,0x1E, /* 57 '9' */
    0x00,0x36,0x36,0x00,0x00, /* 58 ':' */
    0x00,0x56,0x36,0x00,0x00, /* 59 ';' */
    0x00,0x08,0x14,0x22,0x41, /* 60 '<' */
    0x14,0x14,0x14,0x14,0x14, /* 61 '=' */
    0x41,0x22,0x14,0x08,0x00, /* 62 '>' */
    0x02,0x01,0x51,0x09,0x06, /* 63 '?' */
    0x32,0x49,0x79,0x41,0x3E, /* 64 '@' */
    0x7E,0x11,0x11,0x11,0x7E, /* 65 'A' */
    0x7F,0x49,0x49,0x49,0x36, /* 66 'B' */
    0x3E,0x41,0x41,0x41,0x22, /* 67 'C' */
    0x7F,0x41,0x41,0x22,0x1C, /* 68 'D' */
    0x7F,0x49,0x49,0x49,0x41, /* 69 'E' */
    0x7F,0x09,0x09,0x01,0x01, /* 70 'F' */
    0x3E,0x41,0x41,0x51,0x32, /* 71 'G' */
    0x7F,0x08,0x08,0x08,0x7F, /* 72 'H' */
    0x00,0x41,0x7F,0x41,0x00, /* 73 'I' */
    0x20,0x40,0x41,0x3F,0x01, /* 74 'J' */
    0x7F,0x08,0x14,0x22,0x41, /* 75 'K' */
    0x7F,0x40,0x40,0x40,0x40, /* 76 'L' */
    0x7F,0x02,0x04,0x02,0x7F, /* 77 'M' */
    0x7F,0x04,0x08,0x10,0x7F, /* 78 'N' */
    0x3E,0x41,0x41,0x41,0x3E, /* 79 'O' */
    0x7F,0x09,0x09,0x09,0x06, /* 80 'P' */
    0x3E,0x41,0x51,0x21,0x5E, /* 81 'Q' */
    0x7F,0x09,0x19,0x29,0x46, /* 82 'R' */
    0x46,0x49,0x49,0x49,0x31, /* 83 'S' */
    0x01,0x01,0x7F,0x01,0x01, /* 84 'T' */
    0x3F,0x40,0x40,0x40,0x3F, /* 85 'U' */
    0x1F,0x20,0x40,0x20,0x1F, /* 86 'V' */
    0x7F,0x20,0x18,0x20,0x7F, /* 87 'W' */
    0x63,0x14,0x08,0x14,0x63, /* 88 'X' */
    0x03,0x04,0x78,0x04,0x03, /* 89 'Y' */
    0x61,0x51,0x49,0x45,0x43, /* 90 'Z' */
    0x00,0x00,0x7F,0x41,0x41, /* 91 '[' */
    0x02,0x04,0x08,0x10,0x20, /* 92 '\' */
    0x41,0x41,0x7F,0x00,0x00, /* 93 ']' */
    0x04,0x02,0x01,0x02,0x04, /* 94 '^' */
    0x40,0x40,0x40,0x40,0x40, /* 95 '_' */
    0x00,0x01,0x02,0x04,0x00, /* 96 '`' */
    0x20,0x54,0x54,0x54,0x78, /* 97 'a' */
    0x7F,0x48,0x44,0x44,0x38, /* 98 'b' */
    0x38,0x44,0x44,0x44,0x20, /* 99 'c' */
    0x38,0x44,0x44,0x48,0x7F, /* 100 'd' */
    0x38,0x54,0x54,0x54,0x18, /* 101 'e' */
    0x08,0x7E,0x09,0x01,0x02, /* 102 'f' */
    0x08,0x54,0x54,0x54,0x3C, /* 103 'g' */
    0x7F,0x08,0x04,0x04,0x78, /* 104 'h' */
    0x00,0x44,0x7D,0x40,0x00, /* 105 'i' */
    0x20,0x40,0x44,0x3D,0x00, /* 106 'j' */
    0x00,0x7F,0x10,0x28,0x44, /* 107 'k' */
    0x00,0x41,0x7F,0x40,0x00, /* 108 'l' */
    0x7C,0x04,0x18,0x04,0x78, /* 109 'm' */
    0x7C,0x08,0x04,0x04,0x78, /* 110 'n' */
    0x38,0x44,0x44,0x44,0x38, /* 111 'o' */
    0x7C,0x14,0x14,0x14,0x08, /* 112 'p' */
    0x08,0x14,0x14,0x18,0x7C, /* 113 'q' */
    0x7C,0x08,0x04,0x04,0x08, /* 114 'r' */
    0x48,0x54,0x54,0x54,0x20, /* 115 's' */
    0x04,0x3F,0x44,0x40,0x20, /* 116 't' */
    0x3C,0x40,0x40,0x20,0x7C, /* 117 'u' */
    0x1C,0x20,0x40,0x20,0x1C, /* 118 'v' */
    0x3C,0x40,0x30,0x40,0x3C, /* 119 'w' */
    0x44,0x28,0x10,0x28,0x44, /* 120 'x' */
    0x0C,0x50,0x50,0x50,0x3C, /* 121 'y' */
    0x44,0x64,0x54,0x4C,0x44, /* 122 'z' */
    0x00,0x08,0x36,0x41,0x00, /* 123 '{' */
    0x00,0x00,0x7F,0x00,0x00, /* 124 '|' */
    0x00,0x41,0x36,0x08,0x00, /* 125 '}' */
    0x08,0x04,0x08,0x10,0x08, /* 126 '~' */
    0x7F,0x41,0x41,0x41,0x7F, /* 127 DEL (block) */
};

/*============================================================================
 * SSD1306 Low-Level Commands
 *============================================================================*/

static void ssd1306Cmd(uint8_t addr, uint8_t cmd) {
    Wire.beginTransmission(addr);
    Wire.write(0x00);  /* Co=0, D/C#=0 -> command */
    Wire.write(cmd);
    Wire.endTransmission();
}

static void ssd1306CmdList(uint8_t addr, const uint8_t *cmds, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(0x00);
    for (uint8_t i = 0; i < len; i++) Wire.write(cmds[i]);
    Wire.endTransmission();
}

/*============================================================================
 * SSD1306 Init / Deinit
 *============================================================================*/

bool ssd1306Init(uint8_t addr, uint8_t height) {
    if (!i2cActive()) return false;
    if (!i2cDetect(addr)) {
        Serial.printf("SSD1306: not found at 0x%02X\n", addr);
        return false;
    }

    uint8_t mux_ratio = (height == 32) ? 0x1F : 0x3F;
    uint8_t com_pins  = (height == 32) ? 0x02 : 0x12;

    /* Init sequence */
    static const uint8_t init_cmds[] = {
        0xAE,       /* display off */
        0xD5, 0x80, /* clock divide ratio */
        0xA8,       /* set multiplex (followed by mux_ratio below) */
    };
    ssd1306CmdList(addr, init_cmds, sizeof(init_cmds));
    ssd1306Cmd(addr, mux_ratio);

    ssd1306Cmd(addr, 0xD3); ssd1306Cmd(addr, 0x00); /* display offset = 0 */
    ssd1306Cmd(addr, 0x40);                          /* start line = 0 */
    ssd1306Cmd(addr, 0x8D); ssd1306Cmd(addr, 0x14); /* charge pump enable */
    ssd1306Cmd(addr, 0x20); ssd1306Cmd(addr, 0x00); /* horizontal addressing */
    ssd1306Cmd(addr, 0xA1);                          /* segment remap (flip X) */
    ssd1306Cmd(addr, 0xC8);                          /* COM scan reverse (flip Y) */
    ssd1306Cmd(addr, 0xDA); ssd1306Cmd(addr, com_pins); /* COM pins config */
    ssd1306Cmd(addr, 0x81); ssd1306Cmd(addr, 0xCF); /* contrast */
    ssd1306Cmd(addr, 0xD9); ssd1306Cmd(addr, 0xF1); /* pre-charge */
    ssd1306Cmd(addr, 0xDB); ssd1306Cmd(addr, 0x40); /* VCOMH deselect */
    ssd1306Cmd(addr, 0xA4);                          /* display from RAM */
    ssd1306Cmd(addr, 0xA6);                          /* normal display */
    ssd1306Cmd(addr, 0xAF);                          /* display on */

    ssd1306Clear(addr);

    Serial.printf("SSD1306: initialized at 0x%02X (%dx%d)\n", addr, 128, height);
    return true;
}

void ssd1306Deinit(uint8_t addr) {
    if (!i2cActive()) return;
    ssd1306Clear(addr);
    ssd1306Cmd(addr, 0xAE); /* display off */
    Serial.printf("SSD1306: deinitialized 0x%02X\n", addr);
}

void ssd1306Clear(uint8_t addr) {
    if (!i2cActive()) return;

    /* Set column and page range to cover full display */
    ssd1306Cmd(addr, 0x21); ssd1306Cmd(addr, 0); ssd1306Cmd(addr, 127);
    ssd1306Cmd(addr, 0x22); ssd1306Cmd(addr, 0); ssd1306Cmd(addr, 7);

    /* Send 128*8 = 1024 zero bytes (Wire has 128-byte buffer, chunk it) */
    for (int page = 0; page < 8; page++) {
        for (int chunk = 0; chunk < 128; chunk += 16) {
            Wire.beginTransmission(addr);
            Wire.write(0x40); /* data mode */
            for (int i = 0; i < 16; i++) Wire.write((uint8_t)0x00);
            Wire.endTransmission();
        }
    }
}

/*============================================================================
 * SSD1306 Text Rendering
 *============================================================================*/

#define SSD1306_MAX_COLS 21  /* 128 / 6 = 21 chars (5 pixel + 1 spacing) */

void ssd1306WriteText(uint8_t addr, uint8_t line, const char *text) {
    if (!i2cActive()) return;

    /* Set cursor to start of line (page) */
    ssd1306Cmd(addr, 0x21); ssd1306Cmd(addr, 0); ssd1306Cmd(addr, 127);
    ssd1306Cmd(addr, 0x22); ssd1306Cmd(addr, line); ssd1306Cmd(addr, line);

    /* Render up to 21 characters */
    int col = 0;
    for (int i = 0; text[i] && col < SSD1306_MAX_COLS; i++, col++) {
        uint8_t c = (uint8_t)text[i];
        if (c < 32 || c > 127) c = 32;

        const uint8_t *glyph = &FONT_5X7[(c - 32) * 5];

        Wire.beginTransmission(addr);
        Wire.write(0x40);
        for (int j = 0; j < 5; j++) Wire.write(pgm_read_byte(&glyph[j]));
        Wire.write((uint8_t)0x00); /* 1-pixel spacing */
        Wire.endTransmission();
    }

    /* Fill remaining columns with blanks */
    int remaining = (SSD1306_MAX_COLS - col) * 6;
    while (remaining > 0) {
        int chunk = remaining > 16 ? 16 : remaining;
        Wire.beginTransmission(addr);
        Wire.write(0x40);
        for (int i = 0; i < chunk; i++) Wire.write((uint8_t)0x00);
        Wire.endTransmission();
        remaining -= chunk;
    }
}

/*============================================================================
 * Template Engine — {device_name} token replacement
 *============================================================================*/

static void templateExpand(const char *tmpl, char *out, int out_len) {
    int w = 0;
    const char *p = tmpl;

    while (*p && w < out_len - 1) {
        if (*p == '{') {
            /* Find closing brace */
            const char *end = strchr(p + 1, '}');
            if (!end) { out[w++] = *p++; continue; }

            /* Extract token name */
            int tlen = end - p - 1;
            if (tlen <= 0 || tlen >= 32) { out[w++] = *p++; continue; }
            char token[32];
            memcpy(token, p + 1, tlen);
            token[tlen] = '\0';

            /* Special tokens */
            if (strcmp(token, "ip") == 0) {
                w += snprintf(out + w, out_len - w, "%s",
                              WiFi.localIP().toString().c_str());
            } else if (strcmp(token, "heap") == 0) {
                w += snprintf(out + w, out_len - w, "%u", ESP.getFreeHeap());
            } else if (strcmp(token, "uptime") == 0) {
                unsigned long secs = millis() / 1000;
                unsigned long h = secs / 3600;
                unsigned long m = (secs % 3600) / 60;
                w += snprintf(out + w, out_len - w, "%luh%02lum", h, m);
            } else if (strcmp(token, "name") == 0) {
                w += snprintf(out + w, out_len - w, "%s", cfg_device_name);
            } else {
                /* Look up device by name */
                Device *dev = deviceFind(token);
                if (dev && deviceIsSensor(dev->kind)) {
                    float val = deviceReadSensor(dev);
                    w += snprintf(out + w, out_len - w, "%.1f", val);
                } else if (dev && deviceIsActuator(dev->kind)) {
                    w += snprintf(out + w, out_len - w, "%d", dev->last_value);
                } else {
                    /* Unknown token — pass through */
                    w += snprintf(out + w, out_len - w, "?%s", token);
                }
            }

            p = end + 1;
        } else {
            out[w++] = *p++;
        }
    }
    out[w] = '\0';
}

void ssd1306RenderTemplate(uint8_t addr, const char *tmpl, uint8_t height) {
    if (!i2cActive() || !tmpl || !tmpl[0]) return;

    /* Expand template tokens */
    char expanded[256];
    templateExpand(tmpl, expanded, sizeof(expanded));

    int max_lines = (height == 32) ? 4 : 8;

    /* Split by \n and render each line */
    char *line_start = expanded;
    int line_num = 0;

    while (line_start && *line_start && line_num < max_lines) {
        /* Find next newline */
        char *nl = strchr(line_start, '\n');
        if (nl) *nl = '\0';

        ssd1306WriteText(addr, line_num, line_start);
        line_num++;

        if (nl) line_start = nl + 1;
        else break;
    }

    /* Clear remaining lines */
    while (line_num < max_lines) {
        ssd1306WriteText(addr, line_num, "");
        line_num++;
    }
}

/*============================================================================
 * Display Poll — refresh all SSD1306 displays
 *============================================================================*/

void displayPoll() {
    static uint32_t last_poll = 0;
    uint32_t now = millis();
    if (now - last_poll < 5000) return;
    last_poll = now;

    Device *devs = deviceGetAll();
    for (int i = 0; i < MAX_DEVICES; i++) {
        Device *d = &devs[i];
        if (!d->used || d->kind != DEV_ACTUATOR_SSD1306) continue;
        if (d->i2c_addr == 0) continue;
        if (d->disp_template[0] == '\0') continue;

        uint8_t height = (d->pin == 1) ? 32 : 64;
        ssd1306RenderTemplate(d->i2c_addr, d->disp_template, height);
    }
}
