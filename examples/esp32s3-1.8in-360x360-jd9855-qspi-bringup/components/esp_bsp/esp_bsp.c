/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_rom_gpio.h"
#include "esp_lcd_jd9855.h"
#include "esp_lcd_touch_cst816.h"
#include "bsp_err_check.h"

#include "lv_port.h"
#include "display.h"
#include "esp_bsp.h"

static const char *TAG = "example";

static const jd9855_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xDE, (uint8_t []){0x00}, 1, 0},
    {0xDF, (uint8_t []){0x98,0x55}, 2, 0},
    {0xCE, (uint8_t []){0x0D,0x00}, 2, 0},
    {0xB2, (uint8_t []){0x1F}, 1, 0},
    {0xB7, (uint8_t []){0x01,0x2D,0x01,0x55}, 4, 0},
    {0xBB, (uint8_t []){0x1B,0x64,0xC4,0x0E,0x3E,0xF5}, 6, 0},
    {0xBC, (uint8_t []){0x03,0x22,0xF3,0xC0}, 4, 0},
    {0xC0, (uint8_t []){0x22,0xA4}, 2, 0},
    {0xC3, (uint8_t []){0x00,0x02,0x2A,0x0B,0x08,0x48,0x08,0x04,0x62,0x30,0x30}, 11, 0},
    {0xC4, (uint8_t []){0x40,0x00,0xAD,0x68,0x43,0x07,0x04,0x16,0x43,0x07,0x04}, 11, 0},
    {0xC8, (uint8_t []){0x3F,0x31,0x28,0x25,0x25,0x27,0x22,0x22,0x20,0x1F,0x1C,0x12,0x0F,0x0B,0x02,0x02,0x3F,0x31,0x28,0x25,0x25,0x27,0x22,0x22,0x20,0x1F,0x1C,0x12,0x0F,0x0B,0x02,0x02}, 32, 0},
    {0xD3, (uint8_t []){0x28,0x13}, 2, 0},
    {0xD7, (uint8_t []){0x00,0x30}, 2, 0},
    {0xD9, (uint8_t []){0x00,0x00,0xFF,0x00,0xF0,0x00}, 6, 0},
    {0xDE, (uint8_t []){0x01}, 1, 0},
    {0xB7, (uint8_t []){0x13,0xE7,0x64,0x39,0x06,0x36,0x19,0x1C}, 8, 0},
    {0xBE, (uint8_t []){0x00}, 1, 0},
    {0xC1, (uint8_t []){0x05,0x4A,0x80}, 3, 0},
    {0xC2, (uint8_t []){0x00,0x16,0xDA,0xE7}, 4, 0},
    {0xC7, (uint8_t []){0x00,0x00,0x00,0x38,0x08,0x08,0x00,0x01}, 8, 0},
    {0xC8, (uint8_t []){0x00,0x00,0x00,0x00,0x15,0x3B}, 6, 0},
    {0xC9, (uint8_t []){0x06,0x04,0x0A,0x08,0x1E}, 5, 0},
    {0xCA, (uint8_t []){0x00,0x35,0x15,0x1F,0x1F}, 5, 0},
    {0xCB, (uint8_t []){0x07,0x05,0x0B,0x09,0x1E}, 5, 0},
    {0xCC, (uint8_t []){0x01,0x35,0x15,0x1F,0x1F}, 5, 0},
    {0xCD, (uint8_t []){0x09,0x0B,0x05,0x07,0x1E}, 5, 0},
    {0xCE, (uint8_t []){0x01,0x35,0x1F,0x15,0x1F}, 5, 0},
    {0xCF, (uint8_t []){0x08,0x0A,0x04,0x06,0x1E}, 5, 0},
    {0xD0, (uint8_t []){0x00,0x35,0x1F,0x15,0x1F}, 5, 0},
    {0xD1, (uint8_t []){0x02,0x30}, 2, 0},
    {0xD2, (uint8_t []){0x02,0x03,0x52,0xDF,0xDD}, 5, 0},
    {0xD3, (uint8_t []){0x3B,0x04,0x48}, 3, 0},
    {0xD5, (uint8_t []){0x10,0x10,0x07,0x07,0x0F,0x94,0x26}, 7, 0},
    {0xD6, (uint8_t []){0x00,0x00,0x40}, 3, 0},
    {0xD7, (uint8_t []){0x00,0x00,0x20}, 3, 0},
    {0xDE, (uint8_t []){0x02}, 1, 0},
    {0xB6, (uint8_t []){0x1C}, 1, 0},
    {0xDE, (uint8_t []){0x00}, 1, 0},
    {0x4D, (uint8_t []){0x00}, 1, 0},
    {0x4E, (uint8_t []){0x00}, 1, 0},
    {0x4F, (uint8_t []){0x00}, 1, 0},
    {0x4C, (uint8_t []){0x01}, 1, 10},
    {0x4C, (uint8_t []){0x00}, 1, 0},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x35, (uint8_t []){0x00}, 0, 0},
    {0x2A, (uint8_t []){0x00,0x00,0x01,0x68}, 4, 0},
    {0x2B, (uint8_t []){0x00,0x00,0x01,0x68}, 4, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x11, (uint8_t []){0x00}, 0, 120},
    {0x29, (uint8_t []){0x00}, 0, 20},
};

