/***********************************************************
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: - camera-motor.h
* Description: Head file for camera-motor.
*           To define camera-motor array and register address.
** Version: 1.0
** Date : 2014/06/20	
** Author: Xinhua.Song@BSP
** 
****************************************************************/

enum {
	CAMERA_MOTOR_MODE_FULL = 1,
	CAMERA_MOTOR_MODE_1_2,
	CAMERA_MOTOR_MODE_1_4,
	CAMERA_MOTOR_MODE_1_8,
	CAMERA_MOTOR_MODE_1_16,
	CAMERA_MOTOR_MODE_1_32
};

void start_motor(void);
void stop_motor(void);
void motor_speed_set(int speed);
