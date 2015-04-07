/***********************************************************
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: - camera-motor.c
* Description: Source file for camera motor driver.
				
** Version: 1.0
** Date : 2014/06/20	
** Author: Xinhua.Song@BSP.Group.Module
** 
****************************************************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/wakelock.h>
#include <linux/regulator/consumer.h>
#include <linux/pcb_version.h>
#include <mach/device_info.h>
#include <linux/camera-motor.h>
//zwx
#include <linux/of_gpio.h>
#include <linux/qpnp/pwm.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/qpnp/pin.h>

#define MAX_BUFFER_SIZE	512
#define MOTOR_POWER_ON 1   //powered on 8834 drv IC
#define MOTOR_POWER_OFF 0 //powered off 8834 drv IC

#define  ANGEL_PER_PULSE 18 
#define  GEAR_RATIO_10   467   //46.7
#define CAMERA_DEBUG 1


#define CAMERAMOTOR_POWER_GPIO 67   //camera motor BOOST_EN

#define CAMERAMOTOR_CTRL	GPIO_CFG(CAMERAMOTOR_POWER_GPIO, 0, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA)

enum CAMERA_MOTOR_SPEED {
	CAMERA_MOTOR_SPEED1 = 0, //High
	CAMERA_MOTOR_SPEED2,
	CAMERA_MOTOR_SPEED3,
	CAMERA_MOTOR_SPEED4,
	CAMERA_MOTOR_SPEED5,
	CAMERA_MOTOR_SPEED6,
	CAMERA_MOTOR_SPEED7,
	CAMERA_MOTOR_SPEED8,
	CAMERA_MOTOR_SPEED9,//LOW
};

static struct class *camera_motor_class = NULL;


/* LPG revisions */
enum qpnp_lpg_revision {
	QPNP_LPG_REVISION_0 = 0x0,
	QPNP_LPG_REVISION_1 = 0x1,
};

/* SPMI LPG registers */
enum qpnp_lpg_registers_list {
	QPNP_LPG_PATTERN_CONFIG,
	QPNP_LPG_PWM_SIZE_CLK,
	QPNP_LPG_PWM_FREQ_PREDIV_CLK,
	QPNP_LPG_PWM_TYPE_CONFIG,
	QPNP_PWM_VALUE_LSB,
	QPNP_PWM_VALUE_MSB,
	QPNP_ENABLE_CONTROL,
	QPNP_RAMP_CONTROL,
	QPNP_RAMP_STEP_DURATION_LSB = QPNP_RAMP_CONTROL + 9,
	QPNP_RAMP_STEP_DURATION_MSB,
	QPNP_PAUSE_HI_MULTIPLIER_LSB,
	QPNP_PAUSE_HI_MULTIPLIER_MSB,
	QPNP_PAUSE_LO_MULTIPLIER_LSB,
	QPNP_PAUSE_LO_MULTIPLIER_MSB,
	QPNP_HI_INDEX,
	QPNP_LO_INDEX,
	QPNP_TOTAL_LPG_SPMI_REGISTERS
};

struct qpnp_lut_config {
	u8	*duty_pct_list;
	int	list_len;
	int	lo_index;
	int	hi_index;
	int	lut_pause_hi_cnt;
	int	lut_pause_lo_cnt;
	int	ramp_step_ms;
	bool	ramp_direction;
	bool	pattern_repeat;
	bool	ramp_toggle;
	bool	enable_pause_hi;
	bool	enable_pause_lo;
};

struct qpnp_lpg_config {
	struct qpnp_lut_config	lut_config;
	u16			base_addr;
	u16			lut_base_addr;
	u16			lut_size;
};

struct qpnp_pwm_config {
	int				channel_id;
	bool				in_use;
	const char			*lable;
	int				pwm_value;
	int				pwm_period;	/* in microseconds */
	int				pwm_duty;	/* in microseconds */
	struct pwm_period_config	period;
	int				force_pwm_size;
};

/* Public facing structure */
struct pwm_device {
	struct qpnp_lpg_chip	*chip;
	struct qpnp_pwm_config	pwm_config;
};

struct qpnp_lpg_chip {
	struct	spmi_device	*spmi_dev;
	struct	pwm_device	pwm_dev;
	spinlock_t		lpg_lock;
	struct	qpnp_lpg_config	lpg_config;
	u8	qpnp_lpg_registers[QPNP_TOTAL_LPG_SPMI_REGISTERS];
	enum qpnp_lpg_revision	revision;
	u8			sub_type;
	u32			flags;
};

