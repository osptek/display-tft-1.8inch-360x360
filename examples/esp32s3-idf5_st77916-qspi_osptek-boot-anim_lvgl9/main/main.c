#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st77916.h"
#include "esp_lcd_touch_cst816s.h"

#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "lv_demos.h"

#include "st77916_init_cmds.h"

/* lv_efc.h 与 efc mmap 资产头文件 */
#include "lv_efc.h"
#include "esp_lv_fs.h"
#include "esp_mmap_assets.h"
#include "mmap_generate_efc.h"

static const char *TAG = "LVGL_ST77916_CST816S_QSPI";

// ==================== 引脚定义（请根据你的硬件修改） ====================
#define EXAMPLE_PIN_NUM_LCD_CS      GPIO_NUM_14
#define EXAMPLE_PIN_NUM_LCD_SCK     GPIO_NUM_9
#define EXAMPLE_PIN_NUM_LCD_D0      GPIO_NUM_10   // QSPI D0
#define EXAMPLE_PIN_NUM_LCD_D1      GPIO_NUM_11   // QSPI D1
#define EXAMPLE_PIN_NUM_LCD_D2      GPIO_NUM_12   // QSPI D2
#define EXAMPLE_PIN_NUM_LCD_D3      GPIO_NUM_13   // QSPI D3
#define EXAMPLE_PIN_NUM_LCD_RST     GPIO_NUM_15
#define EXAMPLE_PIN_NUM_BK_LIGHT    GPIO_NUM_47
#define EXAMPLE_PIN_NUM_LCD_TE      GPIO_NUM_16

#define EXAMPLE_TOUCH_I2C_SCL       GPIO_NUM_42
#define EXAMPLE_TOUCH_I2C_SDA       GPIO_NUM_41
#define EXAMPLE_TOUCH_RST           GPIO_NUM_40
#define EXAMPLE_TOUCH_INT           GPIO_NUM_39

/* Touch settings */
#define EXAMPLE_TOUCH_I2C_NUM       (0)
#define EXAMPLE_TOUCH_I2C_CLK_HZ    (400 * 1000)

// ==================== 显示参数 ====================
#define EXAMPLE_LCD_HOST            SPI2_HOST
#if LV_COLOR_DEPTH == 16
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ  (20 * 1000 * 1000)
#elif LV_COLOR_DEPTH >= 24
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ  (50 * 1000 * 1000)
#endif   

#define EXAMPLE_LCD_H_RES           360
#define EXAMPLE_LCD_V_RES           360    

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL 0

/* EFC player context */
static mmap_assets_handle_t s_assets;
static esp_lv_fs_handle_t s_fs_handle;
static lv_obj_t *s_efc_anim;
static lv_obj_t *s_info_label;
static lv_timer_t *s_finish_check_timer = NULL;

// 挂载 EFC 资产分区并注册 LVGL 文件系统
static esp_err_t mount_efc_assets(void)
{
    mmap_assets_config_t cfg = {
        .partition_label = "efc",
        .max_files = MMAP_EFC_FILES,
        .checksum = MMAP_EFC_CHECKSUM,
        .flags = { .mmap_enable = 1 },
    };

    ESP_RETURN_ON_ERROR(mmap_assets_new(&cfg, &s_assets), TAG, "mmap init failed");

    size_t file_count = mmap_assets_get_stored_files(s_assets);
    if (file_count == 0) {
        mmap_assets_del(s_assets);
        return ESP_ERR_NOT_FOUND;
    }

    fs_cfg_t fs_cfg = {
        .fs_letter = 'E',
        .fs_nums = file_count,
        .fs_assets = s_assets,
    };

    ESP_RETURN_ON_ERROR(esp_lv_adapter_fs_mount(&fs_cfg, &s_fs_handle), TAG, "fs mount failed");
    ESP_LOGI(TAG, "EFC assets mounted on drive E:");

    return ESP_OK;
}

// EFC播放完成检查回调
static void efc_finish_check_cb(lv_timer_t *timer)
{
    if (!timer) return;

    lv_obj_t *efc_anim = (lv_obj_t *)lv_timer_get_user_data(timer);

    if (!efc_anim || !lv_efc_is_loaded(efc_anim)) {
        return;
    }

    int32_t current = lv_efc_get_current_frame(efc_anim);
    int32_t total   = lv_efc_get_total_frames(efc_anim);
    bool loop_enabled = lv_efc_get_loop_enabled(efc_anim);

    // 如果不循环且播放到最后一帧
    if (!loop_enabled && (current >= total - 1 || current < 0)) {
        ESP_LOGI(TAG, "EFC播放完成，启动 lv_demo_widgets 示例");

        // 删除定时器
        lv_timer_delete(timer);
        s_finish_check_timer = NULL;

        // 删除EFC动画对象
        if (s_efc_anim) {
            lv_obj_delete(s_efc_anim);
            s_efc_anim = NULL;
        }

        // 启动 LVGL 官方 widgets 示例
        lv_demo_widgets();
    }
}

/**
 * @brief 初始化并启动 EFC 开机动画播放
 */