typedef struct {
    SemaphoreHandle_t te_v_sync_sem;    /*!< Semaphore for vertical synchronization */
    SemaphoreHandle_t te_catch_sem;     /*!< Semaphore for tear catch */
    uint32_t time_Tvdl;                 /*!< tvdl = The display panel is updated from the Frame Memory */
    uint32_t time_Tvdh;                 /*!< tvdh = The display panel is not updated from the Frame Memory */
    uint32_t te_timestamp;              /*!< Tear record timestamp */
    portMUX_TYPE lock;                  /*!< Lock for read/write */
} bsp_lcd_tear_t;

typedef struct {
    SemaphoreHandle_t tp_intr_event;    /*!< Semaphore for tp interrupt */
    lv_disp_rot_t rotate;               /*!< Rotation configuration for the display */
} bsp_touch_int_t;

static lv_disp_t *disp;
static lv_indev_t *disp_indev = NULL;
static esp_lcd_touch_handle_t tp = NULL;   // LCD touch handle
static esp_lcd_panel_handle_t panel_handle = NULL;

static i2c_master_bus_handle_t s_i2c_bus = NULL;

esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_bus) {
        return ESP_OK;
    }

    const i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = EXAMPLE_PIN_NUM_QSPI_TOUCH_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_QSPI_TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };

    BSP_ERROR_CHECK_RETURN_ERR(i2c_new_master_bus(&i2c_bus_config, &s_i2c_bus));

    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (!s_i2c_bus) {
        return ESP_OK;
    }

    BSP_ERROR_CHECK_RETURN_ERR(i2c_del_master_bus(s_i2c_bus));
    s_i2c_bus = NULL;
    return ESP_OK;
}

static bool __attribute__((unused)) bsp_display_sync_cb(void *arg)
{
    assert(arg);
    bsp_lcd_tear_t *tear_handle = (bsp_lcd_tear_t *)arg;

    if (tear_handle->te_catch_sem) {
        xSemaphoreGive(tear_handle->te_catch_sem);
    }

    if (tear_handle->te_v_sync_sem) {

        xSemaphoreTake(tear_handle->te_v_sync_sem, portMAX_DELAY);
    }
    return true;
}

static void bsp_display_sync_task(void *arg)
{
    assert(arg);
    bsp_lcd_tear_t *tear_handle = (bsp_lcd_tear_t *)arg;

    while (true) {
        if (pdPASS != xSemaphoreTake(tear_handle->te_catch_sem, pdMS_TO_TICKS(tear_handle->time_Tvdl))) {
            xSemaphoreTake(tear_handle->te_v_sync_sem, 0);
        }
    }
    vTaskDelete(NULL);
}