struct CameraMotor_platform_data 
{
	int         md_mode;    /*data*/   
	int         md_speed;    
	int         md_dir;    // 1:forward   0:reverse    
	unsigned long         md_angle;   


	int		pwm_count;
	int		power_enabled;
	int		pwm_enable;

	struct pwm_device	*pwm;
	
	struct work_struct	work;
	spinlock_t		lock;

};

struct CameraMotor_dev	
{
	struct device *dev;
	struct miscdevice	motor_device;
	unsigned int md_vref_gpio;
	unsigned int md_sleep_gpio;
	unsigned int md_dir_gpio;
	unsigned int md_m0_gpio;
	unsigned int md_m1_gpio;
	unsigned int md_step_gpio;

	struct CameraMotor_platform_data data;
	struct hrtimer timer;
};

struct CameraMotor_dev  *motor_devdata = NULL;
struct qpnp_pin_cfg param;  // zhangqiang add  for motor blocking 
static void CameraMotor_running(struct work_struct *work);
static struct wake_lock motor_suspend_wake_lock;

struct manufacture_info camaramotor_info = {
	.version = "8834",
	.manufacture = "TI",
};

static ssize_t direction_store(struct device *pdev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	ssize_t ret = -EINVAL;	
	unsigned long direction = 0;

	if(motor_devdata->data.pwm_enable)
		return ret;

//	printk("%s: songxh motor_devdata->data.md_dir = %d\n",__func__, motor_devdata->data.md_dir);
	ret = strict_strtoul(buf, 10, &direction);
		
	if (!ret) 
	{		
		ret = size;
		motor_devdata->data.md_dir = direction;
	}
	return ret;
}

static ssize_t direction_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t size = -EINVAL;

	if (motor_devdata->data.md_dir >= 0)
		size = snprintf(buf, PAGE_SIZE, "%d\n", motor_devdata->data.md_dir );
	return size;
}

static ssize_t speed_store(struct device *pdev, struct device_attribute *attr,
			   const char *buff, size_t size)
{
	ssize_t ret = -EINVAL;	
	unsigned long speed = 0;

	if(motor_devdata->data.pwm_enable)
		return ret;
	printk("speed = %d\n", motor_devdata->data.md_speed);
	ret = strict_strtoul(buff, 10, &speed);
		
	if (!ret) 
	{		
		ret = size;
		motor_devdata->data.md_speed = speed;
	}
    
	printk("speed:%d\n", motor_devdata->data.md_speed);
    
	return ret;
}

static ssize_t speed_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t size = -EINVAL;

	if (motor_devdata->data.md_speed >= 0)
		size = snprintf(buf, PAGE_SIZE, "%d\n", motor_devdata->data.md_speed);
	return size;
}

static ssize_t mode_store(struct device *pdev, struct device_attribute *attr,
			   const char *buff, size_t size)
{
	ssize_t ret = -EINVAL;	
	unsigned long mdmode = 0;
	int mode;

	if(motor_devdata->data.pwm_enable)
		return ret;
//	printk("%s: songxh mdmode = %d\n",__func__, motor_devdata->data.md_mode);
	ret = strict_strtoul(buff, 10, &mdmode);
	
	if (!ret) 
	{		
		ret = size;
		switch(mdmode)
		{
			case CAMERA_MOTOR_MODE_FULL:
				mode = 1;
			break;

			case CAMERA_MOTOR_MODE_1_2:
				mode = 2;
			break;

			case CAMERA_MOTOR_MODE_1_4:
				mode = 4;
			break;

			case CAMERA_MOTOR_MODE_1_8:
				mode = 8;
			break;

			case CAMERA_MOTOR_MODE_1_16:
				mode = 16;
			break;

			case CAMERA_MOTOR_MODE_1_32:
				mode = 32;
			break;

			default:
				mode = 32;
			break;
		}			
		motor_devdata->data.md_mode= mode;
		printk("mdmode = %d\n", mode);
	}
	return ret;
}

static ssize_t  mode_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t size = -EINVAL;

	if (motor_devdata->data.md_mode >= 0)
		size = snprintf(buf, PAGE_SIZE, "%d\n", motor_devdata->data.md_mode);
	return size;
}

