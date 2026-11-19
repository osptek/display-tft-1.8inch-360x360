/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <sys/cdefs.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "esp_log.h"

#include "esp_lcd_jd9855.h"

#define LCD_OPCODE_WRITE_CMD        (0x02ULL)
#define LCD_OPCODE_READ_CMD         (0x0BULL)
#define LCD_OPCODE_WRITE_COLOR      (0x32ULL)

static const char *TAG = "jd9855";

static esp_err_t panel_jd9855_del(esp_lcd_panel_t *panel);
static esp_err_t panel_jd9855_reset(esp_lcd_panel_t *panel);
static esp_err_t panel_jd9855_init(esp_lcd_panel_t *panel);
static esp_err_t panel_jd9855_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data);
static esp_err_t panel_jd9855_invert_color(esp_lcd_panel_t *panel, bool invert_color_data);
static esp_err_t panel_jd9855_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y);
static esp_err_t panel_jd9855_swap_xy(esp_lcd_panel_t *panel, bool swap_axes);
static esp_err_t panel_jd9855_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap);
static esp_err_t panel_jd9855_disp_on_off(esp_lcd_panel_t *panel, bool off);

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t fb_bits_per_pixel;
    uint8_t madctl_val; // save current value of LCD_CMD_MADCTL register
    uint8_t colmod_val; // save surrent value of LCD_CMD_COLMOD register
    const jd9855_lcd_init_cmd_t *init_cmds;
    uint16_t init_cmds_size;
    struct {
        unsigned int use_qspi_interface: 1;
        unsigned int reset_level: 1;
    } flags;
} jd9855_panel_t;

esp_err_t esp_lcd_new_panel_jd9855(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t ret = ESP_OK;
    jd9855_panel_t *jd9855 = NULL;
    jd9855 = calloc(1, sizeof(jd9855_panel_t));
    ESP_GOTO_ON_FALSE(jd9855, ESP_ERR_NO_MEM, err, TAG, "no mem for jd9855 panel");

    if (panel_dev_config->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << panel_dev_config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    switch (panel_dev_config->rgb_ele_order) {
    case LCD_RGB_ELEMENT_ORDER_RGB:
        jd9855->madctl_val = 0;
        break;
    case LCD_RGB_ELEMENT_ORDER_BGR:
        jd9855->madctl_val |= LCD_CMD_BGR_BIT;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported color element order");
        break;
    }

    switch (panel_dev_config->bits_per_pixel) {
    case 16: // RGB565
        jd9855->colmod_val = 0x55;
        jd9855->fb_bits_per_pixel = 16;
        break;
    case 18: // RGB666
        jd9855->colmod_val = 0x66;
        // each color component (R/G/B) should occupy the 6 high bits of a byte, which means 3 full bytes are required for a pixel
        jd9855->fb_bits_per_pixel = 24;
        break;
    default:
        ESP_GOTO_ON_FALSE(false, ESP_ERR_NOT_SUPPORTED, err, TAG, "unsupported pixel width");
        break;
    }

    jd9855->io = io;
    jd9855->reset_gpio_num = panel_dev_config->reset_gpio_num;
    jd9855->flags.reset_level = panel_dev_config->flags.reset_active_high;
    jd9855_vendor_config_t *vendor_config = (jd9855_vendor_config_t *)panel_dev_config->vendor_config;
    if (vendor_config) {
        jd9855->init_cmds = vendor_config->init_cmds;
        jd9855->init_cmds_size = vendor_config->init_cmds_size;
        jd9855->flags.use_qspi_interface = vendor_config->flags.use_qspi_interface;
    }
    jd9855->base.del = panel_jd9855_del;
    jd9855->base.reset = panel_jd9855_reset;
    jd9855->base.init = panel_jd9855_init;
    jd9855->base.draw_bitmap = panel_jd9855_draw_bitmap;
    jd9855->base.invert_color = panel_jd9855_invert_color;
    jd9855->base.set_gap = panel_jd9855_set_gap;
    jd9855->base.mirror = panel_jd9855_mirror;
    jd9855->base.swap_xy = panel_jd9855_swap_xy;
    jd9855->base.disp_on_off = panel_jd9855_disp_on_off;
    *ret_panel = &(jd9855->base);
    ESP_LOGD(TAG, "new jd9855 panel @%p", jd9855);

    ESP_LOGI(TAG, "LCD panel create success, version: %d.%d.%d", ESP_LCD_JD9855_VER_MAJOR, ESP_LCD_JD9855_VER_MINOR,
             ESP_LCD_JD9855_VER_PATCH);

    return ESP_OK;

err:
    if (jd9855) {
        if (panel_dev_config->reset_gpio_num >= 0) {
            gpio_reset_pin(panel_dev_config->reset_gpio_num);
        }
        free(jd9855);
    }
    return ret;
}

static esp_err_t tx_param(jd9855_panel_t *jd9855, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (jd9855->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        lcd_cmd |= LCD_OPCODE_WRITE_CMD << 24;
    }
    return esp_lcd_panel_io_tx_param(io, lcd_cmd, param, param_size);
}

static esp_err_t tx_color(jd9855_panel_t *jd9855, esp_lcd_panel_io_handle_t io, int lcd_cmd, const void *param, size_t param_size)
{
    if (jd9855->flags.use_qspi_interface) {
        lcd_cmd &= 0xff;
        lcd_cmd <<= 8;
        /* JD9855 QSPI 写显存：0x32 + 0x002C00，像素走 D0–D3 */
        lcd_cmd |= LCD_OPCODE_WRITE_COLOR << 24;
    }
    return esp_lcd_panel_io_tx_color(io, lcd_cmd, param, param_size);
}

static esp_err_t panel_jd9855_del(esp_lcd_panel_t *panel)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);

    if (jd9855->reset_gpio_num >= 0) {
        gpio_reset_pin(jd9855->reset_gpio_num);
    }
    ESP_LOGD(TAG, "del jd9855 panel @%p", jd9855);
    free(jd9855);
    return ESP_OK;
}

