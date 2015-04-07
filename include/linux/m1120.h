#ifndef __M1120_H__
#define __M1120_H__

#include <linux/ioctl.h>
#include <linux/mutex.h>

/* ********************************************************* */
/* feature of ic revision */
/* ********************************************************* */
#define M1120_REV_0_2						(0x02)
#define M1120_REV_1_0						(0x10)
#define M1120_REV							M1120_REV_1_0
#define M1120_DRIVER_VERSION				"Ver1.04-140214"
/* ********************************************************* */


/* ********************************************************* */
/* property of driver */
/* ********************************************************* */
#define M1120_DRIVER_NAME					"m1120"
#define M1120_IRQ_NAME						"m1120-irq"
#define M1120_PATH							"/dev/m1120"
#define M1120_SLAVE_ADDR					(0x18)
/* ********************************************************* */


/* ********************************************************* */
/* register map */
/* ********************************************************* */
#define M1120_REG_PERSINT					(0x00)
#define M1120_VAL_PERSINT_COUNT			    (0x80)					
#define M1120_VAL_PERSINT_INTCLR			(0x01)
	/*
		[7:4]	PERS		: interrupt persistence count
		[0]		INTCLR	= 1 : interrupt clear
	*/
/* --------------------------------------------------------- */
#define M1120_REG_INTSRS					(0x01)
#define M1120_VAL_INTSRS_INT_ON				(0x80)
#define M1120_DETECTION_MODE_INTERRUPT		M1120_VAL_INTSRS_INT_ON
#define M1120_VAL_INTSRS_INT_OFF			(0x00)
#define M1120_DETECTION_MODE_POLLING		M1120_VAL_INTSRS_INT_OFF
#define M1120_VAL_INTSRS_INTTYPE_BESIDE		(0x40)
#define M1120_VAL_INTSRS_INTTYPE_WITHIN		(0x00)
#define M1120_VAL_INTSRS_SRS_0_068mT		(0x00)
#define M1120_VAL_INTSRS_SRS_0_034mT		(0x01)
#define M1120_VAL_INTSRS_SRS_0_017mT		(0x02)
#define M1120_VAL_INTSRS_SRS_0_009mT		(0x03)
#define M1120_VAL_INTSRS_SRS_0_004mT		(0x04)
	/*
		[7]		INTON	= 0 : disable interrupt
		[7]		INTON	= 1 : enable interrupt
		[4]		INT_TYP = 0 : generate inteerupt when raw data is within range of threshold
		[4]		INT_TYP = 1 : generate interrupt when raw data is beside range of threshold
		[2:0]	SRS			: select sensitivity type
					000		: 0.068 (mT/LSB)
					001		: 0.034 (mT/LSB)
					010		: 0.017 (mT/LSB)
					011		: 0.009 (mT/LSB)
					100		: 0.004 (mT/LSB)
					101		: 0.017 (mT/LSB)
					110		: 0.017 (mT/LSB)
					111		: 0.017 (mT/LSB)
	*/
/* --------------------------------------------------------- */
#define M1120_REG_LTHL						(0x02)
	/*
		[7:0]	LTHL		: low byte of low threshold value	
	*/
/* --------------------------------------------------------- */
#define M1120_REG_LTHH						(0x03)
	/*
		[7:6]	LTHH		: high 2bits of low threshold value with sign
	*/
/* --------------------------------------------------------- */
#define M1120_REG_HTHL						(0x04)
	/*
		[7:0]	HTHL		: low byte of high threshold value	
	*/
/* --------------------------------------------------------- */
#define M1120_REG_HTHH						(0x05)
	/*
		[7:6]	HTHH		: high 2bits of high threshold value with sign
	*/
/* --------------------------------------------------------- */
#define M1120_REG_I2CDIS					(0x06)
#define M1120_VAL_I2CDISABLE					(0x37)
	/*
		[7:0]	I2CDIS		: disable i2c
	*/
/* --------------------------------------------------------- */
#define M1120_REG_SRST						(0x07)
#define M1120_VAL_SRST_RESET				(0x01)
	/*
		[0]		SRST	= 1	: soft reset
	*/