static ssize_t angel_store(struct device *pdev, struct device_attribute *attr,
			   const char *buf, size_t size)
{
	ssize_t ret = -EINVAL;	
	unsigned long angel = 0;

	if(motor_devdata->data.pwm_enable)
		return ret;

	ret = strict_strtoul(buf, 10, &angel);
	if (!ret) 
	{		
		ret = size;
		if (angel <= 0 || angel > 240)
			motor_devdata->data.md_angle =  1;
		else
			motor_devdata->data.md_angle =  angel;
	}
	printk("angle:%lu\n", motor_devdata->data.md_angle);	
	return ret;
}

static ssize_t  angel_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t size = -EINVAL;

	if (motor_devdata->data.md_angle >= 0 )
		size = snprintf(buf, PAGE_SIZE, "%lu\n", motor_devdata->data.md_angle);
	return size;
}


static ssize_t pwm_enable_store(struct device *pdev, struct device_attribute *attr,
			    const char *buff, size_t size)
{
	ssize_t ret = -EINVAL;	
	unsigned long enable = 0;

	ret = strict_strtoul(buff, 10, &enable);
	if (!ret) 
	{
		unsigned long flags;
		ret = size;
		spin_lock_irqsave(&motor_devdata->data.lock, flags);
		hrtimer_cancel(&motor_devdata->timer);
		if(enable == 0)
		{
			motor_devdata->data.pwm_enable = 0;
		}
		else
		{
			motor_devdata->data.pwm_enable = 1;
		}

        printk("pwm_enable %d\n", motor_devdata->data.pwm_enable);
        
		spin_unlock_irqrestore(&motor_devdata->data.lock, flags);
		schedule_work(&motor_devdata->data.work);	
	}
	return ret;
}


static ssize_t pwm_change_speed_store(struct device *pdev, struct device_attribute *attr,
			    const char *buff, size_t size)
{
	ssize_t ret = -EINVAL;	
	unsigned long speed = 0;

	ret = strict_strtoul(buff, 10, &speed);
	if (!ret) 
	{
		unsigned long flags;
		ret = size;
		spin_lock_irqsave(&motor_devdata->data.lock, flags);
		hrtimer_cancel(&motor_devdata->timer);

		motor_devdata->data.md_speed = speed;
		spin_unlock_irqrestore(&motor_devdata->data.lock, flags);
        
		printk("pwm_change_speed : %d\n", motor_devdata->data.md_speed);
		schedule_work(&motor_devdata->data.work);
	}
	return ret;
}

static ssize_t  pwm_change_speed_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t size = -EINVAL;

	if (motor_devdata->data.md_speed >= 0)
		size = snprintf(buf, PAGE_SIZE, "%d\n", motor_devdata->data.md_speed);
	return size;
}

static ssize_t  pwm_enable_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t size = -EINVAL;

	if (motor_devdata->data.pwm_enable >= 0)
		size = snprintf(buf, PAGE_SIZE, "%d\n", motor_devdata->data.pwm_enable);
	return size;
}

// zhangqiang add  for motor blocking 
extern int start_motor_flag ;
extern int direction_for_hall;

void start_motor(void)
{
    unsigned long flags;
    spin_lock_irqsave(&motor_devdata->data.lock, flags);
    motor_devdata->data.pwm_enable = 1; 
    spin_unlock_irqrestore(&motor_devdata->data.lock, flags);
    schedule_work(&motor_devdata->data.work);
}

void stop_motor(void)
{
    unsigned long flags;
    spin_lock_irqsave(&motor_devdata->data.lock, flags);
    motor_devdata->data.pwm_enable = 0;
    spin_unlock_irqrestore(&motor_devdata->data.lock, flags);

    schedule_work(&motor_devdata->data.work);

    printk("stop motor , dhall detect! \n");
}

void motor_speed_set(int speed)
{
    unsigned long flags;
    spin_lock_irqsave(&motor_devdata->data.lock, flags);
    motor_devdata->data.md_speed = speed;
    spin_unlock_irqrestore(&motor_devdata->data.lock, flags);
    printk("%s = %d! \n", __func__, speed);
}