static esp_err_t panel_jd9855_reset(esp_lcd_panel_t *panel)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);
    esp_lcd_panel_io_handle_t io = jd9855->io;

    // Perform hardware reset
    if (jd9855->reset_gpio_num >= 0) {
        gpio_set_level(jd9855->reset_gpio_num, jd9855->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(jd9855->reset_gpio_num, !jd9855->flags.reset_level);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else { // Perform software reset
        ESP_RETURN_ON_ERROR(tx_param(jd9855, io, LCD_CMD_SWRESET, NULL, 0), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    return ESP_OK;
}

static esp_err_t panel_jd9855_init(esp_lcd_panel_t *panel)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);
    esp_lcd_panel_io_handle_t io = jd9855->io;
    const jd9855_lcd_init_cmd_t *init_cmds = jd9855->init_cmds;
    uint16_t init_cmds_size = jd9855->init_cmds_size;

    ESP_RETURN_ON_FALSE(init_cmds && init_cmds_size, ESP_ERR_INVALID_ARG, TAG, "JD9855 init commands are required");

    ESP_RETURN_ON_ERROR(tx_param(jd9855, io, LCD_CMD_MADCTL, (uint8_t[]) {
        jd9855->madctl_val,
    }, 1), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(jd9855, io, LCD_CMD_COLMOD, (uint8_t[]) {
        jd9855->colmod_val,
    }, 1), TAG, "send command failed");

    for (int i = 0; i < init_cmds_size; i++) {
        if (init_cmds[i].data_bytes > 0) {
            switch (init_cmds[i].cmd) {
            case LCD_CMD_MADCTL:
                jd9855->madctl_val = ((uint8_t *)init_cmds[i].data)[0];
                ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence", init_cmds[i].cmd);
                break;
            case LCD_CMD_COLMOD:
                jd9855->colmod_val = ((uint8_t *)init_cmds[i].data)[0];
                ESP_LOGW(TAG, "The %02Xh command has been used and will be overwritten by external initialization sequence", init_cmds[i].cmd);
                break;
            default:
                break;
            }
        }

        ESP_RETURN_ON_ERROR(tx_param(jd9855, io, init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes), TAG, "send command failed");
        vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }
    ESP_LOGD(TAG, "send init commands success");

    return ESP_OK;
}

static esp_err_t panel_jd9855_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);
    assert((x_start < x_end) && (y_start < y_end) && "start position must be smaller than end position");
    esp_lcd_panel_io_handle_t io = jd9855->io;

    x_start += jd9855->x_gap;
    x_end += jd9855->x_gap;
    y_start += jd9855->y_gap;
    y_end += jd9855->y_gap;

    // define an area of frame memory where MCU can access
    ESP_RETURN_ON_ERROR(tx_param(jd9855, io, LCD_CMD_CASET, (uint8_t[]) {
        (x_start >> 8) & 0xFF,
        x_start & 0xFF,
        ((x_end - 1) >> 8) & 0xFF,
        (x_end - 1) & 0xFF,
    }, 4), TAG, "send command failed");
    ESP_RETURN_ON_ERROR(tx_param(jd9855, io, LCD_CMD_RASET, (uint8_t[]) {
        (y_start >> 8) & 0xFF,
        y_start & 0xFF,
        ((y_end - 1) >> 8) & 0xFF,
        (y_end - 1) & 0xFF,
    }, 4), TAG, "send command failed");
    // transfer frame buffer
    size_t len = (x_end - x_start) * (y_end - y_start) * jd9855->fb_bits_per_pixel / 8;
    ESP_RETURN_ON_ERROR(tx_color(jd9855, io, LCD_CMD_RAMWR, color_data, len), TAG, "send color data failed");

    return ESP_OK;
}