static void bsp_display_tear_interrupt(void *arg)
{
    assert(arg);
    bsp_lcd_tear_t *tear_handle = (bsp_lcd_tear_t *)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (tear_handle->te_v_sync_sem) {
        portENTER_CRITICAL_ISR(&tear_handle->lock);
        tear_handle->te_timestamp = esp_log_timestamp();
        portEXIT_CRITICAL_ISR(&tear_handle->lock);
        xSemaphoreGiveFromISR(tear_handle->te_v_sync_sem, &xHigherPriorityTaskWoken);

        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    assert(config != NULL && config->max_transfer_sz > 0);

    SemaphoreHandle_t te_catch_sem = NULL;
    SemaphoreHandle_t te_v_sync_sem = NULL;
    bsp_lcd_tear_t *tear_ctx = NULL;

    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = JD9855_PANEL_BUS_QSPI_CONFIG(
                                        EXAMPLE_PIN_NUM_QSPI_PCLK,
                                        EXAMPLE_PIN_NUM_QSPI_DATA0,
                                        EXAMPLE_PIN_NUM_QSPI_DATA1,
                                        EXAMPLE_PIN_NUM_QSPI_DATA2,
                                        EXAMPLE_PIN_NUM_QSPI_DATA3,
                                        config->max_transfer_sz);
    ESP_ERROR_CHECK(spi_bus_initialize(EXAMPLE_LCD_QSPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {                                     
        .cs_gpio_num = EXAMPLE_PIN_NUM_QSPI_CS,                                      
        .dc_gpio_num = -1,                                      
        .spi_mode = 0,                                          
        .pclk_hz = EXAMPLE_LCD_QSPI_CLK_SPEED_HZ,                        
        .trans_queue_depth = 10,                                
        .on_color_trans_done = NULL,                              
        .user_ctx = NULL,                                     
        .lcd_cmd_bits = 32,                                     
        .lcd_param_bits = 8,                                    
        .flags = {                                              
            /* 像素走 D0–D3，配合驱动里的 0x32 写显存 */
            .quad_mode = true,                                  
        },                                                      
    };

    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_QSPI_HOST, &io_config, ret_io));

    ESP_LOGI(TAG, "Install LCD driver of jd9855");
    const jd9855_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,         // Uncomment these line if use custom initialization commands
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_QSPI_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .vendor_config = (void *) &vendor_config,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_jd9855(*ret_io, &panel_config, ret_panel));

    esp_lcd_panel_reset(*ret_panel);
    esp_lcd_panel_init(*ret_panel);

    esp_lcd_panel_disp_on_off(*ret_panel, true);

    if (config->tear_cfg.te_gpio_num > 0) {

        tear_ctx = malloc(sizeof(bsp_lcd_tear_t));
        ESP_GOTO_ON_FALSE(tear_ctx, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for tear_ctx allocation!");

        te_v_sync_sem = xSemaphoreCreateCounting(1, 0);
        ESP_GOTO_ON_FALSE(te_v_sync_sem, ESP_ERR_NO_MEM, err, TAG, "Failed to create te_v_sync_sem Semaphore");
        tear_ctx->te_v_sync_sem = te_v_sync_sem;

        te_catch_sem = xSemaphoreCreateCounting(1, 0);
        ESP_GOTO_ON_FALSE(te_catch_sem, ESP_ERR_NO_MEM, err, TAG, "Failed to create te_catch_sem Semaphore");
        tear_ctx->te_catch_sem = te_catch_sem;

        tear_ctx->time_Tvdl = config->tear_cfg.time_Tvdl;
        tear_ctx->time_Tvdh = config->tear_cfg.time_Tvdh;

        tear_ctx->lock.owner = portMUX_FREE_VAL;
        tear_ctx->lock.count = 0;

        const gpio_config_t te_detect_cfg = {
            .intr_type = config->tear_cfg.tear_intr_type,
            .mode = GPIO_MODE_INPUT,
            .pin_bit_mask = BIT64(config->tear_cfg.te_gpio_num),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_ENABLE,
        };

        ESP_ERROR_CHECK(gpio_config(&te_detect_cfg));
        gpio_install_isr_service(0);
        ESP_ERROR_CHECK(gpio_isr_handler_add(config->tear_cfg.te_gpio_num, bsp_display_tear_interrupt, tear_ctx));

        BaseType_t res;
        if (config->tear_cfg.task_affinity < 0) {
            res = xTaskCreate(bsp_display_sync_task, "Tear task", config->tear_cfg.task_stack, tear_ctx, config->tear_cfg.task_priority, NULL);
        } else {
            res = xTaskCreatePinnedToCore(bsp_display_sync_task, "Tear task", config->tear_cfg.task_stack, tear_ctx, config->tear_cfg.task_priority, NULL, config->tear_cfg.task_affinity);
        }
        ESP_GOTO_ON_FALSE(res == pdPASS, ESP_FAIL, err, TAG, "Create Sync task fail!");
    }

    (*ret_panel)->user_data = (void *)tear_ctx;

    return ret;

err:
    if (te_v_sync_sem) {
        vSemaphoreDelete(te_v_sync_sem);
    }
    if (te_catch_sem) {
        vSemaphoreDelete(te_catch_sem);
    }
    if (tear_ctx) {
        free(tear_ctx);
    }
    if (*ret_panel) {
        esp_lcd_panel_del(*ret_panel);
    }
    if (*ret_io) {
        esp_lcd_panel_io_del(*ret_io);
    }

    spi_bus_free(EXAMPLE_LCD_QSPI_HOST);

    return ret;
}

static lv_disp_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);
    esp_lcd_panel_io_handle_t io_handle = NULL;

    uint32_t hres;
    uint32_t vres;

    hres = EXAMPLE_LCD_QSPI_H_RES;
    vres = EXAMPLE_LCD_QSPI_V_RES;
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = hres * vres * sizeof(uint16_t),
        .tear_cfg = BSP_SYNC_TASK_CONFIG(EXAMPLE_PIN_NUM_QSPI_TE, GPIO_INTR_NEGEDGE),
    };

    bsp_display_new(&bsp_disp_cfg, &panel_handle, &io_handle);

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = cfg->buffer_size,
        .sw_rotate = cfg->rotate,
        .hres = hres,
        .vres = vres,
        .trans_size = hres * vres / 10,
        /* 整帧传输长于一场刷新，等 TE 再写会把画面切成两截 */
        .draw_wait_cb = NULL,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        },
    };

    if (disp_cfg.sw_rotate == LV_DISP_ROT_180 || disp_cfg.sw_rotate == LV_DISP_ROT_NONE) {
        disp_cfg.hres = hres;
        disp_cfg.vres = vres;
    } else {
        disp_cfg.hres = vres;
        disp_cfg.vres = hres;
    }

    return lvgl_port_add_disp(&disp_cfg);
}