static void CameraMotor_running(struct work_struct *work)
{
	unsigned long flags, duty_ns, intsecond,nsecond;
	long long value, period_ns;
	int mdmode = 0;

	spin_lock_irqsave(&motor_devdata->data.lock, flags);

	printk("%s enable  = %d, dir = %d, start %d\n", __func__,motor_devdata->data.pwm_enable,  motor_devdata->data.md_dir, start_motor_flag);

    direction_for_hall = motor_devdata->data.md_dir;    // zhangqiang add  for motor blocking 
    
	if(motor_devdata->data.pwm_enable)
	{
		gpio_set_value(CAMERAMOTOR_POWER_GPIO, 1);
		gpio_set_value(motor_devdata->md_sleep_gpio, 1);
		gpio_set_value(motor_devdata->md_dir_gpio, motor_devdata->data.md_dir);

		switch(motor_devdata->data.md_mode)
		{
			case 1://Full Setp mode 
				param.mode  = 1;
				param.output_type = 0;
				param.invert = QPNP_PIN_INVERT_DISABLE;
				param.pull = 5;
				param.vin_sel = 2; // 1.8V
				//param.out_strength = 1; //QPNP_PIN_OUT_STRENGTH_LOW
				param.out_strength = 3; //QPNP_PIN_OUT_STRENGTH_HIGH
				param.src_sel = 0;
				param.master_en = 1;	
				param.ain_route = 0;
				param.aout_ref = 0;
				param.cs_out = 0;				
				qpnp_pin_config(motor_devdata->md_vref_gpio, &param);
				gpio_set_value(motor_devdata->md_vref_gpio, 1); //not Z state, use gpio_set_value function to set the value after be configed

				param.mode  = 1;
				param.output_type = 0;
				param.invert = QPNP_PIN_INVERT_DISABLE;
				param.pull = 5;
				param.vin_sel = 2; // 1.8V
				param.out_strength = 1;
				param.src_sel = 0;
				param.master_en = 1;
				param.ain_route = 0;
				param.aout_ref = 0;
				param.cs_out = 0;				
				qpnp_pin_config(motor_devdata->md_m0_gpio, &param);
				gpio_set_value(motor_devdata->md_m0_gpio, 0);
				gpio_set_value(motor_devdata->md_m1_gpio, 0);
				mdmode = 1;
			break;

			case 16:
				param.mode  = 1;
				param.output_type = 0;
				param.invert = QPNP_PIN_INVERT_DISABLE;
				param.pull = 5;
				param.vin_sel = 2; // 1.8V
				//param.out_strength = 1; //QPNP_PIN_OUT_STRENGTH_LOW
				param.out_strength = 3; //QPNP_PIN_OUT_STRENGTH_HIGH
				param.src_sel = 0;
				param.master_en = 0;	//high impedance
				param.ain_route = 0;
				param.aout_ref = 0;
				param.cs_out = 0;				
				qpnp_pin_config(motor_devdata->md_vref_gpio, &param);

				param.mode  = 1;
				param.output_type = 0;
				param.invert = QPNP_PIN_INVERT_DISABLE;
				param.pull = 5;
				param.vin_sel = 2; // 1.8V
				param.out_strength = 1;
				param.src_sel = 0;
				param.master_en = 1;
				param.ain_route = 0;
				param.aout_ref = 0;
				param.cs_out = 0;				
				qpnp_pin_config(motor_devdata->md_m0_gpio, &param);				
				gpio_set_value(motor_devdata->md_m0_gpio, 1);
				gpio_set_value(motor_devdata->md_m1_gpio, 1);
				mdmode = 16;
			break;

			case 32:
				param.mode  = 1;
				param.output_type = 0;
				param.invert = QPNP_PIN_INVERT_DISABLE;
				param.pull = 5;
				param.vin_sel = 2; // 1.8V
				//param.out_strength = 1; //QPNP_PIN_OUT_STRENGTH_LOW
				param.out_strength = 3; //QPNP_PIN_OUT_STRENGTH_HIGH
				param.src_sel = 0;
				param.master_en = 0;	//high impedance
				param.ain_route = 0;
				param.aout_ref = 0;
				param.cs_out = 0;	
				qpnp_pin_config(motor_devdata->md_vref_gpio, &param);
				
				param.mode  = 1;
				param.output_type = 0;
				param.invert = QPNP_PIN_INVERT_DISABLE;
				param.pull = 5;
				param.vin_sel = 0;
				param.out_strength = 1;
				param.src_sel = 0;
				param.master_en = 0;	
				param.ain_route = 0;
				param.aout_ref = 0;
				param.cs_out = 0;				
				qpnp_pin_config(motor_devdata->md_m0_gpio, &param);//Z state, not use gpio_set_value function
				gpio_set_value(motor_devdata->md_m1_gpio, 1);
				mdmode = 32;
			break;

			default:
				param.mode  = 1;
				param.output_type = 0;
				param.invert = QPNP_PIN_INVERT_DISABLE;
				param.pull = 5;
				param.vin_sel = 2; // 1.8V
				//param.out_strength = 1; //QPNP_PIN_OUT_STRENGTH_LOW
				param.out_strength = 3; //QPNP_PIN_OUT_STRENGTH_HIGH
				param.src_sel = 0;
				param.master_en = 0;	//high impedance
				param.ain_route = 0;
				param.aout_ref = 0;
				param.cs_out = 0;				
				qpnp_pin_config(motor_devdata->md_vref_gpio, &param);
				
				param.mode  = 1;
				param.output_type = 0;
				param.invert = QPNP_PIN_INVERT_DISABLE;
				param.pull = 5;
				param.vin_sel = 0;
				param.out_strength = 1;
				param.src_sel = 0;
				param.master_en = 0;	//high impedance
				param.ain_route = 0;
				param.aout_ref = 0;
				param.cs_out = 0;
				qpnp_pin_config(motor_devdata->md_m0_gpio, &param);//Z state, not use gpio_set_value function
				gpio_set_value(motor_devdata->md_m1_gpio, 1);
				mdmode = 32;
			break;
		}

		motor_devdata->data.pwm_count = motor_devdata->data.md_angle*mdmode*GEAR_RATIO_10/(10*ANGEL_PER_PULSE);

		switch(motor_devdata->data.md_speed)
		{
			case CAMERA_MOTOR_SPEED1:
				switch(mdmode)
				{
					case 32:
						period_ns = 26667;// 37.5KHZ. speed 1
					break;
					
					default:
						period_ns = 26667;// 37.5KHZ. speed 1
					break;
				}
			break;

			case CAMERA_MOTOR_SPEED2:
				switch(mdmode)
				{
					case 32:
						period_ns = 53000;// 18.75KHZ. speed 2
					break;
					
					default:
						period_ns = 53000;// 18.75KHZ. speed 2
					break;
				}
			break;

			case CAMERA_MOTOR_SPEED3:
				switch(mdmode)
				{
					case 32:
						period_ns = 106000;// 9.375KHZ. speed 3
					break;
					
					default:
						period_ns = 106000;// 9.375KHZ. speed 3
					break;
				}
			break;

			case CAMERA_MOTOR_SPEED4:
				switch(mdmode)
				{
					case 32:
						period_ns = 216*1000;// 4.6KHZ speed 4
					break;
					
					default:
						period_ns = 216*1000;// 4.6KHZ speed 4
					break;
				}
			break;

			case CAMERA_MOTOR_SPEED5:
				switch(mdmode)
				{
					case 32:
						period_ns = 520*1000;// 1.9KHZ speed 5
					break;
					
					default:
						period_ns = 520*1000;// 1.9KHZ speed 5
					break;
				}
			break;

			case CAMERA_MOTOR_SPEED6:
				switch(mdmode)
				{
					case 32:
						period_ns = 1000*1000;// 1000HZ  speed 6
					break;
					
					default:
						period_ns = 1000*1000;// 1000HZ  speed 6
					break;
				}
			break;	

			case CAMERA_MOTOR_SPEED7:
				switch(mdmode)
				{
					case 32:
						period_ns = 2000*1000;// 500HZ  speed 7
					break;
					
					default:
						period_ns = 2000*1000;// 500HZ  speed 7
					break;
				}
			break;	

			case CAMERA_MOTOR_SPEED8:
				switch(mdmode)
				{
					case 32:
						period_ns = 2500*1000;// 383HZ  speed 8
					break;
					
					default:
						period_ns = 2500*1000;// 383HZ  speed 8
					break;
				}
			break;

			case CAMERA_MOTOR_SPEED9:
				switch(mdmode)
				{
					case 32:
						period_ns = 5000*1000;// 171HZ  speed 9
					break;
					
					default:
						period_ns = 5000*1000;// 171HZ  speed 9
					break;
				}
			break;			


			default:
				switch(mdmode)
				{
					case 32:
						period_ns = 500*1000;//speed 5
					break;
					
					default:
						period_ns = 500*1000;//speed 5
					break;
				}
			break;
		}
		value = motor_devdata->data.pwm_count * period_ns;
		duty_ns =  (unsigned long)(period_ns/2);
	
		motor_devdata->data.pwm->pwm_config.pwm_duty = duty_ns;
		motor_devdata->data.pwm->pwm_config.pwm_period = period_ns;

		pwm_config(motor_devdata->data.pwm, duty_ns, period_ns);
		pwm_enable(motor_devdata->data.pwm);

        printk("motor time value = %llu.\n", value);
        
		nsecond = do_div(value, 1000000000);
		intsecond = (unsigned long) value;

		hrtimer_start(&motor_devdata->timer,
			ktime_set(intsecond /*s*/, nsecond /*ns*/ ),
			  		HRTIMER_MODE_REL);

        start_motor_flag = 1; // zhangqiang add  for motor blocking 
        wake_lock_timeout(&motor_suspend_wake_lock, 3 * HZ);        
	}
	else
	{
		hrtimer_cancel(&motor_devdata->timer);
		pwm_disable(motor_devdata->data.pwm);
        
        if(motor_devdata->data.md_speed > 5)
    		usleep(15*1000);
        
		printk("hrtimer_cancel\n");

		gpio_set_value(motor_devdata->md_vref_gpio, 0);
		gpio_set_value(motor_devdata->md_sleep_gpio, 0);
		gpio_set_value(motor_devdata->md_dir_gpio, 0);
		gpio_set_value(motor_devdata->md_m0_gpio, 0);
		gpio_set_value(motor_devdata->md_m1_gpio, 0);
		gpio_set_value(motor_devdata->md_step_gpio, 0);
		gpio_set_value(CAMERAMOTOR_POWER_GPIO, 0);
		
		param.mode  = 1;
		param.output_type = 0;
		param.invert = QPNP_PIN_INVERT_DISABLE;
		param.pull = 5;
		param.vin_sel = 2; // 1.8V
		//param.out_strength = 1; //QPNP_PIN_OUT_STRENGTH_LOW
		param.out_strength = 3; //QPNP_PIN_OUT_STRENGTH_HIGH
		param.src_sel = 0;
		param.master_en = 0;	//high impedance
		param.ain_route = 0;
		param.aout_ref = 0;
		param.cs_out = 0;		
		qpnp_pin_config(motor_devdata->md_vref_gpio, &param);

        start_motor_flag = 0; // zhangqiang add  for motor blocking 
        wake_unlock(&motor_suspend_wake_lock);
	}

	spin_unlock_irqrestore(&motor_devdata->data.lock, flags);
}

