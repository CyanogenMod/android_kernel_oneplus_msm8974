/* Copyright (c) 2009-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef MSM_LED_FLASH_H
#define MSM_LED_FLASH_H

#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <media/v4l2-subdev.h>
#include <media/msm_cam_sensor.h>
#include <mach/camera2.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <mach/gpiomux.h>
#include <linux/gpio.h>
#include "msm_camera_i2c.h"
#include "msm_cci.h"
#include "msm_sd.h"
#include <linux/pcb_version.h>
#include <linux/proc_fs.h>

#define MAX_LED_TRIGGERS 3

struct msm_led_cci_ctrl_t;

struct msm_led_cci_fn_t {
	int32_t (*flash_get_subdev_id)(struct msm_led_cci_ctrl_t *, void *);
	int32_t (*flash_led_config)(struct msm_led_cci_ctrl_t *, void *);
	int32_t (*flash_led_init)(struct msm_led_cci_ctrl_t *);
	int32_t (*flash_led_release)(struct msm_led_cci_ctrl_t *);
	int32_t (*flash_led_off)(struct msm_led_cci_ctrl_t *);
	int32_t (*flash_led_low)(struct msm_led_cci_ctrl_t *);
	int32_t (*flash_led_high)(struct msm_led_cci_ctrl_t *);
};

struct msm_led_cci_reg_t {
	struct msm_camera_i2c_reg_setting *init_setting;
	struct msm_camera_i2c_reg_setting *off_setting;
	struct msm_camera_i2c_reg_setting *release_setting;
	struct msm_camera_i2c_reg_setting *low_setting;
	struct msm_camera_i2c_reg_setting *high_setting;
};

struct msm_led_cci_led_info_t {
    char name[8];
    const char *regulator_name;
    uint32_t cci_master;
    uint32_t slave_id;
    uint32_t chip_id_reg;
    uint32_t chip_id;
    uint32_t fault_info_reg;
    uint32_t max_torch_current;
    uint32_t max_flash_current;
    uint32_t gpio_en;
    uint32_t gpio_torch_en;
    uint32_t gpio_strobe_en;
    struct msm_led_cci_reg_t config_arrays;
    struct regulator *retulator;
    uint32_t status;
    int test_mode;
    bool test_status;
    bool blink_status;
    struct delayed_work dwork;
};

struct msm_led_cci_ctrl_t {
	struct msm_camera_i2c_client *flash_i2c_client;
	struct msm_sd_subdev msm_sd;
	struct platform_device *pdev;
	struct msm_led_cci_fn_t *func_tbl;
	struct msm_camera_sensor_board_info *flashdata;
	struct msm_led_cci_led_info_t *led_info;
	struct msm_led_cci_reg_t *reg_setting;
	const char *flash_trigger_name[MAX_LED_TRIGGERS];
	struct led_trigger *flash_trigger[MAX_LED_TRIGGERS];
	uint32_t flash_op_current[MAX_LED_TRIGGERS];
	const char *torch_trigger_name;
	struct led_trigger *torch_trigger;
	uint32_t torch_op_current;
	void *data;
	uint32_t num_sources;
	enum msm_camera_device_type_t flash_device_type;
	uint32_t subdev_id;
};

#endif