static esp_err_t panel_jd9855_invert_color(esp_lcd_panel_t *panel, bool invert_color_data)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);
    esp_lcd_panel_io_handle_t io = jd9855->io;
    int command = 0;
    if (invert_color_data) {
        command = LCD_CMD_INVON;
    } else {
        command = LCD_CMD_INVOFF;
    }
    ESP_RETURN_ON_ERROR(tx_param(jd9855, io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_jd9855_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);
    esp_lcd_panel_io_handle_t io = jd9855->io;
    esp_err_t ret = ESP_OK;

    if (mirror_x) {
        jd9855->madctl_val |= BIT(6);
    } else {
        jd9855->madctl_val &= ~BIT(6);
    }
    if (mirror_y) {
        jd9855->madctl_val |= BIT(7);
    } else {
        jd9855->madctl_val &= ~BIT(7);
    }
    ESP_RETURN_ON_ERROR(tx_param(jd9855, io, LCD_CMD_MADCTL, (uint8_t[]) {
        jd9855->madctl_val
    }, 1), TAG, "send command failed");
    return ret;
}

static esp_err_t panel_jd9855_swap_xy(esp_lcd_panel_t *panel, bool swap_axes)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);
    esp_lcd_panel_io_handle_t io = jd9855->io;
    if (swap_axes) {
        jd9855->madctl_val |= LCD_CMD_MV_BIT;
    } else {
        jd9855->madctl_val &= ~LCD_CMD_MV_BIT;
    }
    ESP_RETURN_ON_ERROR(tx_param(jd9855, io, LCD_CMD_MADCTL, (uint8_t[]) {
        jd9855->madctl_val
    }, 1), TAG, "send command failed");
    return ESP_OK;
}

static esp_err_t panel_jd9855_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);
    jd9855->x_gap = x_gap;
    jd9855->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t panel_jd9855_disp_on_off(esp_lcd_panel_t *panel, bool on_off)
{
    jd9855_panel_t *jd9855 = __containerof(panel, jd9855_panel_t, base);
    esp_lcd_panel_io_handle_t io = jd9855->io;
    int command = 0;

    if (on_off) {
        command = LCD_CMD_DISPON;
    } else {
        command = LCD_CMD_DISPOFF;
    }
    ESP_RETURN_ON_ERROR(tx_param(jd9855, io, command, NULL, 0), TAG, "send command failed");
    return ESP_OK;
}