static struct device_attribute dev_attr_direction = {
	.attr = {
		 .name = "mddir",
		 .mode = S_IRUGO | S_IWUSR},
	.store = &direction_store,
	.show = &direction_show
};

static struct device_attribute dev_attr_speed = {
	.attr = {
		 .name = "mdspeed",
		 .mode = S_IRUGO | S_IWUSR},
	.store = &speed_store,
	.show = &speed_show
};

static struct device_attribute dev_attr_angel = {
	.attr = {
		 .name = "mdangel",
		 .mode = S_IRUGO | S_IWUSR},
	.store = &angel_store,
	.show = &angel_show
};

static struct device_attribute dev_attr_mode = {
	.attr = {
		 .name = "mdmode",
		 .mode = S_IRUGO | S_IWUSR},
	.store = &mode_store,
	.show = &mode_show
};

	
static struct device_attribute dev_attr_pwm_enable = {
	.attr = {
		 .name = "pwm_enable",
		 .mode = S_IRUGO | S_IWUSR},
	.store = &pwm_enable_store,
	.show = &pwm_enable_show
};

static struct device_attribute dev_attr_change_speed_pwm = {
	.attr = {
		 .name = "pwm_change_speed",
		 .mode = S_IRUGO | S_IWUSR},
	.store = &pwm_change_speed_store,
	.show = &pwm_change_speed_show,	
};