/* --------------------------------------------------------- */
#define M1120_REG_OPF						(0x08)
#define M1120_VAL_OPF_FREQ_20HZ				(0x00)
#define M1120_VAL_OPF_FREQ_10HZ				(0x10)
#define M1120_VAL_OPF_FREQ_6_7HZ			(0x20)
#define M1120_VAL_OPF_FREQ_5HZ				(0x30)
#define M1120_VAL_OPF_FREQ_80HZ				(0x40)
#define M1120_VAL_OPF_FREQ_40HZ				(0x50)
#define M1120_VAL_OPF_FREQ_26_7HZ			(0x60)
#define M1120_VAL_OPF_EFRD_ON				(0x08)
#define M1120_VAL_OPF_BIT_8					(0x02)
#define M1120_VAL_OPF_BIT_10				(0x00)
#define M1120_VAL_OPF_HSSON_ON				(0x01)
	/*
		[6:4]	OPF			: operation frequency
					000		: 20	(Hz)
					001		: 10	(Hz)
					010		: 6.7	(Hz)
					011		: 5		(Hz)
					100		: 80	(Hz)
					101		: 40	(Hz)
					110		: 26.7	(Hz)
					111		: 20	(Hz)
		[3]		EFRD	= 0	: keep data without accessing eFuse
		[3]		EFRD	= 1	: update data after accessing eFuse
		[1]		BIT		= 0	: 10 bit resolution
		[1]		BIT		= 1	: 8 bit resolution
		[0]		HSSON	= 0 : Off power down mode
		[0]		HSSON	= 1 : On power down mode

	*/
/* --------------------------------------------------------- */
#define M1120_REG_DID						(0x09)
#define M1120_VAL_DID						(0x9C)
	/*
		[7:0]	DID			: Device ID
	*/
/* --------------------------------------------------------- */
#define M1120_REG_INFO						(0x0A)
	/*
		[7:0]	INFO		: Information about IC
	*/
/* --------------------------------------------------------- */
#define M1120_REG_ASA						(0x0B)
	/*
		[7:0]	ASA			: Hall Sensor sensitivity adjustment
	*/
/* --------------------------------------------------------- */
#define M1120_REG_ST1						(0x10)
#define M1120_VAL_ST1_DRDY					(0x01)
	/*
		[4]	INTM			: status of interrupt mode
		[1]	BITM			: status of resolution
		[0]	DRDY			: status of data ready
	*/
/* --------------------------------------------------------- */
#define M1120_REG_HSL						(0x11)
	/*
		[7:0]	HSL			: low byte of hall sensor measurement data
	*/
/* --------------------------------------------------------- */
#define M1120_REG_HSH						(0x12)
	/*
		[7:6]	HSL			: high 2bits of hall sensor measurement data with sign
	*/
/* ********************************************************* */


/* ********************************************************* */


/* ********************************************************* */
/* ioctl command */
/* ********************************************************* */
typedef  struct 
{
    short cali_flag;
    short value_30degree;    
    short value_70degree;        
    short lowthd;
    short highthd;
}RotorDetectionCaliData;

