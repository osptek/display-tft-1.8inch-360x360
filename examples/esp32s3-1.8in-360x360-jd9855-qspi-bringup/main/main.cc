#include "esp_bsp.h"
#include "display.h"
#include "lv_demos.h"

#include "esp_log.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = EXAMPLE_LCD_QSPI_H_RES * EXAMPLE_LCD_QSPI_V_RES,
        .rotate = LV_DISP_ROT_NONE,
    };

    if (bsp_display_start_with_config(&cfg) == NULL) {
        ESP_LOGE(TAG, "Display start failed");
        return;
    }

    bsp_display_lock(0);
    lv_demo_widgets();
    bsp_display_unlock();
}