static const struct file_operations motor_fops = {
	.owner = THIS_MODULE,
};

static struct miscdevice motor_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "motor",
	.fops = &motor_fops,
};

static int cameramotor_dt(struct device *dev, struct CameraMotor_platform_data *pdata)
{
	struct device_node *np = dev->of_node;	
	int rc;
	dev_err(dev, "songxh read device tree\n");

	rc = gpio_tlmm_config(CAMERAMOTOR_CTRL, GPIO_CFG_ENABLE);
	if (rc) {
		printk(KERN_ERR "%s:songxh, gpio_tlmm_config(%#x)=%d\n", __func__, CAMERAMOTOR_CTRL, rc);
	}
	gpio_set_value(CAMERAMOTOR_POWER_GPIO, 0);
	
		
	motor_devdata->md_vref_gpio = of_get_named_gpio(np, "qcom,platform-vref-gpio", 0);
	if (!gpio_is_valid(motor_devdata->md_vref_gpio))
		pr_err("%s:%d, md-vref-gpio gpio not specified\n", __func__, __LINE__);

	motor_devdata->md_sleep_gpio = of_get_named_gpio(np, "qcom,platform-sleep-gpio", 0);
	if (!gpio_is_valid(motor_devdata->md_sleep_gpio))
		pr_err("%s:%d, md_sleep_gpio gpio not specified\n", __func__, __LINE__);

	motor_devdata->md_dir_gpio = of_get_named_gpio(np, "qcom,platform-dir-gpio", 0);
	if (!gpio_is_valid(motor_devdata->md_dir_gpio))
		pr_err("%s:%d, md_dir_gpio	gpio not specified\n",	__func__, __LINE__);

	motor_devdata->md_m0_gpio = of_get_named_gpio(np, "qcom,platform-m0-gpio", 0);
	if (!gpio_is_valid(motor_devdata->md_m0_gpio))
		pr_err("%s:%d, md_m0_gpio gpio not specified\n",	__func__, __LINE__);

	motor_devdata->md_m1_gpio = of_get_named_gpio(np, "qcom,platform-m1-gpio", 0);
	if (!gpio_is_valid(motor_devdata->md_m1_gpio))
		pr_err("%s:%d, md_m1_gpio gpio not specified\n",	__func__, __LINE__);

	motor_devdata->md_step_gpio = of_get_named_gpio(np, "qcom,platform-step-gpio", 0);
	if (!gpio_is_valid(motor_devdata->md_step_gpio))
		pr_err("%s:%d, md_step_gpio gpio not specified\n",	__func__, __LINE__);

	rc = gpio_request(motor_devdata->md_vref_gpio, "md_vref-gpio");
	if (rc) 
		pr_err("request md_vref-gpio gpio failed, rc=%d\n",rc);
	gpio_set_value(motor_devdata->md_vref_gpio, 0);
	
	rc = gpio_request(motor_devdata->md_sleep_gpio, "md-sleep-gpio");
	if (rc) 
		pr_err("request md_sleep_gpio gpio failed, rc=%d\n",rc);
	gpio_set_value(motor_devdata->md_sleep_gpio, 0);

	rc = gpio_request(motor_devdata->md_dir_gpio, "md-dir-gpio");
	if (rc) 
		pr_err("request md_dir_gpio gpio failed, rc=%d\n",rc);
	gpio_set_value(motor_devdata->md_dir_gpio, 0);
	
	rc = gpio_request(motor_devdata->md_m0_gpio, "md-m0-gpio");
	if (rc) 
		pr_err("request md_m0_gpio gpio failed, rc=%d\n",rc);
	gpio_set_value(motor_devdata->md_m0_gpio, 0);
	
	rc = gpio_request(motor_devdata->md_m1_gpio, "md-m1-gpio");
	if (rc) 
		pr_err("request md_m1_gpio gpio failed, rc=%d\n",rc);
	gpio_set_value(motor_devdata->md_m1_gpio, 0);
	
	rc = gpio_request(motor_devdata->md_step_gpio, "md-step-gpio");
	if (rc) 
		pr_err("request md_step_gpio gpio failed, rc=%d\n",rc);
	gpio_set_value(motor_devdata->md_step_gpio, 0);

	return 0;
}