#define RHALL_IOCTL_BASE                   (0x89)
#define RHALL_IOCTL_SET_ENABLE				_IOW(RHALL_IOCTL_BASE, 0x00, int)
#define RHALL_IOCTL_GET_ENABLE				_IOR(RHALL_IOCTL_BASE, 0x01, int)
#define RHALL_IOCTL_SET_DELAY				_IOW(RHALL_IOCTL_BASE, 0x02, int)
#define RHALL_IOCTL_GET_DELAY				_IOR(RHALL_IOCTL_BASE, 0x03, int)
#define RHALL_IOCTL_SET_CALIBRATION			_IOW(RHALL_IOCTL_BASE, 0x04, RotorDetectionCaliData)
#define RHALL_IOCTL_GET_CALIBRATED_DATA		_IOR(RHALL_IOCTL_BASE, 0x05, int*)
#define RHALL_IOCTL_SET_INTERRUPT			_IOW(RHALL_IOCTL_BASE, 0x06, unsigned int)
#define RHALL_IOCTL_GET_INTERRUPT			_IOR(RHALL_IOCTL_BASE, 0x07, unsigned int*)
#define RHALL_IOCTL_SET_THRESHOLD_HIGH		_IOW(RHALL_IOCTL_BASE, 0x08, unsigned int)
#define RHALL_IOCTL_GET_THRESHOLD_HIGH		_IOR(RHALL_IOCTL_BASE, 0x09, unsigned int*)
#define RHALL_IOCTL_SET_THRESHOLD_LOW		_IOW(RHALL_IOCTL_BASE, 0x0A, unsigned int)
#define RHALL_IOCTL_GET_THRESHOLD_LOW		_IOR(RHALL_IOCTL_BASE, 0x0B, unsigned int*)
#define RHALL_IOCTL_SET_REG					_IOW(RHALL_IOCTL_BASE, 0x0C, int)
#define RHALL_IOCTL_GET_REG					_IOR(RHALL_IOCTL_BASE, 0x0D, int)
#define RHALL_IOCTL_GET_DATA					_IOR(RHALL_IOCTL_BASE, 0x0E, int*)
#define RHALL_IOCTL_GET_CALIBRATION			_IOR(RHALL_IOCTL_BASE, 0x0F, RotorDetectionCaliData)
#define RHALL_IOCTL_GET_TYPE					_IOR(RHALL_IOCTL_BASE, 0x1F, int)
/* ********************************************************* */


/* ********************************************************* */
/* event property */
/* ********************************************************* */
#define DEFAULT_EVENT_TYPE					EV_ABS
#define DEFAULT_EVENT_CODE					ABS_X
#define DEFAULT_EVENT_DATA_CAPABILITY_MIN	(-32768)
#define DEFAULT_EVENT_DATA_CAPABILITY_MAX	(32767)
/* ********************************************************* */
/* delay property */
/* ********************************************************* */
#define M1120_DELAY_MAX						(200)	// ms
#define M1120_DELAY_MIN						(50)	// ms
#define M1120_DELAY_FOR_READY				(60)	// ms
/* ********************************************************* */


/* ********************************************************* */
/* data type for driver */
/* ********************************************************* */

enum {
	OPERATION_MODE_POWERDOWN,
	OPERATION_MODE_MEASUREMENT,
	OPERATION_MODE_FUSEROMACCESS
};

#define M1120_REG_NUM						(15)
typedef union {
	struct {
		unsigned char persint;
		unsigned char intsrs;
		unsigned char lthl;
		unsigned char lthh;
		unsigned char hthl;
		unsigned char hthh;
		unsigned char i2cdis;
		unsigned char srst;
		unsigned char opf;
		unsigned char did;
		unsigned char info;
		unsigned char asa;
		unsigned char st1;
		unsigned char hsl;
		unsigned char hsh;
	} map;
	unsigned char array[M1120_REG_NUM];
} m1120_reg_t;

typedef struct {
	struct mutex	enable;
	struct mutex	data;
} m1120_mutex_t;

typedef struct {
	atomic_t enable;
	atomic_t delay;
	atomic_t debug;
} m1120_atomic_t;

typedef struct {
	int					power_vi2c;
	int					power_vdd;
	int					interrupt_gpio;
	int					interrupt_irq;
} m1120_platform_data_t;

typedef struct {
	struct i2c_client	*client;
	struct input_dev	*input_dev;
	m1120_mutex_t		mtx;
	m1120_atomic_t		atm;
	m1120_reg_t			reg;
	bool				irq_enabled;
	int					calibrated_data;
	int					irq_source;
    short               value_30degree;
    short               value_70degree;
	short				thrhigh;
	short				thrlow;
    bool                last_state;
	struct delayed_work	work;
        
	struct regulator *					power_vi2c;
	struct regulator *					power_vdd;
	int					igpio;
	int					irq;
} m1120_data_t;

/* ********************************************************* */

#endif // __M1120_H__

