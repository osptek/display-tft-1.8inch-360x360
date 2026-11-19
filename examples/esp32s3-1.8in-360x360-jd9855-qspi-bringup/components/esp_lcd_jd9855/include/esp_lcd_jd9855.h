/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

#include "hal/lcd_types.h"
#include "esp_lcd_panel_vendor.h"

#define ESP_LCD_JD9855_VER_MAJOR 1
#define ESP_LCD_JD9855_VER_MINOR 0
#define ESP_LCD_JD9855_VER_PATCH 0

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LCD panel initialization commands.
 */
typedef struct {
    int cmd;                /*!< The specific LCD command */
    const void *data;       /*!< Buffer that holds the command specific data */
    size_t data_bytes;      /*!< Size of `data` in memory, in bytes */
    unsigned int delay_ms;  /*!< Delay in milliseconds after this command */
} jd9855_lcd_init_cmd_t;

/**
 * @brief LCD panel vendor configuration.
 *
 * Pass this structure through `vendor_config` of `esp_lcd_panel_dev_config_t`.
 */
typedef struct {
    const jd9855_lcd_init_cmd_t *init_cmds; /*!< Initialization commands. Required. */
    uint16_t init_cmds_size;                /*!< Number of commands in `init_cmds`. */
    struct {
        unsigned int use_qspi_interface: 1; /*!< QSPI command framing (32-bit opcode + command). */
    } flags;
} jd9855_vendor_config_t;

/**
 * @brief Create a JD9855 LCD panel.
 *
 * @param[in]  io LCD panel IO handle
 * @param[in]  panel_dev_config General panel device configuration
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *      - ESP_OK: Success
 *      - Otherwise: Fail
 */
esp_err_t esp_lcd_new_panel_jd9855(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

#define JD9855_PANEL_BUS_QSPI_CONFIG(sclk, d0, d1, d2, d3, max_trans_sz) \
    {                                                           \
        .sclk_io_num = sclk,                                    \
        .data0_io_num = d0,                                     \
        .data1_io_num = d1,                                     \
        .data2_io_num = d2,                                     \
        .data3_io_num = d3,                                     \
        .max_transfer_sz = max_trans_sz,                        \
    }

#ifdef __cplusplus
}
#endif