static enum hrtimer_restart CameraMotor_timer_func(struct hrtimer *hrtimer)
{
	unsigned long flags;
	spin_lock_irqsave(&motor_devdata->data.lock, flags);
	motor_devdata->data.pwm_enable = 0;
	spin_unlock_irqrestore(&motor_devdata->data.lock, flags);
	schedule_work(&motor_devdata->data.work);
	return HRTIMER_NORESTART;
}

static void CameraMotor_init_timer(struct CameraMotor_dev *motor_dev)
{
	hrtimer_init(&motor_devdata->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	motor_devdata->timer.function = CameraMotor_timer_func;
}

static int CameraMotor_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;	
	struct device *dev ;
	struct CameraMotor_platform_data *pdata = client->dev.platform_data;

	motor_devdata = devm_kzalloc(&client->dev,
			sizeof(struct CameraMotor_dev), GFP_KERNEL);
	
	if (motor_devdata == NULL) {
		pr_err("kzalloc() failed.\n");
		return -ENOMEM;
	}
	motor_devdata->dev = &(client->dev);

	ret = misc_register(&motor_device);
	if (ret) 
	{
		pr_err("%s songxh : device_register failed\n", __FILE__);
		return ret;
	}

	if (!camera_motor_class) {
		printk("songxh camera_motor_class == NULL\n");
		camera_motor_class = class_create(THIS_MODULE, "motor");
		if (IS_ERR(camera_motor_class))
			return PTR_ERR(camera_motor_class);
	}

 	dev = device_create(camera_motor_class, NULL,
				MKDEV(0, 0), NULL, "cameramotor"); 

	if (IS_ERR(dev))
		return PTR_ERR(dev);
	
	device_create_file(dev, &dev_attr_direction);
	device_create_file(dev, &dev_attr_speed);
	device_create_file(dev, &dev_attr_angel);
	device_create_file(dev, &dev_attr_pwm_enable);  
	device_create_file(dev, &dev_attr_change_speed_pwm);  
	device_create_file(dev, &dev_attr_mode);

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct CameraMotor_platform_data), GFP_KERNEL);
		if (!pdata) {
			printk("Failed to allocate memory\n");
			return -ENOMEM;
		}

		ret = cameramotor_dt(&client->dev, pdata);
		if (ret)
			return ret;
	} else
	{
		pdata = client->dev.platform_data;
	}

	motor_devdata->data.pwm = pwm_request(4, "motorpwm");
	pwm_config_pwm_value(motor_devdata->data.pwm, 128);
	
	if (IS_ERR(motor_devdata->data.pwm)) {
		printk("songxh CameraMotor_probe  pwm_request error \n");
	} else
		printk("songxh CameraMotor_probe  got pwm for camera motor\n");

	spin_lock_init(&motor_devdata->data.lock);

	CameraMotor_init_timer(motor_devdata);

	INIT_WORK(&motor_devdata->data.work, CameraMotor_running);

	wake_lock_init(&motor_suspend_wake_lock, WAKE_LOCK_SUSPEND, "motor_suspend_lock");
	
	printk("songxh CameraMotor_probe end \n");

	return ret;
}