static void start_efc_player(lv_display_t *disp)
{
    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        return;
    }

    lv_obj_t *screen = lv_disp_get_scr_act(disp);

    /* 创建 EFC 动画对象 */
    s_efc_anim = lv_efc_create(screen);
    lv_obj_align(s_efc_anim, LV_ALIGN_CENTER, 0, 0);

    /* 构建文件路径并加载动画 */
    const char *file_name = mmap_assets_get_name(s_assets, 0);
    if (file_name) {
        char path[64];
        snprintf(path, sizeof(path), "E:%s", file_name);

        lv_efc_set_src(s_efc_anim, path);

        if (lv_efc_is_loaded(s_efc_anim)) {
            lv_efc_set_frame_delay(s_efc_anim, 20);
            lv_efc_set_loop_enabled(s_efc_anim, false); // 不循环，只播放一次

            /* 创建定时器检查播放完成 */
            s_finish_check_timer = lv_timer_create(efc_finish_check_cb,
                                                   100,           // 每100ms检查一次
                                                   s_efc_anim);   // user_data 传入 efc 对象
        } else {
            if (s_info_label) lv_label_set_text(s_info_label, "EFC Load failed!");
        }
    } else {
        if (s_info_label) lv_label_set_text(s_info_label, "No file!");
    }

    esp_lv_adapter_unlock();
}

/* 独立任务：避免 EFC 解码占用 main 任务栈 */
static void efc_player_task(void *arg)
{
    lv_display_t *disp = (lv_display_t *)arg;
    // 1. 等待一小段时间（如 50ms），确保 LVGL 的黑色背景已经通过 QSPI 刷新到屏幕显存中
    vTaskDelay(pdMS_TO_TICKS(50));

    // 2. 确认黑底已刷新，此时安全开启背光，避免开机白屏或杂色闪烁
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
    ESP_LOGI(TAG, "背光已开启");

    // 3. 启动 EFC 开机动画播放
    start_efc_player(disp);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 + ST77916 (QSPI) + CST816S + LVGL9 开始初始化...");

    // QSPI 总线初始化
    spi_bus_config_t bus_config = {
        .sclk_io_num = EXAMPLE_PIN_NUM_LCD_SCK,
        .data0_io_num = EXAMPLE_PIN_NUM_LCD_D0,
        .data1_io_num = EXAMPLE_PIN_NUM_LCD_D1,
        .data2_io_num = EXAMPLE_PIN_NUM_LCD_D2,
        .data3_io_num = EXAMPLE_PIN_NUM_LCD_D3,
#if LV_COLOR_DEPTH == 16
        .max_transfer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2,
#elif LV_COLOR_DEPTH >= 24
        .max_transfer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 3,
#endif   
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(EXAMPLE_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));

    // LCD Panel IO (QSPI)
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags = {
            .quad_mode = true,
        },
    };
    esp_lcd_panel_io_handle_t panel_io = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_HOST, &io_config, &panel_io));

    // ST77916 Panel 初始化
    st77916_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = lcd_init_cmds_size,
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
#if LV_COLOR_DEPTH == 16
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#elif LV_COLOR_DEPTH >= 24
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#endif    
        .bits_per_pixel = LCD_COLOR_DEPTH,
        .vendor_config = &vendor_config,
    };

    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "ST77916 QSPI 显示初始化完成 (%dx%d)", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

    // CST816S 触摸初始化（I2C）
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = EXAMPLE_TOUCH_I2C_NUM,
        .scl_io_num = EXAMPLE_TOUCH_I2C_SCL,
        .sda_io_num = EXAMPLE_TOUCH_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus));

    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    tp_io_config.scl_speed_hz = EXAMPLE_TOUCH_I2C_CLK_HZ;

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_TOUCH_RST,
        .int_gpio_num = EXAMPLE_TOUCH_INT,
        .levels = {.reset = 0, .interrupt = 0},
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_lcd_touch_handle_t touch_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch_handle));

    ESP_LOGI(TAG, "CST816S 触控初始化完成");

    // esp_lvgl_adapter 初始化
    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_cfg));

    // 注册显示器（QSPI 使用 SPI 宏）
    esp_lv_adapter_display_config_t disp_cfg =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_TE_DEFAULT_CONFIG(
            panel_handle,
            panel_io,
            EXAMPLE_LCD_H_RES,
            EXAMPLE_LCD_V_RES,
            ESP_LV_ADAPTER_ROTATE_0,
            EXAMPLE_PIN_NUM_LCD_TE,
            EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            4,
            LCD_COLOR_DEPTH);

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
    assert(disp != NULL);

    lv_obj_t *default_scr = lv_disp_get_scr_act(disp);
    if (default_scr) {
        lv_obj_remove_style_all(default_scr);
        lv_obj_set_style_bg_color(default_scr, lv_color_black(), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(default_scr, LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_size(default_scr, LV_PCT(100), LV_PCT(100));
    }

    // 注册触摸输入设备
    esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handle);
    lv_indev_t *indev = esp_lv_adapter_register_touch(&touch_cfg);
    assert(indev != NULL);

    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_LOGI(TAG, "esp_lvgl_adapter 启动成功！");

    // 背光初始化
    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bk_gpio_config);
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

    /* 安装 EFC 资产，并在独立任务中启动播放（避免 main 栈溢出） */
    ESP_ERROR_CHECK(mount_efc_assets());
    xTaskCreatePinnedToCore(efc_player_task, "efc_player", 16384, disp, 5, NULL, 0);
}