static bool bsp_touch_sync_cb(void *arg)
{
    assert(arg);
    bool touch_interrupt = false;
    bsp_touch_int_t *touch_handle = (bsp_touch_int_t *)arg;

    if (touch_handle && touch_handle->tp_intr_event) {
        if (xSemaphoreTake(touch_handle->tp_intr_event, 0) == pdTRUE) {
            touch_interrupt = true;
        }
    } else {
        touch_interrupt = true;
    }

    return touch_interrupt;
}

static void bsp_touch_interrupt_cb(esp_lcd_touch_handle_t tp)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    bsp_touch_int_t *touch_handle = (bsp_touch_int_t *)tp->config.user_data;

    xSemaphoreGiveFromISR(touch_handle->tp_intr_event, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void bsp_touch_process_points_cb(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    uint16_t tmp;
    bsp_touch_int_t *touch_handle = (bsp_touch_int_t *)tp->config.user_data;

    for (int i = 0; i < *point_num; i++) {
        if (LV_DISP_ROT_270 == touch_handle->rotate) {
            tmp = x[i];
            x[i] = tp->config.y_max - y[i];
            y[i] = tmp;
        } else if (LV_DISP_ROT_180 == touch_handle->rotate) {
            tmp = x[i];
            x[i] = tp->config.x_max - x[i];
            y[i] = tp->config.y_max - y[i];
        } else if (LV_DISP_ROT_90 == touch_handle->rotate) {
            tmp = x[i];
            x[i] = y[i];
            y[i] = tp->config.x_max - tmp;
        }
    }
}

esp_err_t bsp_touch_new(const bsp_display_cfg_t *config, esp_lcd_touch_handle_t *ret_touch)
{
    esp_err_t ret = ESP_OK;

    /* Initialize I2C */
    BSP_ERROR_CHECK_RETURN_ERR(bsp_i2c_init());

    SemaphoreHandle_t tp_intr_event = NULL;
    bsp_touch_int_t *touch_ctx = NULL;

    /* Initialize touch */
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_QSPI_H_RES,
        .y_max = EXAMPLE_LCD_QSPI_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_QSPI_TOUCH_RST, // Shared with LCD reset
        .int_gpio_num = EXAMPLE_PIN_NUM_QSPI_TOUCH_INT,
        .process_coordinates = bsp_touch_process_points_cb,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_touch_handle_t tp_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816_CONFIG();
    tp_io_config.scl_speed_hz = BSP_I2C_CLK_SPEED_HZ;

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_config, &tp_io_handle), TAG, "");
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816(tp_io_handle, &tp_cfg, &tp_handle), TAG, "New cst816 failed");

    touch_ctx = malloc(sizeof(bsp_touch_int_t));
    ESP_GOTO_ON_FALSE(touch_ctx, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for touch_ctx allocation!");

    if (tp_cfg.int_gpio_num > 0) {

        tp_intr_event = xSemaphoreCreateBinary();
        ESP_GOTO_ON_FALSE(tp_intr_event, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for tp_intr_event allocation!");
        touch_ctx->tp_intr_event = tp_intr_event;
        esp_lcd_touch_register_interrupt_callback_with_data(tp_handle, bsp_touch_interrupt_cb, (void *)touch_ctx);
    } else {
        touch_ctx->tp_intr_event = NULL;
    }
    touch_ctx->rotate = config->rotate;
    tp_handle->config.user_data = touch_ctx;

    *ret_touch = tp_handle;

    return ESP_OK;
err:
    if (tp_intr_event) {
        vSemaphoreDelete(tp_intr_event);
    }
    if (touch_ctx) {
        free(touch_ctx);
    }
    if (tp_handle) {
        esp_lcd_touch_del(tp_handle);
    }
    if (tp_io_handle) {
        esp_lcd_panel_io_del(tp_io_handle);
    }
    return ret;
}

static lv_indev_t *bsp_display_indev_init(const bsp_display_cfg_t *config, lv_disp_t *disp) __attribute__((unused));

static lv_indev_t *bsp_display_indev_init(const bsp_display_cfg_t *config, lv_disp_t *disp)
{
    BSP_ERROR_CHECK_RETURN_NULL(bsp_touch_new(config, &tp));
    assert(tp);

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = tp,
        .touch_wait_cb = bsp_touch_sync_cb,
    };

    return lvgl_port_add_touch(&touch_cfg);
}

lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    BSP_ERROR_CHECK_RETURN_NULL(lvgl_port_init(&cfg->lvgl_port_cfg));

    BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);

    /* 当前板子没有触摸 */
    // BSP_NULL_CHECK(disp_indev = bsp_display_indev_init(cfg, disp), NULL);

    return disp;
}

lv_indev_t *bsp_display_get_input_dev(void)
{
    return disp_indev;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}