static int CameraMotor_remove(struct i2c_client *client)
{
	struct CameraMotor_dev *motor_dev;

	motor_dev = i2c_get_clientdata(client);
	misc_deregister(&motor_dev->motor_device);
	//gpio_free(motor_dev->ven_gpio);
	gpio_free(CAMERAMOTOR_POWER_GPIO);
	gpio_free(motor_dev->md_vref_gpio);	
	gpio_free(motor_dev->md_dir_gpio);
	gpio_free(motor_dev->md_m0_gpio);
	gpio_free(motor_dev->md_m1_gpio);
	gpio_free(motor_dev->md_sleep_gpio);
	kfree(motor_dev);

	return 0;
}
#ifdef CONFIG_OF
static struct of_device_id CameraMotor_of_match_table[] = {
	{ .compatible = "cameramotor,drv8834",},
	{ },
};
#else
#define CameraMotor_of_match_table NULL
#endif

static const struct i2c_device_id CameraMotor_id[] = {
	{ "motor", 0 },
	{ }
};

static struct i2c_driver CameraMotor_driver = {
	.id_table	= CameraMotor_id,
	.probe		= CameraMotor_probe,
	.remove		= CameraMotor_remove,
	.driver		= 
	{
		.owner	= THIS_MODULE,
		.name	= "motor",
		.of_match_table = CameraMotor_of_match_table,
	},
};

/*
 * module load/unload record keeping
 */

static int __init CameraMotor_dev_init(void)
{    
	int result;
	pr_info("songxh Camera motor driver init\n");
	result = i2c_add_driver(&CameraMotor_driver);
	return result;
}
module_init(CameraMotor_dev_init);

static void __exit CameraMotor_dev_exit(void)
{
	pr_info("Unloading Camera motor driver\n");
	i2c_del_driver(&CameraMotor_driver);
}
module_exit(CameraMotor_dev_exit);

MODULE_AUTHOR("Xinhua.Song");
MODULE_DESCRIPTION("CAMERA MOTOR driver");
MODULE_LICENSE("GPL");
