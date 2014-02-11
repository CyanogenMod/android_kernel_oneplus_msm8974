/*
 * es325.c  --	Audience eS325 ALSA SoC Audio driver
 *
 * Copyright 2011 Audience, Inc.
 *
 * Author: Greg Clemson <gclemson@audience.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CONFIG_VENDOR_EDIT
#define CONFIG_VENDOR_EDIT
#endif

#define DEBUG
//#define ES325_QCOM_V1
#define ES325_QCOM_V2
#define BUS_TRANSACTIONS
#define FIXED_CONFIG

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/completion.h>
#include <linux/i2c.h>
#include <linux/slimbus/slimbus.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <linux/kthread.h>

#include <linux/i2c/esxxx.h> /* TODO: common location for i2c and slimbus */
#include "es325.h"
#include "es325-export.h"
#include <linux/qpnp/clkdiv.h>
#include <linux/of_gpio.h>
//liuyan add for dvt
#ifdef CONFIG_VENDOR_EDIT
#include <mach/sps.h>
#include"../../../drivers/slimbus/slim-msm.h"
#include <linux/pcb_version.h>
#endif


#if defined(ES325_QCOM_V1)
#define ES325_DAI_ID_BASE	1
#define DAI_INDEX(xid)		(xid - 1)
#elif defined(ES325_QCOM_V2)
#define ES325_DAI_ID_BASE	0
#define DAI_INDEX(xid)		(xid)
#else
#error "no QCOM implementation specified"
#endif

#define ES325_CMD_ACCESS_WR_MAX 4
#define ES325_CMD_ACCESS_RD_MAX 4
struct es325_cmd_access {
	u8 read_msg[ES325_CMD_ACCESS_RD_MAX];
	unsigned int read_msg_len;
	u8 write_msg[ES325_CMD_ACCESS_WR_MAX];
	unsigned int write_msg_len;
	unsigned int val_shift;
	unsigned int val_max;
};

#include "es325-access.h"

#define ES325_SLIM_CH_RX_OFFSET		152
#define ES325_SLIM_CH_TX_OFFSET		156
/* #define ES325_SLIM_RX_PORTS		10 */
#define ES325_SLIM_RX_PORTS		6
#define ES325_SLIM_TX_PORTS		6

enum {
	ES325_SLIM_1_PB = ES325_DAI_ID_BASE,
	ES325_SLIM_1_CAP,
	ES325_SLIM_2_PB,
	ES325_SLIM_2_CAP,
	ES325_SLIM_3_PB,
	ES325_SLIM_3_CAP,
};

enum es325_power_state {
ES325_POWER_BOOT,
ES325_POWER_SLEEP,
ES325_POWER_SLEEP_PENDING,
ES325_POWER_AWAKE
};

#define ES325_SLIM_1_PB_MAX_CHANS	2
#define ES325_SLIM_1_CAP_MAX_CHANS	2
#define ES325_SLIM_2_PB_MAX_CHANS	2
#define ES325_SLIM_2_CAP_MAX_CHANS	2
#define ES325_SLIM_3_PB_MAX_CHANS	2
#define ES325_SLIM_3_CAP_MAX_CHANS	2

#define ES325_SLIM_1_PB_OFFSET	0
#define ES325_SLIM_2_PB_OFFSET	2
#define ES325_SLIM_3_PB_OFFSET	4
#define ES325_SLIM_1_CAP_OFFSET	0
#define ES325_SLIM_2_CAP_OFFSET	2
#define ES325_SLIM_3_CAP_OFFSET	4

#define ES325_NUM_CODEC_SLIM_DAIS	6

static unsigned int es325_fw_ready = 0;
/*add for the Lenovo K6 EVT board*/	

#if 0
#undef CDBG
#ifdef CONFIG_ES325_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif
#else
#undef CDBG
#define CDBG(fmt, args...)  do { } while (0)
#endif


struct es325_slim_dai_data {
	unsigned int rate;
	unsigned int *ch_num;
	unsigned int ch_act;
	unsigned int ch_tot;
};

struct es325_slim_ch {
	u32	sph;
	u32	ch_num;
	u16	ch_h;
	u16	grph;
};

/* codec private data */
struct es325_priv {
	struct snd_soc_codec *codec;
	struct firmware *fw;

	struct esxxx_platform_data *pdata;

	struct i2c_client *this_client;
	struct slim_device *intf_client;
	struct slim_device *gen0_client;
	struct es325_slim_dai_data dai[ES325_NUM_CODEC_SLIM_DAIS];
	struct es325_slim_ch slim_rx[ES325_SLIM_RX_PORTS];
    struct es325_slim_ch slim_tx[ES325_SLIM_TX_PORTS];
    struct delayed_work sleep_work;
    enum es325_power_state power_state;
    struct mutex power_lock;
} es325_priv;

enum es325_vreg_type {
	REG_LDO,
	REG_VS,
	REG_GPIO,
};


struct es325_vreg_t {
	const char *reg_name;
	enum es325_vreg_type type;
	int min_voltage;
	int max_voltage;
	int op_mode;
	uint32_t delay;
};

#define ES325_SLEEP_DELAY 300
static unsigned int es325_ap_tx1_ch_cnt = 2;
unsigned int es325_rx1_route_enable = 0;
unsigned int es325_rx2_route_enable = 0;
unsigned int es325_tx1_route_enable = 0;
static unsigned int es325_power_state = 1;

#if defined(CONFIG_SND_SOC_WCD9310)
static int es325_slim_rx_port_to_ch[ES325_SLIM_RX_PORTS] = {
	152, 153, 154, 155, 134, 135
};
static int es325_slim_tx_port_to_ch[ES325_SLIM_TX_PORTS] = {
	156, 157, 138, 139, 143, 144
};
#elif defined(CONFIG_SND_SOC_WCD9320)
static int es325_slim_rx_port_to_ch[ES325_SLIM_RX_PORTS] = {
	152, 153, 154, 155, 134, 135
};
static int es325_slim_tx_port_to_ch[ES325_SLIM_TX_PORTS] = {
	156, 157, 144, 145, 146, 147
};
#else
#error "no CODEC port to channel map specified"
#endif

static int es325_slim_be_id[ES325_NUM_CODEC_SLIM_DAIS] = {
	ES325_SLIM_2_CAP, /* for ES325_SLIM_1_PB tx from es325 */
	ES325_SLIM_3_PB, /* for ES325_SLIM_1_CAP rx to es325 */
	ES325_SLIM_3_CAP, /* for ES325_SLIM_2_PB tx from es325 */
	-1, /* for ES325_SLIM_2_CAP */
	-1, /* for ES325_SLIM_3_PB */
	-1, /* for ES325_SLIM_3_CAP */
};

#define ES325_CUSTOMER_PROFILE_MAX 4
static u8 es325_audio_custom_profiles[ES325_CUSTOMER_PROFILE_MAX][100] = {
	{
        //Softness
		0x80, 0x2f, 0x00, 0x3e,
        0x04, 0x11, 0x00, 0x01,
        0x00, 0x04, 0x00, 0x00,
        0xe3, 0xdc, 0xef, 0xea,
        0xe0, 0xea, 0xd0, 0xd7,
        0x9e, 0xc0, 0x9e, 0x92,
        0xb2, 0xa9, 0xb7, 0xb3,
        0xbb, 0xba, 0xd7, 0xc6,
        0xdc, 0xdf, 0xdd, 0xdc,
        0xe4, 0xdf, 0xed, 0xeb,
        0xf9, 0xf4, 0x02, 0xff,
        0x07, 0x04, 0x0c, 0x08,
        0x0e, 0x0e, 0x0d, 0x0f,
        0x0f, 0x0f, 0x07, 0x0a,
        0xdb, 0xfa, 0xac, 0xbb,
        0x91, 0x9b, 0x00, 0x82,
        0x94, 0x14,
       	0xff		/* terminate */
	}, 
    {
        //Brightness
		0x80, 0x2f, 0x00, 0x3e,
        0x04, 0x11, 0x00, 0x01,
        0x00, 0x05, 0x00, 0x00,
        0x19, 0x17, 0x1c, 0x1b,
        0x18, 0x1b, 0x13, 0x15,
        0xf5, 0x09, 0xf6, 0xed,
        0x02, 0xfe, 0x00, 0x00,
        0x00, 0xff, 0xf9, 0xfe,
        0xf6, 0xf7, 0xf4, 0xf5,
        0xf2, 0xf3, 0xf0, 0xf1,
        0xf1, 0xf1, 0xf2, 0xf2,
        0xf3, 0xf2, 0xf1, 0xf2,
        0xee, 0xef, 0xeb, 0xed,
        0xe8, 0xea, 0xdf, 0xe4,
        0xd4, 0xda, 0xc7, 0xcd,
        0xbc, 0xc0, 0x00, 0xb6,
        0x28, 0xbf,
        0xff		/* terminate */
	},
	{
		0x80, 0x2f, 0x00, 0x3e,
		0x04, 0x11, 0x00, 0x01,
		0x00, 0x04, 0x00, 0x00,
		0x2a, 0x2b, 0x29, 0x2a,
		0x2b, 0x2a, 0x2c, 0x2c,
		0x22, 0x29, 0x19, 0x1c,
		0x12, 0x16, 0x01, 0x0b,
		0xe9, 0xf6, 0xc0, 0xd6,
		0xb0, 0xb3, 0xad, 0xae,
		0xbd, 0xb2, 0xcf, 0xc8,
		0xd3, 0xd2, 0xd5, 0xd5,
		0xd6, 0xd5, 0xe6, 0xdb,
		0xfa, 0xf2, 0x04, 0xff,
		0x06, 0x06, 0x06, 0x06,
		0x06, 0x06, 0x06, 0x06,
		0x05, 0x06, 0x00, 0x05,
		0xa7, 0x5b,
		0xff		/* terminate */
	},
};
#define ES325_INTERNAL_ROUTE_MAX 20//12
static long es325_internal_route_num;
static u8 es325_internal_route_configs[ES325_INTERNAL_ROUTE_MAX][80] = {
	/* [0]: 1-mic Headset */
	{
		0xb0, 0x5c, 0x00, 0x01, /* Algo = Voice processing  */
		0xb0, 0x4c, 0x00, 0x00, /* Algo rate = 8kHz  */
		0xb0, 0x5e, 0x00, 0x01, /* Mix rate = 16 kHz  */
		0xb0, 0x5a, 0x28, 0xa0, /* Set SBUS.Rx0 -> UITONE1 (APQ..325) */
		0xb0, 0x5a, 0x14, 0x60, /* Set  PORTD.Left -> FEIN (MDM..325) */
		0xb0, 0x5a, 0x04, 0xa4, /* Set SBUS.Rx4 -> PRI (codec..325) */
		0xb0, 0x5a, 0x40, 0x60, /* Set PORTD.Left <- CSOUT (325..MDM) */
		0xb0, 0x5a, 0x44, 0xac, /* Set SBUS.Tx2 <- FEOUT1 (325..codec) */
		0xb0, 0x5a, 0x48, 0xad, /* Set SBUS.Tx3 <- FEOUT2 (325..codec) */
//		0xb0, 0x5a, 0x60, 0xaa, /* REC -> CSOUT */
//	    0xb0, 0x23, 0x00, 0x03, /* Uplink + Downlink mix */
//		0xb0, 0x17, 0x00, 0x02, /* Mic config = ...  */
//		0xb0, 0x18, 0x00, 0x03, /*	... 1-MIC DV Need FIXED  */
		0xb0, 0x15, 0x05, 0x0c, /* rx input gain up 12dB */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT) */
		0xff			/* terminate */
	},

	/* [1]: 1-mic CS Voice (DV) */
	{
		0xb0, 0x5c, 0x00, 0x01, /* Algo = Voice processing */
		0xb0, 0x4c, 0x00, 0x00, /* Algo rate = 8kHz  */
		0xb0, 0x5e, 0x00, 0x01, /* Mix rate = 16 kHz  */
		0xb0, 0x5a, 0x28, 0xa0, /* Set SBUS.Rx0 -> UITONE1 (APQ..325) */
		0xb0, 0x5a, 0x14, 0x60, /* Set  PORTD.Left -> FEIN (MDM..325) */
		0xb0, 0x5a, 0x04, 0xa4, /* Set SBUS.Rx4 -> PRI (codec..325) */
		0xb0, 0x5a, 0x40, 0x60, /* Set PORTD.Left <- CSOUT (325..MDM) */
		0xb0, 0x5a, 0x44, 0xac, /* Set SBUS.Tx2 <- FEOUT1 (325..codec) */
		0xb0, 0x5a, 0x60, 0xaa, /* REC -> CSOUT */
		0xb0, 0x23, 0x00, 0x03, /* Uplink + Downlink mix */
		0xb0, 0x17, 0x00, 0x02, /* Mic config = ...  */
		0xb0, 0x18, 0x00, 0x02, /*	... 1-MIC DV  */
//		0xb0, 0x15, 0x05, 0x06, /* rx input gain up 6dB */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off  */
		0xff				/* terminate */
	},

	/* [2]: 2-mic CS Voice (CT) */
	{
		0xb0, 0x5c, 0x00, 0x01, /* Algo = Voice processing */
		0xb0, 0x4c, 0x00, 0x00, /* Algo rate = 8 kHz */
		0xb0, 0x5e, 0x00, 0x01, /* Mix rate = 16 kHz */
		0xb0, 0x5a, 0x28, 0xa0, /* Set SBUS.Rx0 -> UITONE1 (APQ..325) */
		0xb0, 0x5a, 0x14, 0x60, /* Set  PORTD.Left -> FEIN (MDM..325) */
		0xb0, 0x5a, 0x04, 0xa4, /* Set SBUS.Rx4 -> PRI (codec..325) */
		0xb0, 0x5a, 0x08, 0xa5, /* Set SBUS.Rx5 -> SEC (codec..325) */
		0xb0, 0x5a, 0x40, 0x60, /* Set PORTD.Left <- CSOUT (325..MDM) */
		0xb0, 0x5a, 0x44, 0xac, /* Set SBUS.Tx2 <- FEOUT1 (325..codec) */
		0xb0, 0x5a, 0x60, 0xaa, /* REC -> CSOUT */
		0xb0, 0x23, 0x00, 0x03, /* Uplink + Downlink mix */
		0xb0, 0x17, 0x00, 0x02, /* Mic config = ...  */
		0xb0, 0x18, 0x00, 0x00, /*	... 2-MIC CT  */
	//	0xb0, 0x15, 0x05, 0x0c, /* rx input gain up 12dB */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT) */
		0xff				/* terminate */
	},

	/* [3]: BT testing */ 
	{
		0xb0, 0x5c, 0x00, 0x01, /* Algo = Voice processing  */
		0xb0, 0x4c, 0x00, 0x00, /* Algo rate = 8kHz  */
		0xb0, 0x5e, 0x00, 0x01, /* Mix rate = 16 kHz  */
		0xb0, 0x5a, 0x28, 0xa4, /* Set SBUS.Rx4 -> UITONE1 (APQ..325) */
		0xb0, 0x5a, 0x14, 0x60, /* Set  PORTD.Left -> FEIN (MDM..325) */
		0xb0, 0x5a, 0x04, 0xa0, /* Set SBUS.Rx4 -> PRI (codec..325) */
		0xb0, 0x5a, 0x40, 0x60, /* Set PORTD.Left <- CSOUT (325..MDM) */
		0xb0, 0x5a, 0x44, 0xaa, /* Set SBUS.Tx0 <- FEOUT1 (325..codec) */
	//	0xb0, 0x15, 0x05, 0x12, /* rx input gain up 12dB */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT) */
		0xff			/* terminate */
	}
	, 
    {
		0xb0, 0x5c, 0x00, 0x01, /* Algo = Voice processing */
		0xb0, 0x4c, 0x00, 0x00, /* Algo rate = 8kHz  */
		0xb0, 0x5e, 0x00, 0x01, /* Mix rate = 16 kHz  */
		0xb0, 0x5a, 0x28, 0xa0, /* Set SBUS.Rx0 -> UITONE1 (APQ..325) */
		0xb0, 0x5a, 0x14, 0x60, /* Set  PORTD.Left -> FEIN (MDM..325) */
		0xb0, 0x5a, 0x04, 0xa4, /* Set SBUS.Rx4 -> PRI (codec..325) */
		0xb0, 0x5a, 0x40, 0x60, /* Set PORTD.Left <- CSOUT (325..MDM) */
		0xb0, 0x5a, 0x44, 0xac, /* Set SBUS.Tx2 <- FEOUT1 (325..codec) */
		0xb0, 0x5a, 0x60, 0xaa, /* REC -> CSOUT */
		0xb0, 0x23, 0x00, 0x03, /* Uplink + Downlink mix */
		0xb0, 0x17, 0x00, 0x02, /* Mic config = ...  */
		0xb0, 0x18, 0x00, 0x03, /*	... 1-MIC DV  */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off  */
		0xff				/* terminate */
	}
	,

	/* [5]: Port D settings, 1 channel */
	{
		0xb0, 0x4c, 0x00, 0x00, /* Algo rate = 8kHz NB */
                0xb0, 0x0c, 0x0d,0x07, /* PORT D audio port mode, */
		0xb0, 0x0d, 0x00,0x00, /* set device parm  pcm*/
                0xb0, 0x0c, 0x0d,0x09, /* set PCM3 master, */
                0xb0, 0x0d, 0x31,0x08, /* 8 kHz*/ 
		0xb0,0x0C, 0x0D,0x01,
		0xb0,0x0D, 0x00,0x00 ,// 0x800C:SetDeviceParmId, 0x0D01:PORTD TDM Time Slots, 0x800D:SetDeviceParm, 0x0000:1 Slots

		0xb0,0x0C, 0x0D,0x02,
		0xb0,0x0D, 0x00,0x01 ,// 0x800C:SetDeviceParmId, 0x0D02:PORTD Delay from Tx Frame sync, 0x800D:SetDeviceParm, 0x0001: 1 clock
		0xb0,0x0C, 0x0D,0x03,
		0xb0,0x0D, 0x00,0x01 ,// 0x800C:SetDeviceParmId, 0x0D03:PORTD Delay from Rx Frame sync, 0x800D:SetDeviceParm, 0x0001: 1 clock
		0xff				/* terminate */
	},

	/* [6]: Audio playback, 2 channels */
	{

		0xb0, 0x5c, 0x00, 0x04, /* Algo = 4 chan pass  */
		0xb0, 0x4c, 0x00, 0x01, /* Algo rate = 16kHz  */
		0xb0, 0x5a, 0x18, 0xa0, /* Set SBUS.Rx0 -> AUDIN1  */
		0xb0, 0x5a, 0x1c, 0xa1, /* Set SBUS.Rx1 -> AUDIN2  */
		0xb0, 0x5a, 0x4c, 0xac, /* Set SBUS.Tx2 <- AUDOUT1 */
		0xb0, 0x5a, 0x50, 0xad, /* Set SBUS.Tx3 <- AUDOUT2 */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT)  */
		0xff				/* terminate */
	},

	/* [7]: Audio record, 1 channel */
	{
		0xb0, 0x5c, 0x00, 0x04, /* Algo = 4 chan pass  */
		0xb0, 0x4c, 0x00, 0x01, /* Algo rate = 16kHz  */
		0xb0, 0x5a, 0x18, 0xa4, /* Set SBUS.Rx4 -> AUDIN1  */
		0xb0, 0x5a, 0x4c, 0xaa, /* Set SBUS.Tx0 <- AUDOUT1 */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT)  */
		0xff				/* terminate */
	},

	/* [8]: Audio record, 2 channels */
	{
		0xb0, 0x5c, 0x00, 0x04, /* Algo = 4 chan pass  */
		0xb0, 0x4c, 0x00, 0x01, /* Algo rate = 16kHz  */
		0xb0, 0x5a, 0x18, 0xa4, /* Set SBUS.Rx4 -> AUDIN1 */
		0xb0, 0x5a, 0x1c, 0xa5, /* Set SBUS.Rx5 -> AUDIN2 */
		0xb0, 0x5a, 0x4c, 0xaa, /* Set SBUS.Tx0 <- AUDOUT1 */
		0xb0, 0x5a, 0x50, 0xab, /* Set SBUS.Tx1 <- AUDOUT2 */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT)  */
		0xff				/* terminate */
	},

	/* [9]: 2-mic FT ASR */
	{
		0xb0, 0x5c, 0x00, 0x01, /* Algo = Voice processing */
		0xb0, 0x4c, 0x00, 0x01, /* Algo rate = 16 kHz */
		0xb0, 0x5e, 0x00, 0x01, /* Mix rate = 16 kHz */
		0xb0, 0x5a, 0x04, 0xa4, /* Set SBUS.Rx4 -> PRI (codec..325) */
		0xb0, 0x5a, 0x08, 0xa5, /* Set SBUS.Rx5 -> SEC (codec..325) */
		0xb0, 0x5a, 0x40, 0xaa, /* Set SBUS.Tx0 <- CSOUT (325..MDM) */
		0xb0, 0x17, 0x00, 0x02, /* Mic config = ... */
		0xb0, 0x18, 0x00, 0x06, /*   ... 2-MIC ASR (AF) */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT) */
		0xff			/* terminate */
	},

	/* [10]: 1-mic External ASR */
	{
		0xb0, 0x5c, 0x00, 0x01, /* Algo = Voice processing */
		0xb0, 0x4c, 0x00, 0x01, /* Algo rate = 16 kHz */
		0xb0, 0x5e, 0x00, 0x01, /* Mix rate = 16 kHz */
		0xb0, 0x5a, 0x04, 0xa4, /* Set SBUS.Rx4 -> PRI (codec..325) */
		0xb0, 0x5a, 0x40, 0xaa, /* Set SBUS.Tx0 <- CSOUT (325..MDM) */
		0xb0, 0x17, 0x00, 0x02, /* Mic config = ... */
		0xb0, 0x18, 0x00, 0x07, /*   ... 1-MIC ASR (AM) */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT) */
		0xff			/* terminate */
	},
	 /* [11]: VOIP Headset(CT) */
	{
		0xb0, 0x5c, 0x00, 0x01, /* Algo = Voice processing */
		0xb0, 0x4c, 0x00, 0x01, /* Algo rate = 16 kHz */
		0xb0, 0x5e, 0x00, 0x01, /* Mix rate = 16 kHz */
		0xb0, 0x5a, 0x14, 0xa0, /* Set: SBUS.Rx0 -> FEIN */
		0xb0, 0x5a, 0x04, 0xa4, /* Set: SBUS.Rx4 -> PRI (9310 to 325) */
		0xb0, 0x5a, 0x40, 0xaa, /* Set: SBUS.Tx0 <- CSOUT (325 to MDM) */
		0xb0, 0x5a, 0x44, 0xac, /* Set: SBUS.Tx2 <- FEOUT1 (325 to 9310) */
		0xb0, 0x5a, 0x48, 0xad, /* Set: SBUS.Tx3 <- FEOUT2 (325 to 9310) */
		0xb0, 0x17, 0x00, 0x02, /* Mic config = ... */
		0xb0, 0x18, 0x00, 0x02, /*      ... 1-MIC CT */
		0x90, 0x1c, 0x00, 0x00, /* Algo processing = off (COMMIT) */
		0xff                    /* terminate */
	},
	/* [12]: Disable Port D clock */
	{
                0xb0, 0x0c, 0x0d,0x09, /* set PCM3 master, */
                0x80, 0x0d, 0x30,0x08, /* 8 kHz*/ 
		0xff				/* terminate */
	},
	/* [13]: 1MIC CT , PRI D MIC*/
	{
	/*
		0x80,0x17,0x10,0x04,
		0x80,0x18,0x00,0x04,
		0x80,0x5c,0x00,0x04,
		0xa0,0x5a,0x18,0xc0,
		0x80,0x5a,0x4c,0xaa,

		*/

	      0x90, 0x52,0x00, 0x00,
	      0x90, 0x5c, 0x00, 0x04,
	      0xB0, 0x5a, 0x18, 0xc0,
	      0x90, 0x5a, 0x4c, 0xaa,
	      
		/*0xb0, 0x52, 0x00, 0x00,*/  /* Disable PASS through */
		/*0xb0, 0x5c, 0x00, 0x01,*/ /* Algo = Voice processing */
             /* 0xb0, 0x5a, 0x14, 0xa0,*/ /* Set: SBUS.Rx0 -> FEIN */
		/*0xb0, 0x5a, 0x04, 0xc3,*/ /* Set SBUS.Rx4 -> PRI (codec..325) */
		/*0xb0, 0x5a, 0x08, 0xa4,*/ /* Set SBUS.Rx5 -> SEC (codec..325) */
		/*0xb0, 0x5a, 0x40, 0xAA, *//* Set PORTD.Left <- CSOUT (325..MDM) */
		/*0xb0, 0x5a, 0x44, 0xac,*/ /* Set SBUS.Tx2 <- FEOUT1 (325..codec) */
		  
		0xff				/* terminate */
	},
	/* [14]:  PRI D MIC and Slim Mic recording*/
	{
	      0x90, 0x52,0x00, 0x00, /* Disable PASS through */
	      0x90, 0x5c, 0x00, 0x04, /* Algo = Pass through */
	      0xB0, 0x5a, 0x18, 0xc0, /* Set data Path, PDMIO AUDIN1 */
	      0xB0, 0x5a, 0x1c, 0xa5,  /* Set data Path, Slimbus Mic, Rx5 AUDIN2 */
	      0xB0, 0x5a, 0x4C, 0xAA, /* Set data Path, Slimbus, Tx0 AUDOUT1 */
             0x90, 0x5a, 0x50, 0xab, /* Set data Path, Slimbus , Tx1 AUDOUT2 */
		  
		0xff				/* terminate */
	},
	/* [15]:  CT voice call with 1 PDM MIC */
	{
	      0x90, 0x52,0x00, 0x00, /* Disable PASS through */
	      0x90, 0x5c, 0x00, 0x01, /* Algo = Voice Processing */
	      0xB0, 0x5a, 0x04, 0xc0, /* Set data Path, PDMIO PRI */
	      0xB0, 0x5a, 0x14, 0xa0,  /* Set data Path, FEIN*/
	      0xB0, 0x5a, 0x40, 0xAA, /* Set data Path,CSOUT */
             0x90, 0x5a, 0x44, 0xac, /* Set data Path,FEOUT1*/
		  
		0xff				/* terminate */
	},
	/* [16]: One PRI D MIC recording*/
	{
	      0x90, 0x52,0x00, 0x00, /* Disable PASS through */
	      0x90, 0x5c, 0x00, 0x04, /* Algo = Pass through */
	      0xB0, 0x5a, 0x18, 0xc0, /* Set data Path, PDMIO AUDIN1 */
	      
	      0x90, 0x5a, 0x4C, 0xAA, /* Set data Path, Slimbus, Tx0 AUDOUT1 */
             
		  
		0xff				/* terminate */
	},
	/* [17]: Gain max */
	{

		  0x90, 0x15, 0x06,0x1E, /* AUDIN1 30 db gAIN , mAX*/ 
		  0x90, 0x15, 0x07,0x1E, /* AUDIN2 30 db gAIN , mAX*/ 
	 	  0x90, 0x15, 0x13,0x1E, /*  AUDOUT1 30 db gAIN , mAX*/ 
	 	  0x90, 0x15, 0x14,0x1E, /*  AUDOUT2 30 db gAIN , mAX*/ 
		0xff				/* terminate */
	},
	


};

static struct snd_soc_dai_driver es325_dai[];
static struct q_clkdiv *codec_clk;

static void es325_fixed_config(struct es325_priv *es325);
/* static void es325_ping(struct es325_priv *es325); */
static int es325_write_block_api(struct es325_priv *es325, u8 *cmd_blk);
static int es325_write_data_block_cmd(struct es325_priv *es325, u8 *cmd_blk);
static int es325_alloc_slim_rx_chan(struct slim_device *sbdev);
static int es325_alloc_slim_tx_chan(struct slim_device *sbdev);
static int es325_cfg_slim_rx(struct slim_device *sbdev, unsigned int *ch_num,
			     unsigned int ch_cnt, unsigned int rate);
static int es325_cfg_slim_tx(struct slim_device *sbdev, unsigned int *ch_num,
			     unsigned int ch_cnt, unsigned int rate);
static int es325_close_slim_rx(struct slim_device *sbdev, unsigned int *ch_num,
			       unsigned int ch_cnt);
static int es325_close_slim_tx(struct slim_device *sbdev, unsigned int *ch_num,
			       unsigned int ch_cnt);
static int es325_rx_ch_num_to_idx(int ch_num);
static int es325_tx_ch_num_to_idx(int ch_num);
static int es325_clk_ctl(int enable);

static void es325_sleep_request(struct es325_priv *es325);


unsigned int es325_presets = 0;
static int es325_rx_ch_num_to_idx(int ch_num)
{
	int i;
	int idx = -1;

	pr_info("GAC: %s(ch_num = %d)\n", __func__, ch_num);
	/* for (i = 0; i < ES325_SLIM_RX_PORTS; i++) { */
	for (i = 0; i < 6; i++) {
		if (ch_num == es325_slim_rx_port_to_ch[i]) {
			idx = i;
			break;
		}
	}

	return idx;
}

static int es325_tx_ch_num_to_idx(int ch_num)
{
	int i;
	int idx = -1;

	pr_info("GAC: %s(ch_num = %d)\n", __func__, ch_num);
	for (i = 0; i < ES325_SLIM_TX_PORTS; i++) {
		if (ch_num == es325_slim_tx_port_to_ch[i]) {
			idx = i;
			break;
		}
	}

	return idx;
}

/* es325 -> codec - alsa playback function */
static int es325_codec_cfg_slim_tx(struct es325_priv *es325, int dai_id)
{
	struct slim_device *sbdev = es325->gen0_client;
	int rc = 0;

	dev_info(&sbdev->dev, "GAC:%s(); entry\n", __func__);
	/* start slim channels associated with id */
	rc = es325_cfg_slim_tx(es325->gen0_client,
			       es325->dai[DAI_INDEX(dai_id)].ch_num,
			       es325->dai[DAI_INDEX(dai_id)].ch_tot,
			       es325->dai[DAI_INDEX(dai_id)].rate);

	return rc;
}

/* es325 <- codec - alsa capture function */
static int es325_codec_cfg_slim_rx(struct es325_priv *es325, int dai_id)
{
	struct slim_device *sbdev = es325->gen0_client;
	int rc = 0;

	dev_info(&sbdev->dev, "GAC:%s(); entry\n", __func__);
	/* start slim channels associated with id */
	rc = es325_cfg_slim_rx(es325->gen0_client,
			       es325->dai[DAI_INDEX(dai_id)].ch_num,
			       es325->dai[DAI_INDEX(dai_id)].ch_tot,
			       es325->dai[DAI_INDEX(dai_id)].rate);

	return rc;
}

/* es325 -> codec - alsa playback function */
static int es325_codec_close_slim_tx(struct es325_priv *es325, int dai_id)
{
	struct slim_device *sbdev = es325->gen0_client;
	int rc = 0;

	dev_info(&sbdev->dev, "GAC:%s(); entry\n", __func__);
	/* close slim channels associated with id */
	rc = es325_close_slim_tx(es325->gen0_client,
				 es325->dai[DAI_INDEX(dai_id)].ch_num,
				 es325->dai[DAI_INDEX(dai_id)].ch_tot);

	return rc;
}

/* es325 <- codec - alsa capture function */
static int es325_codec_close_slim_rx(struct es325_priv *es325, int dai_id)
{
	struct slim_device *sbdev = es325->gen0_client;
	int rc = 0;

	dev_info(&sbdev->dev, "GAC:%s(); entry\n", __func__);
	/* close slim channels associated with id */
	rc = es325_close_slim_rx(es325->gen0_client,
				 es325->dai[DAI_INDEX(dai_id)].ch_num,
				 es325->dai[DAI_INDEX(dai_id)].ch_tot);

	return rc;
}

static int es325_alloc_slim_rx_chan(struct slim_device *sbdev)
{
	struct es325_priv *es325_priv = slim_get_devicedata(sbdev);
	struct es325_slim_ch *rx = es325_priv->slim_rx;
	int i;
	int port_id;
	int rc = 0;

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);

	/* for (i = 0; i < ES325_SLIM_RX_PORTS; i++) { */
	for (i = 0; i < 6; i++) {
		port_id = i;
		rx[i].ch_num = es325_slim_rx_port_to_ch[i];
		slim_get_slaveport(sbdev->laddr, port_id, &rx[i].sph,
				   SLIM_SINK);
		slim_query_ch(sbdev, rx[i].ch_num, &rx[i].ch_h);
		dev_dbg(&sbdev->dev,
			"%s(): port_id = %d\n", __func__, port_id);
		dev_dbg(&sbdev->dev,
			"%s(): ch_num = %d\n", __func__, rx[i].ch_num);
		dev_dbg(&sbdev->dev,
			"%s(): sph = 0x%08x\n", __func__, rx[i].sph);
	}

	return rc;
}

static int es325_alloc_slim_tx_chan(struct slim_device *sbdev)
{
	struct es325_priv *es325_priv = slim_get_devicedata(sbdev);
	struct es325_slim_ch *tx = es325_priv->slim_tx;
	int i;
	int port_id;
	int rc = 0;

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);

	for (i = 0; i < ES325_SLIM_TX_PORTS; i++) {
		port_id = i + 10; /* ES325_SLIM_RX_PORTS; */
		tx[i].ch_num = es325_slim_tx_port_to_ch[i];
		slim_get_slaveport(sbdev->laddr, port_id, &tx[i].sph,
				   SLIM_SRC);
		slim_query_ch(sbdev, tx[i].ch_num, &tx[i].ch_h);
		dev_dbg(&sbdev->dev,
			"%s(): port_id = %d\n", __func__, port_id);
		dev_dbg(&sbdev->dev,
			"%s(): ch_num = %d\n", __func__, tx[i].ch_num);
		dev_dbg(&sbdev->dev,
			"%s(): sph = 0x%08x\n", __func__, tx[i].sph);
	}

	return rc;
}

static int es325_cfg_slim_rx(struct slim_device *sbdev, unsigned int *ch_num,
			     unsigned int ch_cnt, unsigned int rate)
{
	struct es325_priv *es325_priv = slim_get_devicedata(sbdev);
	struct es325_slim_ch *rx = es325_priv->slim_rx;
	u16 grph;
	u32 sph[ES325_SLIM_RX_PORTS] = {0};
	u16 ch_h[ES325_SLIM_RX_PORTS] = {0};
	struct slim_ch prop;
	int i;
	int idx = 0;
	int ret = 0;

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);

	for (i = 0; i < ch_cnt; i++) {
		idx = es325_rx_ch_num_to_idx(ch_num[i]);
		ch_h[i] = rx[idx].ch_h;
		sph[i] = rx[idx].sph;

		pr_info("GAC:%s(): idx = %d\n", __func__, idx);
		dev_dbg(&sbdev->dev, "%s(): ch_num[i] = %d\n", __func__, ch_num[i]);
		dev_dbg(&sbdev->dev, "%s(): ch_h[i] = %d\n", __func__, ch_h[i]);
		dev_dbg(&sbdev->dev, "%s(): sph[i] = 0x%08x\n", __func__, sph[i]);
	}

	prop.prot = SLIM_AUTO_ISO;
	prop.baser = SLIM_RATE_4000HZ;
	prop.dataf = SLIM_CH_DATAF_NOT_DEFINED;
	prop.auxf = SLIM_CH_AUXF_NOT_APPLICABLE;
	prop.ratem = (rate/4000);
	prop.sampleszbits = 16;

	ret = slim_define_ch(sbdev, &prop, ch_h, ch_cnt, true, &grph);
	if (ret < 0) {
		dev_err(&sbdev->dev, "%s(): slim_define_ch() failed: %d\n",
			__func__, ret);
		goto slim_define_ch_error;
	}
	for (i = 0; i < ch_cnt; i++) {
		ret = slim_connect_sink(sbdev, &sph[i], 1, ch_h[i]);
		if (ret < 0) {
			dev_err(&sbdev->dev,
				"%s(): slim_connect_sink() failed: %d\n",
				__func__, ret);
			goto slim_connect_sink_error;
		}
	}
	ret = slim_control_ch(sbdev, grph, SLIM_CH_ACTIVATE, true);
	if (ret < 0) {
		dev_err(&sbdev->dev,
			"%s(): slim_control_ch() failed: %d\n",
			__func__, ret);
		goto slim_control_ch_error;
	}
	for (i = 0; i < ch_cnt; i++) {
		idx = es325_rx_ch_num_to_idx(ch_num[i]);
		pr_info("GAC:%s(): idx = %d\n", __func__, idx);
		rx[idx].grph = grph;
	}
	return 0;
slim_control_ch_error:
slim_connect_sink_error:
	es325_close_slim_rx(sbdev, ch_num, ch_cnt);
slim_define_ch_error:
	return ret;
}

static int es325_cfg_slim_tx(struct slim_device *sbdev, unsigned int *ch_num,
			     unsigned int ch_cnt, unsigned int rate)
{
	struct es325_priv *es325_priv = slim_get_devicedata(sbdev);
	struct es325_slim_ch *tx = es325_priv->slim_tx;
	u16 grph;
	u32 sph[ES325_SLIM_TX_PORTS] = {0};
	u16 ch_h[ES325_SLIM_TX_PORTS] = {0};
	struct slim_ch prop;
	int i;
	int idx = 0;
	int ret = 0;

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);
	dev_dbg(&sbdev->dev, "%s(): ch_cnt = %d\n", __func__, ch_cnt);
	dev_dbg(&sbdev->dev, "%s(): rate = %d\n", __func__, rate);

	for (i = 0; i < ch_cnt; i++) {
		idx = es325_tx_ch_num_to_idx(ch_num[i]);
		ch_h[i] = tx[idx].ch_h;
		sph[i] = tx[idx].sph;
		pr_info("GAC:%s(): idx = %d\n", __func__, idx);
		dev_dbg(&sbdev->dev, "%s(): ch_num[i] = %d\n", __func__, ch_num[i]);
		dev_dbg(&sbdev->dev, "%s(): ch_h[i] = %d\n", __func__, ch_h[i]);
		dev_dbg(&sbdev->dev, "%s(): sph[i] = 0x%08x\n", __func__, sph[i]);
	}

	prop.prot = SLIM_AUTO_ISO;
	prop.baser = SLIM_RATE_4000HZ;
	prop.dataf = SLIM_CH_DATAF_NOT_DEFINED;
	prop.auxf = SLIM_CH_AUXF_NOT_APPLICABLE;
	prop.ratem = (rate/4000);
	prop.sampleszbits = 16;

	ret = slim_define_ch(sbdev, &prop, ch_h, ch_cnt, true, &grph);
	if (ret < 0) {
		dev_err(&sbdev->dev, "%s(): slim_define_ch() failed: %d\n",
			__func__, ret);
		goto slim_define_ch_error;
	}
	for (i = 0; i < ch_cnt; i++) {
		ret = slim_connect_src(sbdev, sph[i], ch_h[i]);
		if (ret < 0) {
			dev_err(&sbdev->dev,
				"%s(): slim_connect_src() failed: %d\n",
				__func__, ret);
			dev_err(&sbdev->dev,
				"%s(): ch_num[0] = %d\n",
				__func__, ch_num[0]);
			goto slim_connect_src_error;
		}
	}
	ret = slim_control_ch(sbdev, grph, SLIM_CH_ACTIVATE, true);
	if (ret < 0) {
		dev_err(&sbdev->dev,
			"%s(): slim_control_ch() failed: %d\n",
			__func__, ret);
		goto slim_control_ch_error;
	}
	for (i = 0; i < ch_cnt; i++) {
		idx = es325_tx_ch_num_to_idx(ch_num[i]);
		pr_info("GAC:%s(): idx = %d\n", __func__, idx);
		tx[idx].grph = grph;
	}
	return 0;
slim_control_ch_error:
slim_connect_src_error:
	es325_close_slim_tx(sbdev, ch_num, ch_cnt);
slim_define_ch_error:
	return ret;
}

static int es325_close_slim_rx(struct slim_device *sbdev, unsigned int *ch_num,
			       unsigned int ch_cnt)
{
	struct es325_priv *es325_priv = slim_get_devicedata(sbdev);
	struct es325_slim_ch *rx = es325_priv->slim_rx;
	u16 grph = 0;
	u32 sph[ES325_SLIM_RX_PORTS] = {0};
	int i;
	int idx = 0;
	int ret = 0;

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);

	for (i = 0; i < ch_cnt; i++) {
		idx = es325_rx_ch_num_to_idx(ch_num[i]);
		pr_info("GAC:%s(): idx = %d\n", __func__, idx);
		sph[i] = rx[idx].sph;
		grph = rx[idx].grph;
	}

	ret = slim_control_ch(sbdev, grph, SLIM_CH_REMOVE, true);
	if (ret < 0) {
		dev_err(&sbdev->dev,
			"%s(): slim_control_ch() failed: %d\n",
			__func__, ret);
		goto slim_control_ch_error;
	}
	for (i = 0; i < ch_cnt; i++) {
		idx = es325_rx_ch_num_to_idx(ch_num[i]);
		pr_info("GAC:%s(): idx = %d\n", __func__, idx);
		rx[idx].grph = 0;
	}
	ret = slim_disconnect_ports(sbdev, sph, ch_cnt);
	if (ret < 0) {
		dev_err(&sbdev->dev,
			"%s(): slim_disconnect_ports() failed: %d\n",
			__func__, ret);
	}
slim_control_ch_error:
	return ret;
}

static int es325_close_slim_tx(struct slim_device *sbdev, unsigned int *ch_num,
			       unsigned int ch_cnt)
{
	struct es325_priv *es325_priv = slim_get_devicedata(sbdev);
	struct es325_slim_ch *tx = es325_priv->slim_tx;
	u16 grph = 0;
	u32 sph[ES325_SLIM_TX_PORTS] = {0};
	int i;
	int idx = 0;
	int ret = 0;

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);

	for (i = 0; i < ch_cnt; i++) {
		idx = es325_tx_ch_num_to_idx(ch_num[i]);
		pr_info("GAC:%s(): idx = %d\n", __func__, idx);
		sph[i] = tx[idx].sph;
		grph = tx[idx].grph;
	}

	ret = slim_control_ch(sbdev, grph, SLIM_CH_REMOVE, true);
	if (ret < 0) {
		dev_err(&sbdev->dev,
			"%s(): slim_connect_sink() failed: %d\n",
			__func__, ret);
		goto slim_control_ch_error;
	}
	for (i = 0; i < ch_cnt; i++) {
		idx = es325_tx_ch_num_to_idx(ch_num[i]);
		pr_info("GAC:%s(): idx = %d\n", __func__, idx);
		tx[idx].grph = 0;
	}
	ret = slim_disconnect_ports(sbdev, sph, ch_cnt);
	if (ret < 0) {
		dev_err(&sbdev->dev,
			"%s(): slim_disconnect_ports() failed: %d\n",
			__func__, ret);
	}
slim_control_ch_error:
	return ret;
}

int es325_remote_cfg_slim_rx(int dai_id)
{
	struct es325_priv *es325 = &es325_priv;
	struct slim_device *sbdev = es325->gen0_client;
	int be_id;
	int rc = 0;

	dev_info(&sbdev->dev, "GAC:%s(dai_id = %d): entry\n", __func__, dai_id);

	dev_info(&sbdev->dev, "GAC:%s(): ch_tot = %d\n", __func__,
		 es325->dai[DAI_INDEX(dai_id)].ch_tot);
	if (!es325_fw_ready){
		pr_err("%s(): ES325 Firmware is not ready\n", __func__);
		return rc;
	}
	
	if (dai_id != ES325_SLIM_1_PB
	    && dai_id != ES325_SLIM_2_PB)
		return 0;

	if (es325->dai[DAI_INDEX(dai_id)].ch_tot != 0) {
		pr_info("%s: MDM ch_0=%d ch_1=%d\n", __func__, es325->dai[DAI_INDEX(dai_id)].ch_num[0], es325->dai[DAI_INDEX(dai_id)].ch_num[1]);
		/* start slim channels associated with id */
		rc = es325_cfg_slim_rx(es325->gen0_client,
				       es325->dai[DAI_INDEX(dai_id)].ch_num,
				       es325->dai[DAI_INDEX(dai_id)].ch_tot,
				       es325->dai[DAI_INDEX(dai_id)].rate);

		be_id = es325_slim_be_id[DAI_INDEX(dai_id)];
		es325->dai[DAI_INDEX(be_id)].ch_tot = es325->dai[DAI_INDEX(dai_id)].ch_tot;
		es325->dai[DAI_INDEX(be_id)].rate = es325->dai[DAI_INDEX(dai_id)].rate;
		pr_info("%s: WCD ch_0=%d ch_1=%d\n", __func__, es325->dai[DAI_INDEX(be_id)].ch_num[0], es325->dai[DAI_INDEX(be_id)].ch_num[1]);
		rc = es325_codec_cfg_slim_tx(es325, be_id);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(es325_remote_cfg_slim_rx);

int es325_remote_cfg_slim_tx(int dai_id)
{
	struct es325_priv *es325 = &es325_priv;
	struct slim_device *sbdev = es325->gen0_client;
	int be_id;
	int ch_cnt;
	int rc = 0;

	dev_info(&sbdev->dev, "GAC:%s(); entry\n", __func__);

	dev_info(&sbdev->dev, "GAC:%s(): ch_tot = %d\n", __func__,
		 es325->dai[DAI_INDEX(dai_id)].ch_tot);

if (!es325_fw_ready){
	      pr_err("%s(): ES325 Firmware is not ready\n", __func__);
		return rc;
}

    if (dai_id != ES325_SLIM_1_CAP)
		return 0;

    //Phone call and recording conflict fix
	if ((es325_tx1_route_enable == 1) && (es325->dai[DAI_INDEX(dai_id)].ch_tot == 0) ) 
	{
		 es325->dai[DAI_INDEX(dai_id)].ch_tot = 2;  
	pr_info("GAC:%s(): phone call recording fix\n", __func__ );
	}

	if (es325->dai[DAI_INDEX(dai_id)].ch_tot != 0) {
		pr_info("%s: MDM ch_0=%d ch_1=%d\n", __func__, es325->dai[DAI_INDEX(dai_id)].ch_num[0], es325->dai[DAI_INDEX(dai_id)].ch_num[1]);
		/* start slim channels associated with id */
		if (dai_id == ES325_SLIM_1_CAP) {
			pr_info("GAC:%s(): overwrite ch_cnt = %d\n",
				__func__, es325_ap_tx1_ch_cnt);
			ch_cnt = es325_ap_tx1_ch_cnt;
		}
		rc = es325_cfg_slim_tx(es325->gen0_client,
				       es325->dai[DAI_INDEX(dai_id)].ch_num,
				       ch_cnt,
				       es325->dai[DAI_INDEX(dai_id)].rate);

		be_id = es325_slim_be_id[DAI_INDEX(dai_id)];
		es325->dai[DAI_INDEX(be_id)].ch_tot = es325->dai[DAI_INDEX(dai_id)].ch_tot;
		es325->dai[DAI_INDEX(be_id)].rate = es325->dai[DAI_INDEX(dai_id)].rate;
		pr_info("%s: WCD ch_0=%d ch_1=%d\n", __func__, es325->dai[DAI_INDEX(be_id)].ch_num[0], es325->dai[DAI_INDEX(be_id)].ch_num[1]);
		rc = es325_codec_cfg_slim_rx(es325, be_id);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(es325_remote_cfg_slim_tx);

int es325_remote_close_slim_rx(int dai_id)
{
	struct es325_priv *es325 = &es325_priv;
	struct slim_device *sbdev = es325->gen0_client;
	int be_id;
	int rc = 0;

	if (!es325_fw_ready){
		pr_err("%s(): ES325 Firmware is not ready\n", __func__);
		return rc;
	}
	dev_info(&sbdev->dev, "GAC:%s(); entry\n", __func__);

	dev_info(&sbdev->dev, "GAC:%s(): ch_tot = %d\n", __func__,
		 es325->dai[DAI_INDEX(dai_id)].ch_tot);
	if (dai_id != ES325_SLIM_1_PB
	    && dai_id != ES325_SLIM_2_PB)
		return 0;

	if (es325->dai[DAI_INDEX(dai_id)].ch_tot != 0) {
		es325_close_slim_rx(es325->gen0_client,
				    es325->dai[DAI_INDEX(dai_id)].ch_num,
				    es325->dai[DAI_INDEX(dai_id)].ch_tot);

		be_id = es325_slim_be_id[DAI_INDEX(dai_id)];
		rc = es325_codec_close_slim_tx(es325, be_id);

		es325->dai[DAI_INDEX(dai_id)].ch_tot = 0;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(es325_remote_close_slim_rx);

int es325_remote_close_slim_tx(int dai_id)
{
	struct es325_priv *es325 = &es325_priv;
	struct slim_device *sbdev = es325->gen0_client;
	int be_id;
	int ch_cnt;
	int rc = 0;
	if (!es325_fw_ready){
		pr_err("%s(): ES325 Firmware is not ready\n", __func__);
		return rc;
	}
	dev_info(&sbdev->dev, "GAC:%s(); entry\n", __func__);

	dev_info(&sbdev->dev, "GAC:%s(): ch_tot = %d\n", __func__,
		 es325->dai[DAI_INDEX(dai_id)].ch_tot);
	if (dai_id != ES325_SLIM_1_CAP)
		return 0;

	if (es325->dai[DAI_INDEX(dai_id)].ch_tot != 0) {
		if (dai_id == ES325_SLIM_1_CAP)
			ch_cnt = es325_ap_tx1_ch_cnt;
		es325_close_slim_tx(es325->gen0_client,
				    es325->dai[DAI_INDEX(dai_id)].ch_num,
				    ch_cnt);

		be_id = es325_slim_be_id[DAI_INDEX(dai_id)];
		rc = es325_codec_close_slim_rx(es325, be_id);

		es325->dai[DAI_INDEX(dai_id)].ch_tot = 0;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(es325_remote_close_slim_tx);

static int es325_init_slim_slave(struct slim_device *sbdev)
{
	int rc = 0;

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);

	rc = es325_alloc_slim_rx_chan(sbdev);
	rc = es325_alloc_slim_tx_chan(sbdev);

	return rc;
}

static void msg_to_bus_order(char *msg, int msg_len)
{
	char tmp;

	for (; msg_len > 0; msg_len -= 4, msg += 4) {
		tmp = *(msg + 3);
		*(msg + 3) = *(msg);
		*(msg) = tmp;
		tmp = *(msg + 2);
		*(msg + 2) = *(msg + 1);
		*(msg + 1) = tmp;
	}
}

/* Maximum number of attempts to read a VE in the presence of a "not
 * ready" response. */
#define ES325_RD_POLL_MAX 5
/* Interval between attemps to read a VE in milliseconds. */
#define ES325_RD_POLL_INTV 30

#ifdef BUS_TRANSACTIONS
#if defined(CONFIG_SND_SOC_ES325_I2C)
static int es325_i2c_read(struct es325_priv *es325, char *buf, int len);
static int es325_i2c_write(struct es325_priv *es325, char *buf, int len);
#define ES325_BUS_READ(x_es325, x_offset, x_width, x_buf, x_len, x_bus_order) \
	es325_i2c_read(x_es325, x_buf, x_len)
#define ES325_BUS_WRITE(x_es325, x_offset, x_width, x_buf, x_len, x_bus_order) \
	es325_i2c_write(x_es325, x_buf, x_len)
#elif defined(CONFIG_SND_SOC_ES325_SLIM)
static int es325_slim_read(struct es325_priv *es325, unsigned int offset,
			   unsigned int width, char *buf, int len, int bus_order);
static int es325_slim_write(struct es325_priv *es325, unsigned int offset,
			    unsigned int width, char *buf, int len, int bus_order);
#define ES325_BUS_READ(x_es325, x_offset, x_width, x_buf, x_len, x_bus_order) \
	es325_slim_read(x_es325, x_offset, x_width, x_buf, x_len, x_bus_order)
#define ES325_BUS_WRITE(x_es325, x_offset, x_width, x_buf, x_len, x_bus_order) \
	es325_slim_write(x_es325, x_offset, x_width, x_buf, x_len, x_bus_order)
#else
#error "es325.c - bus infrastructure not defined"
#endif
#else
/* Pretend all read and write operations on the bus are successful -
 * when no bus is available. */
#define ES325_BUS_READ(e, o, w, b, l, r) (0)
#define ES325_BUS_WRITE(e, o, w, b, l, r) (0)
#endif

static int es325_write_block_api(struct es325_priv *es325, u8 *cmd_blk)
{
	int rc = 0;

	if (!es325_fw_ready){
		pr_err("%s(): ES325 Firmware is not ready\n", __func__);
		return rc;
	}

	while (*cmd_blk != 0xff) {
		u8 msg[4];
		memcpy(msg, cmd_blk, 4);
		es325_slim_write(es325, ES325_WRITE_VE_OFFSET,
				 ES325_WRITE_VE_WIDTH, msg, 4, 1);
		pr_info("%s(): msg = %02x%02x%02x%02x\n", __func__,
			msg[0], msg[1], msg[2], msg[3]);
		cmd_blk += 4;
		msleep(20);
	}

	return rc;
}

static int es325_write_block_len(struct es325_priv *es325, u8 *cmd_blk,
				 int len)
{
	int rc = 0;

	if (!es325_fw_ready){
		pr_err("%s(): ES325 Firmware is not ready\n", __func__);
		return rc;
	}

	pr_info("GAC:%s(len = %d); entry\n", __func__, len);
	while (len > 0) {
		u8 msg[4];
		memcpy(msg, cmd_blk, 4);
		es325_slim_write(es325, ES325_WRITE_VE_OFFSET,
				 ES325_WRITE_VE_WIDTH, msg, 4, 1);
		pr_info("%s(): msg = %02x%02x%02x%02x\n", __func__,
			msg[0], msg[1], msg[2], msg[3]);
		cmd_blk += 4;
		len -= 4;
		usleep_range(5000, 5000);
	}

	return rc;
}

static int es325_write_data_block_cmd(struct es325_priv *es325, u8 *cmd_blk)
{
	int rc = 0;
	u16 len;
	u8 msg[4];
	u8 ack_msg[4];

	pr_info("GAC:%s(); entry\n", __func__);
	if (*cmd_blk == 0xff) {
		pr_err("%s(): empty block\n", __func__);
		return -EINVAL;
	}

	memcpy(msg, cmd_blk, 4);
	memcpy((char *)&len, cmd_blk + 2, 2);
	len = be16_to_cpu(len);
	pr_info("GAC:%s(); len = 0x%04x\n", __func__, len);
	es325_slim_write(es325, ES325_WRITE_VE_OFFSET,
			 ES325_WRITE_VE_WIDTH, msg, 4, 1);
	usleep_range(10000, 10000);
	es325_slim_read(es325, ES325_READ_VE_OFFSET,
			ES325_READ_VE_WIDTH, ack_msg, 4, 1);
	if (memcmp(ack_msg, cmd_blk, 4) != 0) {
		pr_err("%s(): ack failed = 0x%02x%02x%02x%02x\n",
		       __func__, ack_msg[0], ack_msg[1], ack_msg[2],
		       ack_msg[3]);
		return -EIO;
	}
	cmd_blk += 4;

	es325_write_block_len(es325, cmd_blk, len);

	usleep_range(10000, 10000);

	es325_slim_read(es325, ES325_READ_VE_OFFSET,
			ES325_READ_VE_WIDTH, ack_msg, 4, 1);
	pr_info("%s(): ack = 0x%02x%02x%02x%02x\n",
		__func__, ack_msg[0], ack_msg[1], ack_msg[2],
		ack_msg[3]);
	switch (ack_msg[3]) {
	case 0x00:
		rc = 0;
		break;
	case 0x02:
		rc = -EINVAL;
		pr_err("%s(): write data block - destination error\n",
		       __func__);
		break;
	case 0x03:
		rc = -EINVAL;
		pr_err("%s(): write data block - version error\n",
		       __func__);
		break;
	case 0x04:
		rc = -EINVAL;
		pr_err("%s(): write data block - type error\n",
		       __func__);
		break;
	case 0x06:
		rc = -EINVAL;
		pr_err("%s(): write data block - length error\n",
		       __func__);
		break;
	case 0x0e:
		rc = -EINVAL;
		pr_err("%s(): write data block - checksum error\n",
		       __func__);
		break;
	default:
		rc = -EINVAL;
		pr_err("%s(): write data block - invalid ack\n",
		       __func__);
		break;
	}

	return rc;
}

#ifdef FIXED_CONFIG
static void es325_fixed_config(struct es325_priv *es325)
{
	int rc;

	rc = es325_write_block_api(es325, &es325_internal_route_configs[6][0]);
}
#endif

/*
static void es325_ping(struct es325_priv *es325)
{
	u8 req_msg[] = {
		0x80, 0x4f, 0x00, 0x00,
	};
	u8 ack_msg[4] = {
		0x00, 0x00, 0x00, 0x00,
	};

	es325_slim_write(es325, ES325_WRITE_VE_OFFSET,
			 ES325_WRITE_VE_WIDTH, req_msg, 4, 1);
	msleep(20);
	es325_slim_read(es325, ES325_READ_VE_OFFSET,
			ES325_READ_VE_WIDTH, ack_msg, 4, 1);
	pr_info("GAC:%s(): ping ack = %02x%02x%02x%02x\n", __func__,
		ack_msg[0], ack_msg[1], ack_msg[2], ack_msg[3]);
}
*/

#if defined(CONFIG_SND_SOC_ES325_I2C)
static int es325_i2c_read(struct es325_priv *es325, char *buf, int len)
{
	struct i2c_msg msg[] = {
		{
			.addr = es325->this_client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		},
	};
	int rc = 0;

	pr_info("%s(): entry\n", __func__);

	/*
	rc = i2c_transfer(es325->this_client->adapter, msg, 1);
	if (rc < 0) {
		pr_err("%s(): i2c_transfer() failed, rc = %d", __func__, rc);
		return rc;
	}

	{
		int i;
		pr_info("%s(): i2c msg:\n", __func__);
		for (i = 0; i < len; ++i)
			pr_info("\t[%d] = 0x%02x\n", i, buf[i]);
	}

	pr_info("%s(): exit\n", __func__);
	*/
	return rc;
}

static int es325_i2c_write(struct es325_priv *es325, char *buf, int len)
{
	struct i2c_msg msg[] = {
		{
			.addr = es325->this_client->addr,
			.flags = 0,
			.len = len,
			.buf = buf,
		},
	};
	int rc = 0;

	/*
	pr_info("%s(): entry\n", __func__);

	{
		int i;

		pr_info("%s(): i2c msg:\n", __func__);
		for (i = 0; i < len; ++i)
			pr_info("\t[%d] = 0x%02x\n", i, buf[i]);
	}

	rc = i2c_transfer(es325->this_client->adapter, msg, 1);
	if (rc < 0) {
		pr_err("%s(): i2c_transfer() failed, rc = %d",
		__func__, rc);
		return rc;
	}

	pr_info("%s(): exit\n", __func__);
	*/
	return rc;
}
#else
static int es325_slim_read(struct es325_priv *es325, unsigned int offset,
			   unsigned int width, char *buf, int len,
			   int bus_order)
{
	char notready[] = { 0, 0, 0, 0 };
	struct slim_device *sbdev = es325->gen0_client;
	DECLARE_COMPLETION_ONSTACK(read_done);
	struct slim_ele_access msg = {
		.start_offset = offset,
		.num_bytes = width,
		/* .comp = &read_done, */
		.comp = NULL,
	};
	int rc = 0;
	int try = 0;

	for (try = 0; try < ES325_RD_POLL_MAX; try++) {
		rc = slim_request_val_element(sbdev, &msg, buf, len);
		if (rc != 0)
			break;
		if (memcmp(buf, notready, 4) != 0)
			break;
		msleep(ES325_RD_POLL_INTV);
	}
	if (try >= ES325_RD_POLL_MAX && memcmp(buf, notready, 4) == 0) {
		pr_err("%s: failed not ready after %d tries\n", __func__, try);
		rc = -EIO;
	}

	if (bus_order)
		msg_to_bus_order(buf, len);
	if (rc != 0)
		pr_err("%s: read failed rc=%d\n", __func__, rc);

	return rc;
}

static int es325_slim_write(struct es325_priv *es325, unsigned int offset,
			    unsigned int width, char *buf, int len, int bus_order)
{
	struct slim_device *sbdev = es325->gen0_client;
	struct slim_ele_access msg = {
		.start_offset = offset,
		.num_bytes = width,
		.comp = NULL,
	};
	int rc = 0;
	
	if (bus_order)
		msg_to_bus_order(buf, len);
	rc = slim_change_val_element(sbdev, &msg, buf, len);

	return rc;
}
#endif

static int es325_build_algo_read_msg(char *msg, int *msg_len,
				     unsigned int reg)
{
	unsigned int index = reg & ES325_ADDR_MASK;
	unsigned int paramid;

	if (index > ARRAY_SIZE(es325_algo_paramid))
		return -EINVAL;

	paramid = es325_algo_paramid[index];

	/* ES325_GET_ALGO_PARAM */
	*msg++ = (ES325_GET_ALGO_PARAM >> 8) & 0x00ff;
	*msg++ = ES325_GET_ALGO_PARAM & 0x00ff;

	/* PARAM ID */
	*msg++ = (paramid >> 8) & 0x00ff;
	*msg++ = paramid & 0x00ff;
	*msg_len = 4;

	return 0;
}

static int es325_build_algo_write_msg(char *msg, int *msg_len,
				      unsigned int reg, unsigned int value)
{
	unsigned int index = reg & ES325_ADDR_MASK;
	unsigned int cmd;
	unsigned int paramid;

	if (index > ARRAY_SIZE(es325_algo_paramid))
		return -EINVAL;

	paramid = es325_algo_paramid[index];

	/* ES325_SET_ALGO_PARAMID */
	cmd = ES325_SET_ALGO_PARAMID;
	if (reg & ES325_STAGED_CMD)
		cmd |= ES325_STAGED_MSG_BIT;
	*msg++ = (cmd >> 8) & 0x00ff;
	*msg++ = cmd & 0x00ff;

	/* PARAM ID */
	*msg++ = (paramid >> 8) & 0x00ff;
	*msg++ = paramid & 0x00ff;

	/* ES325_SET_ALGO_PARAM */
	cmd = ES325_SET_ALGO_PARAM;
	if (reg & ES325_STAGED_CMD)
		cmd |= ES325_STAGED_MSG_BIT;
	*msg++ = (cmd >> 8) & 0x00ff;
	*msg++ = cmd & 0x00ff;

	/* value */
	*msg++ = (value >> 8) & 0x00ff;
	*msg++ = value & 0x00ff;
	*msg_len = 8;

	return 0;
}

static int es325_build_dev_read_msg(char *msg, int *msg_len,
				    unsigned int reg)
{
	unsigned int index = reg & ES325_ADDR_MASK;
	unsigned int paramid;

	if (index > ARRAY_SIZE(es325_dev_paramid))
		return -EINVAL;

	paramid = es325_dev_paramid[index];

	/* ES325_GET_DEV_PARAM */
	*msg++ = (ES325_GET_DEV_PARAM >> 8) & 0x00ff;
	*msg++ = ES325_GET_DEV_PARAM & 0x00ff;

	/* PARAM ID */
	*msg++ = (paramid >> 8) & 0x00ff;
	*msg++ = paramid & 0x00ff;
	*msg_len = 4;

	return 0;
}

static int es325_build_dev_write_msg(char *msg, int *msg_len,
				     unsigned int reg, unsigned int value)
{
	unsigned int index = reg & ES325_ADDR_MASK;
	unsigned int cmd;
	unsigned int paramid;

	if (index > ARRAY_SIZE(es325_dev_paramid))
		return -EINVAL;

	paramid = es325_dev_paramid[index];

	/* ES325_SET_DEV_PARAMID */
	cmd = ES325_SET_DEV_PARAMID;
	if (reg & ES325_STAGED_CMD)
		cmd |= ES325_STAGED_MSG_BIT;
	*msg++ = (cmd >> 8) & 0x00ff;
	*msg++ = cmd & 0x00ff;

	/* PARAM ID */
	*msg++ = (paramid >> 8) & 0x00ff;
	*msg++ = paramid & 0x00ff;

	/* ES325_SET_DEV_PARAM */
	cmd = ES325_SET_DEV_PARAM;
	if (reg & ES325_STAGED_CMD)
		cmd |= ES325_STAGED_MSG_BIT;
	*msg++ = (cmd >> 8) & 0x00ff;
	*msg++ = cmd & 0x00ff;

	/* value */
	*msg++ = (value >> 8) & 0x00ff;
	*msg++ = value & 0x00ff;
	*msg_len = 8;

	return 0;
}

static int es325_build_cmd_read_msg(char *msg, int *msg_len,
				    unsigned int reg)
{
	unsigned int index = reg & ES325_ADDR_MASK;
	struct es325_cmd_access *cmd_access;

	if (index > ARRAY_SIZE(es325_cmd_access))
		return -EINVAL;
	cmd_access = es325_cmd_access + index;

	*msg_len = cmd_access->read_msg_len;
	memcpy(msg, &cmd_access->read_msg, *msg_len);

	return 0;
}

static int es325_build_cmd_write_msg(char *msg, int *msg_len,
				     unsigned int reg, unsigned int value)
{
	unsigned int index = reg & ES325_ADDR_MASK;
	struct es325_cmd_access *cmd_access;

	if (index > ARRAY_SIZE(es325_cmd_access))
		return -EINVAL;
	cmd_access = es325_cmd_access + index;

	*msg_len = cmd_access->write_msg_len;
	memcpy(msg, &cmd_access->write_msg, *msg_len);
	if (reg & ES325_STAGED_CMD)
		*msg |= (1 << 5);

	return 0;
}

static int es325_build_preset_write_msg(char *msg, int *msg_len,
				     unsigned int reg, unsigned int value)
{
	unsigned int index = reg & ES325_ADDR_MASK;
	struct es325_cmd_access *cmd_access;

	if (index > ARRAY_SIZE(es325_cmd_access))
		return -EINVAL;
	cmd_access = es325_cmd_access + index;

	*msg_len = cmd_access->write_msg_len;
	memcpy(msg, &cmd_access->write_msg, *msg_len);
    	if(value > 0) 
	{
        	msg[*msg_len - 1] = value & 0x00ff;
        	msg[*msg_len - 2] = (value >> 8) & 0x00ff;
	}
	if (reg & ES325_STAGED_CMD)
		*msg |= (1 << 5);

    *msg |= (1 << 4);
	return 0;
}
static unsigned int es325_read(struct snd_soc_codec *codec,
			       unsigned int reg)
{
	struct es325_priv *es325 = &es325_priv;
	unsigned int access = reg & ES325_ACCESS_MASK;
	char req_msg[16];
	char ack_msg[16];
	unsigned int msg_len;
	unsigned int value;
	int rc;

	if (!es325_fw_ready) {
		pr_err("%s(): Firmware not ready", __func__);
		return -EINVAL;
	}

	switch (access) {
	case ES325_ALGO_ACCESS:
		rc = es325_build_algo_read_msg(req_msg, &msg_len, reg);
		break;
	case ES325_DEV_ACCESS:
		rc = es325_build_dev_read_msg(req_msg, &msg_len, reg);
		break;
	case ES325_CMD_ACCESS:
		rc = es325_build_cmd_read_msg(req_msg, &msg_len, reg);
		break;
	case ES325_OTHER_ACCESS:
		return 0;
	default:
		rc = -EINVAL;
		break;
	}
	if (rc) {
		pr_err("%s(): failed to build read message for address = 0x%04x\n",
			__func__, reg);
		return rc;
	}

	rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
			     ES325_WRITE_VE_WIDTH, req_msg, msg_len, 1);
	if (rc < 0) {
		pr_err("%s(): es325_xxxx_write() Failed \n", __func__);
		return rc;
	}
	msleep(20);
	rc = ES325_BUS_READ(es325, ES325_READ_VE_OFFSET,
			    ES325_READ_VE_WIDTH, ack_msg, 4, 1);
	if (rc < 0) {
		pr_err("%s(): es325_xxxx_read() Failed \n", __func__);
		return rc;
	}
	value = ack_msg[2] << 8 | ack_msg[3];

	return value;
}

static int es325_write(struct snd_soc_codec *codec, unsigned int reg,
		       unsigned int value)
{
	struct es325_priv *es325 = &es325_priv;
	unsigned int access = reg & ES325_ACCESS_MASK;
	char msg[16];
	char *msg_ptr;
	int msg_len;
	int i;
	int rc;

	if (!es325_fw_ready) {
		pr_err("%s(): Firmware not ready", __func__);
		return -EINVAL;
	}

	switch (access) {
	case ES325_ALGO_ACCESS:
		rc = es325_build_algo_write_msg(msg, &msg_len, reg, value);
		break;
	case ES325_DEV_ACCESS:
		rc = es325_build_dev_write_msg(msg, &msg_len, reg, value);
		break;
	case ES325_CMD_ACCESS:
		rc = es325_build_cmd_write_msg(msg, &msg_len, reg, value);
		break;
	case ES325_OTHER_ACCESS:
		return 0;
	default:
		rc = -EINVAL;
		break;
	}
	if (rc) {
		pr_err("%s(): failed to build write message for address = 0x%04x\n",
			__func__, reg);
		return rc;
	}

	msg_ptr = msg;
	for (i = msg_len; i > 0; i -= 4) {
		rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
				     ES325_WRITE_VE_WIDTH, msg_ptr, 4, 1);
		if (rc < 0) {
			pr_err("%s(): es325_xxxx_write()  Failed \n", __func__);
			return rc;
		}
		msleep(20);
		rc = ES325_BUS_READ(es325, ES325_READ_VE_OFFSET,
				    ES325_READ_VE_WIDTH, msg_ptr, 4, 1);
		if (rc < 0) {
			pr_err("%s(): es325_xxxx_read()  Failed \n", __func__);
			return rc;
		}
		msg_ptr += 4;
	}

	return rc;
}

#define VERIDIAN 1
static ssize_t es325_route_status_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	int i = 0;
	int name_id = 0;
	ssize_t ret = 0;
	struct es325_priv *es325 = &es325_priv;
	char *status_name[] = {
		"Route Status",
#if VERIDIAN
		"Rx ChnMgr[16] Run State",
		"Rx ChnMgr[17] Run State",
		"Rx ChnMgr[18] Run State",
		"Rx ChnMgr[19] Run State",
		"Rx ChnMgr[20] Run State",
		"Rx ChnMgr[21] Run State",
		"Rx ChnMgr[22] Run State",
		"Rx ChnMgr[23] Run State",
		"Rx ChnMgr[24] Run State",
		"Rx ChnMgr[25] Run State",
		"Tx ChnMgr[26] Run State",
		"Tx ChnMgr[27] Run State",
		"Tx ChnMgr[28] Run State",
		"Tx ChnMgr[29] Run State",
		"Tx ChnMgr[30] Run State",
		"Tx ChnMgr[31] Run State",
#else
		"Rx Stream Mgr[32] Run State",
		"Rx Stream Mgr[33] Run State",
		"Rx Stream Mgr[34] Run State",
		"Rx Stream Mgr[35] Run State",
		"Rx Stream Mgr[36] Run State",
		"Rx Stream Mgr[37] Run State",
		"Rx Stream Mgr[38] Run State",
		"Rx Stream Mgr[39] Run State",
		"Rx Stream Mgr[40] Run State",
		"Rx Stream Mgr[41] Run State",
		"Tx Stream Mgr[42] Run State",
		"Tx Stream Mgr[43] Run State",
		"Tx Stream Mgr[44] Run State",
		"Tx Stream Mgr[45] Run State",
		"Tx Stream Mgr[46] Run State",
		"Tx Stream Mgr[47] Run State",
		"Rx ChnMgr[16] Run State",
		"Rx ChnMgr[16] Path Id",
		"Rx ChnMgr[17] Run State",
		"Rx ChnMgr[17] Path Id",
		"Rx ChnMgr[18] Run State",
		"Rx ChnMgr[18] Path Id",
		"Rx ChnMgr[19] Run State",
		"Rx ChnMgr[19] Path Id",
		"Rx ChnMgr[20] Run State",
		"Rx ChnMgr[20] Path Id",
		"Rx ChnMgr[21] Run State",
		"Rx ChnMgr[21] Path Id",
		"Rx ChnMgr[22] Run State",
		"Rx ChnMgr[22] Path Id",
		"Rx ChnMgr[23] Run State",
		"Rx ChnMgr[23] Path Id",
		"Rx ChnMgr[24] Run State",
		"Rx ChnMgr[24] Path Id",
		"Rx ChnMgr[25] Run State",
		"Rx ChnMgr[25] Path Id",
		"Tx ChnMgr[26] Run State",
		"Tx ChnMgr[26] Path Id",
		"Tx ChnMgr[27] Run State",
		"Tx ChnMgr[27] Path Id",
		"Tx ChnMgr[28] Run State",
		"Tx ChnMgr[28] Path Id",
		"Tx ChnMgr[29] Run State",
		"Tx ChnMgr[29] Path Id",
		"Tx ChnMgr[30] Run State",
		"Tx ChnMgr[30] Path Id",
		"Tx ChnMgr[31] Run State",
		"Tx ChnMgr[31] Path Id",
		"Rx ChnMgr[16] Num Packs",
		"Rx ChnMgr[17] Num Packs",
		"Rx ChnMgr[18] Num Packs",
		"Rx ChnMgr[19] Num Packs",
		"Rx ChnMgr[20] Num Packs",
		"Rx ChnMgr[21] Num Packs",
		"Rx ChnMgr[22] Num Packs",
		"Rx ChnMgr[23] Num Packs",
		"Rx ChnMgr[24] Num Packs",
		"Rx ChnMgr[25] Num Packs",
		"Rx ChnMgr[26] Num Packs",
		"Rx ChnMgr[27] Num Packs",
		"Rx ChnMgr[28] Num Packs",
		"Rx ChnMgr[29] Num Packs",
		"Rx ChnMgr[30] Num Packs",
		"Rx ChnMgr[31] Num Packs",
#endif
	};
	u8 req_msg[] = {
		/* 0x80, 0x00, 0x00, 0x00, */
		/* Route Status */
		0x80, 0x4f, 0x00, 0x00,
		0xff,

#if VERIDIAN
		/* Rx ChnMgr[16] Run State */
		0x90, 0x73, 0x20, 0x06,
		0x90, 0x72, 0x51, 0xfc,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[17] Run State */
		0x90, 0x72, 0x53, 0x2c,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[18] Run State */
		0x90, 0x72, 0x54, 0x5c,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[19] Run State */
		0x90, 0x72, 0x55, 0x8c,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[20] Run State */
		0x90, 0x72, 0x56, 0xbc,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[21] Run State */
		0x90, 0x72, 0x57, 0xec,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[22] Run State */
		0x90, 0x72, 0x59, 0x1c,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[23] Run State */
		0x90, 0x72, 0x5a, 0x4c,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[24] Run State */
		0x90, 0x72, 0x5b, 0x7c,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[25] Run State */
		0x90, 0x72, 0x5c, 0xac,
		0x80, 0x75, 0x00, 0x00,
		0xff,

		/* Tx ChnMgr[26] Run State */
		0x90, 0x72, 0x5d, 0xdc,
		0x80, 0x75, 0x00, 0x00,
		0xff,

		/* -----SK---- updated for build 16204 - few address bits */
		/* Tx ChnMgr[27] Run State */
		0x90, 0x72, 0x5f, 0x10,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[28] Run State */
		0x90, 0x72, 0x60, 0x44,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[29] Run State */
		0x90, 0x72, 0x61, 0x78,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[30] Run State */
		0x90, 0x72, 0x62, 0xac,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[31] Run State */
		0x90, 0x72, 0x63, 0xe0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
#else
		/* Rx Stream Mgr[32] Run State */
		0x90, 0x73, 0x20, 0x06,
		0x90, 0x72, 0x03, 0xe0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx Stream Mgr[33] Run State */
		0x90, 0x72, 0x04, 0xd8,
		0x80, 0x75, 0x00, 0x00,
		0xff,

		/* Rx Stream Mgr[34] Run State */
		0x90, 0x72, 0x05, 0xd0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx Stream Mgr[35] Run State */
		0x90, 0x72, 0x06, 0xc8,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx Stream Mgr[36] Run State */
		0x90, 0x72, 0x07, 0xc0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx Stream Mgr[37] Run State */
		0x90, 0x72, 0x08, 0xb8,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx Stream Mgr[38] Run State */
		0x90, 0x72, 0x09, 0xb0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx Stream Mgr[39] Run State */
		0x90, 0x72, 0x0a, 0xa8,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx Stream Mgr[40] Run State */
		0x90, 0x72, 0x0b, 0xa0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx Stream Mgr[41] Run State */
		0x90, 0x72, 0x0c, 0x98,
		0x80, 0x75, 0x00, 0x00,
		0xff,

		/* Tx Stream Mgr[42] Run State */
		0x90, 0x72, 0x0d, 0x90,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx Stream Mgr[43] Run State */
		0x90, 0x72, 0x0e, 0x88,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx Stream Mgr[44] Run State */
		0x90, 0x72, 0x0f, 0x80,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx Stream Mgr[45] Run State */
		0x90, 0x72, 0x10, 0x78,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx Stream Mgr[46] Run State */
		0x90, 0x72, 0x11, 0x70,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx Stream Mgr[47] Run State */
		0x90, 0x72, 0x12, 0x68,
		0x80, 0x75, 0x00, 0x00,
		0xff,

		/* Rx ChnMgr[16] Run State */
		0x90, 0x73, 0x20, 0x05,
		0x90, 0x72, 0xf2, 0x00,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[16] Path Id */
		0x90, 0x72, 0xf2, 0x04,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[17] Run State */
		0x90, 0x72, 0xf3, 0x20,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[17] Path Id */
		0x90, 0x72, 0xf3, 0x24,
		0x80, 0x75, 0x00, 0x00,
		0xff,

		/* Rx ChnMgr[18] Run State */
		0x90, 0x72, 0xf4, 0x40,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[18] Path Id */
		0x90, 0x72, 0xf4, 0x44,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[19] Run State */
		0x90, 0x72, 0xf5, 0x60,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[19] Path Id */
		0x90, 0x72, 0xf5, 0x64,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[20] Run State */
		0x90, 0x72, 0xf6, 0x80,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[20] Path Id */
		0x90, 0x72, 0xf6, 0x84,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[21] Run State */
		0x90, 0x72, 0xf7, 0xa0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[21] Path Id */
		0x90, 0x72, 0xf7, 0xa4,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[22] Run State */
		0x90, 0x72, 0xf8, 0xc0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[22] Path Id */
		0x90, 0x72, 0xf8, 0xc4,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[23] Run State */
		0x90, 0x72, 0xf9, 0xe0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[23] Path Id */
		0x90, 0x72, 0xf9, 0xe4,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[24] Run State */
		0x90, 0x72, 0xfb, 0x00,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[24] Path Id */
		0x90, 0x72, 0xfb, 0x04,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[25] Run State */
		0x90, 0x72, 0xfc, 0x20,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[25] Path Id */
		0x90, 0x72, 0xfc, 0x24,
		0x80, 0x75, 0x00, 0x00,
		0xff,

		/* Tx ChnMgr[26] Run State */
		0x90, 0x72, 0xfd, 0x40,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[26] Path Id */
		0x90, 0x72, 0xfd, 0x44,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[27] Run State */
		0x90, 0x72, 0xfe, 0x68,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[27] Path Id */
		0x90, 0x72, 0xfe, 0x6c,
		0x80, 0x75, 0x00, 0x00,
		0xff,

		/* Tx ChnMgr[28] Run State */
		0x90, 0x72, 0xff, 0x90,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[28] Path Id */
		0x90, 0x72, 0xff, 0x94,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[29] Run State */
		0x90, 0x73, 0x20, 0x06,
		0x90, 0x72, 0x00, 0xb8,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[29] Path Id */
		0x90, 0x72, 0x00, 0xbc,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[30] Run State */
		0x90, 0x72, 0x01, 0xe0,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[30] Path Id */
		0x90, 0x72, 0x01, 0xe4,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[31] Run State */
		0x90, 0x72, 0x03, 0x08,
		0x80, 0x75, 0x00, 0x00,
		0xff,
		/* Tx ChnMgr[31] Path Id */
		0x90, 0x72, 0x03, 0x0c,
		0x80, 0x75, 0x00, 0x00,
		0xff,


		/* Rx ChnMgr[16] Num Packs */
		0x90, 0x73, 0x20, 0x05,
		0x90, 0x72, 0xf1, 0xb8,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[17] Num Packs */
		0x90, 0x72, 0xf2, 0xd8,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[18] Num Packs */
		0x90, 0x72, 0xf3, 0xf8,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[19] Num Packs */
		0x90, 0x72, 0xf5, 0x18,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[20] Num Packs */
		0x90, 0x72, 0xf6, 0x38,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[21] Num Packs */
		0x90, 0x72, 0xf7, 0x58,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[22] Num Packs */
		0x90, 0x72, 0xf8, 0x78,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[23] Num Packs */
		0x90, 0x72, 0xf9, 0x98,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[24] Num Packs */
		0x90, 0x72, 0xfa, 0xb8,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[25] Num Packs */
		0x90, 0x72, 0xfb, 0xd8,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[26] Num Packs */
		0x90, 0x72, 0xfc, 0xf8,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[27] Num Packs */
		0x90, 0x72, 0xfe, 0x20,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[28] Num Packs */
		0x90, 0x72, 0xff, 0x48,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[29] Num Packs */
		0x90, 0x73, 0x20, 0x06,
		0x90, 0x72, 0x00, 0x70,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[30] Num Packs */
		0x90, 0x72, 0x01, 0x98,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
		/* Rx ChnMgr[31] Num Packs */
		0x90, 0x72, 0x02, 0xc0,
/*	0x80, 0x75, 0x00, 0x00, */
		0x80, 0x74, 0x00, 0x00,
		0xff,
#endif
		0xff
	};

	while (*(req_msg + i) != 0xff) {
		u8 ack_msg[4] = {
			0x00, 0x00, 0x00, 0x00,
		};

		while (*(req_msg+i) != 0xff) {
			es325_slim_write(es325, ES325_WRITE_VE_OFFSET,
					 ES325_WRITE_VE_WIDTH, req_msg+i, 4, 1);
			i = i + 4;
		}
		msleep(20);

		es325_slim_read(es325, ES325_READ_VE_OFFSET,
				ES325_READ_VE_WIDTH, ack_msg, 4, 1);

		pr_info("GAC:%s(): ping ack = %02x%02x%02x%02x\n", __func__,
			ack_msg[0], ack_msg[1], ack_msg[2], ack_msg[3]);

		ret = ret + snprintf(buf+ret, PAGE_SIZE - ret,
				     "%s=0x%02x%02x%02x%02x\n",
				     status_name[name_id],
				     ack_msg[0], ack_msg[1],
				     ack_msg[2], ack_msg[3]);
		i++;
		name_id++;
	}
	return ret;
}
static DEVICE_ATTR(route_status, 0644, es325_route_status_show, NULL);
/* /sys/devices/platform/msm_slim_ctrl.1/es325-codec-gen0/route_status */

static ssize_t es325_route_config_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	pr_info("GAC:%s(): route=%ld\n", __func__,
		es325_internal_route_num);
	return snprintf(buf, PAGE_SIZE, "route=%ld\n",
		       es325_internal_route_num);
}

static void es325_switch_route(long route_index)
{
	struct es325_priv *es325 = &es325_priv;
	int ret;

	if (route_index > ES325_INTERNAL_ROUTE_MAX) {
		pr_info("%s(): new es325_internal_route = %ld is out of range\n",
			__func__, route_index);
		return;
	}

	pr_info("GAC:%s():switch current es325_internal_route = %ld to new route = %ld\n",
		__func__, es325_internal_route_num, route_index);
	es325_internal_route_num = route_index;
	ret = es325_write_block_api(es325,
		&es325_internal_route_configs[es325_internal_route_num][0]);
}

static ssize_t es325_route_config_set(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	long route_index;
	int rc;

	pr_info("GAC:%s():buf = %s\n", __func__, buf);
	rc = kstrtol(buf, 10, &route_index);
	es325_switch_route(route_index);
	return count;
}

static DEVICE_ATTR(route_config, 0644, es325_route_config_show,
		   es325_route_config_set);
/* /sys/devices/platform/msm_slim_ctrl.1/es325-codec-gen0/route_config */


static ssize_t es325_reset_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	pr_info("GAC:%s():\n", __func__);
	return 0;
}

static ssize_t es325_reset_set(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{

	struct es325_priv *es325 = &es325_priv;

    if (es325->pdata->reset_gpio < 0) {
        return -1;
    }
    gpio_set_value(es325->pdata->reset_gpio, 0); 
    mdelay(1); 
    gpio_set_value(es325->pdata->reset_gpio, 1); 
    mdelay(50);


    return 0;
}
static DEVICE_ATTR(es325_reset, 0644, es325_reset_show,
		   es325_reset_set);


struct txhex_msg
{
    unsigned int size; 
    u8 msg[256];
} txhex_msg;

static ssize_t es325_txhex_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
    ssize_t ret = 0;
    int i = 0;


    if ( txhex_msg.size > 0)
    { 
        for(; i < txhex_msg.size; i++) 
        {
           ret  +=  snprintf(buf + ret, PAGE_SIZE - ret, "%02x",txhex_msg.msg[i]);
        }

    }

	pr_info("%s exit with %d\n", __func__, ret);
    txhex_msg.size = 0;
	return ret;
}

static ssize_t es325_txhex_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct es325_priv *es325 = &es325_priv;
	u8 cmd[128];
	int cmdlen = count / 2;
	int offset = 0;
	u8 resp[4];
	int rc = 0;

	pr_info("%s called\n", __func__);
	pr_info("%s count=%i\n", __func__, count);

	/* No command sequences larger than 128 bytes. */
//	BUG_ON(count > (128 * 2) + 1);
	/* Expect a even number of hexadecimal digits terminated by a
	 * newline. */
//	BUG_ON(!(count & 1));
    if ((count > (128 * 2) + 1) || !(count & 1)) {
        pr_err("%s: size %d error\n", __func__, count - 1);
        return -1;
    }

	rc = hex2bin(cmd, buf, cmdlen);
    if (rc != 0)
        return -1;
    txhex_msg.size = 0;
	pr_info("%s cmdlen=%i\n", __func__, cmdlen);
	while (offset < cmdlen) {
		/* Commands must be written in 4 byte blocks. */
		int wrsize = (cmdlen - offset > 4) ? 4 : cmdlen - offset;
		es325_slim_write(es325, ES325_WRITE_VE_OFFSET,
				 ES325_WRITE_VE_WIDTH, &cmd[offset], wrsize, 1);
		usleep_range(10000, 10000);
		es325_slim_read(es325, ES325_READ_VE_OFFSET,
				ES325_READ_VE_WIDTH, resp, 4, 1);
        memcpy(txhex_msg.msg + txhex_msg.size, resp, 4); 
        txhex_msg.size += 4;
		pr_info("%s %d: %02x%02x%02x%02x\n", __func__, txhex_msg.size, resp[0], resp[1], resp[2], resp[3]);
		offset += wrsize;
	}

	return count;
}
//liuyan 2013-12-19 modify for cta
#ifndef CONFIG_VENDOR_EDIT
static DEVICE_ATTR(txhex, 0777, es325_txhex_show, es325_txhex_set);
#else
static DEVICE_ATTR(txhex, 0644, es325_txhex_show, es325_txhex_set);
#endif


static int es325_present; 
static ssize_t es325_live_show(struct device *dev,
               struct device_attribute *attr,
               char *buf)
{
    pr_info("%s called es325_present =%d \n", __func__,es325_present);
    
    if(es325_present == 1)
        return snprintf(buf,PAGE_SIZE,"es325 present\n");
    else
        return snprintf(buf,PAGE_SIZE,"es325 not found\n");
    
}
static ssize_t es325_live_set(struct device *dev,
                  struct device_attribute *attr,
                  const char *buf, size_t count)
{
   pr_info("%s called\n", __func__);
   return 0;
}

static DEVICE_ATTR(es325_live, 0644, es325_live_show, es325_live_set);

#define ES325_FW_LOAD_BUF_SZ 4
static int es325_bootup(struct es325_priv *es325)
{
	static char sync_ok[] = { 0x80, 0, 0, 0 };
	char msg[16];
	unsigned int buf_frames;
	char *buf_ptr;
	int rc;

	pr_info("%s(): entry\n", __func__);

    mutex_lock(&es325->power_lock);
    es325->power_state = ES325_POWER_BOOT;
    mutex_unlock(&es325->power_lock);
/*
	gpio_set_value(es325->pdata->reset_gpio, 0); 
	mdelay(1); 
	gpio_set_value(es325->pdata->reset_gpio, 1); 
	mdelay(50);
	*/
	msleep(100);
	pr_info("%s(): write ES325_BOOT_CMD\n", __func__);
	memset(msg, 0, 16);
	msg[0] = ES325_BOOT_CMD & 0x00ff;
	msg[1] = ES325_BOOT_CMD >> 8;
	pr_info("%s(): msg[0] = 0x%02x\n", __func__, msg[0]);
	pr_info("%s(): msg[1] = 0x%02x\n", __func__, msg[1]);
	pr_info("%s(): msg[2] = 0x%02x\n", __func__, msg[2]);
	pr_info("%s(): msg[3] = 0x%02x\n", __func__, msg[3]);
	rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
			     ES325_WRITE_VE_WIDTH, msg, 4, 0);
	if (rc < 0) {
		pr_err("%s(): firmware load failed boot write\n", __func__);
		return	0; /* rc; */
	}
	msleep(100);
	pr_info("%s(): read boot cmd ack\n", __func__);
	memset(msg, 0, 16);
	rc = ES325_BUS_READ(es325, ES325_READ_VE_OFFSET, ES325_READ_VE_WIDTH,
			    msg, 4, 0);
	pr_info("%s(): msg[0] = 0x%02x\n", __func__, msg[0]);
	pr_info("%s(): msg[1] = 0x%02x\n", __func__, msg[1]);
	pr_info("%s(): msg[2] = 0x%02x\n", __func__, msg[2]);
	pr_info("%s(): msg[3] = 0x%02x\n", __func__, msg[3]);
	if (rc < 0) {
		pr_err("%s(): firmware load failed boot ack\n", __func__);
		return	rc;
	}
	if ((msg[0] != (ES325_BOOT_ACK >> 8)) || (msg[1] != (ES325_BOOT_ACK & 0x00ff))) {
		pr_err("%s(): firmware load failed boot ack pattern", __func__);
		return	-EIO;
	}
	msleep(100);

	pr_info("%s(): write firmware image\n", __func__);
	/* send image */
	buf_frames = es325->fw->size / ES325_FW_LOAD_BUF_SZ;
	pr_info("%s(): buf_frames = %d", __func__, buf_frames);
	buf_ptr = (char *)es325->fw->data;
	for ( ; buf_frames; --buf_frames, buf_ptr += ES325_FW_LOAD_BUF_SZ) {
		rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
				     ES325_WRITE_VE_WIDTH, buf_ptr,
				     ES325_FW_LOAD_BUF_SZ, 0);
		if (rc < 0) {
			pr_err("%s(): firmware load failed\n", __func__);
			return -EIO;
		}
	}
	if (es325->fw->size % ES325_FW_LOAD_BUF_SZ) {
		rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
				     ES325_WRITE_VE_WIDTH, buf_ptr,
				     es325->fw->size % ES325_FW_LOAD_BUF_SZ, 0);
		if (rc < 0) {
			pr_err("%s(): firmware load failed\n", __func__);
			return -EIO;
		}
	}

	/* Give the chip some time to become ready after firmware
	 * download. */
	msleep(100);

	pr_info("%s(): write ES325_SYNC_CMD\n", __func__);
	memset(msg, 0, 16);
	msg[0] = ES325_SYNC_CMD >> 8;
	msg[1] = ES325_SYNC_CMD & 0x00ff;
	msg[2] = ES325_SYNC_POLLING >> 8;
	msg[3] = ES325_SYNC_POLLING & 0x00ff;
	pr_info("%s(): msg[0] = 0x%02x\n", __func__, msg[0]);
	pr_info("%s(): msg[1] = 0x%02x\n", __func__, msg[1]);
	pr_info("%s(): msg[2] = 0x%02x\n", __func__, msg[2]);
	pr_info("%s(): msg[3] = 0x%02x\n", __func__, msg[3]);
	rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
			     ES325_WRITE_VE_WIDTH, msg, 4, 1);
	if (rc < 0) {
		pr_err("%s(): firmware load failed sync write\n", __func__);
		return rc;
	}
	msleep(100);
	pr_info("%s(): read sync cmd ack\n", __func__);
	memset(msg, 0, 16);
	rc = ES325_BUS_READ(es325, ES325_READ_VE_OFFSET, ES325_READ_VE_WIDTH,
			    msg, 4, 1);
	pr_info("%s(): msg[0] = 0x%02x\n", __func__, msg[0]);
	pr_info("%s(): msg[1] = 0x%02x\n", __func__, msg[1]);
	pr_info("%s(): msg[2] = 0x%02x\n", __func__, msg[2]);
	pr_info("%s(): msg[3] = 0x%02x\n", __func__, msg[3]);
	if (rc < 0) {
		pr_err("%s(): error reading firmware sync ack rc=%d\n",
		       __func__, rc);
		return rc;
	}
	if (memcmp(msg, sync_ok, 4) == 0) {
		pr_info("%s(): firmware sync ack good=0x%02x%02x%02x%02x\n",
			__func__, msg[0], msg[1], msg[2], msg[3]);
		es325_fw_ready =1;
	} else {
		pr_err("%s(): firmware sync ack failed=0x%02x%02x%02x%02x\n",
		       __func__, msg[0], msg[1], msg[2], msg[3]);
		return -EIO;
	}

    mutex_lock(&es325->power_lock);
    es325->power_state = ES325_POWER_AWAKE;
    mutex_unlock(&es325->power_lock);

	pr_info("%s(): exit\n", __func__);
	return 0;
}

static int register_snd_soc(struct es325_priv *priv);

static int fw_download(void *arg)
{
	struct es325_priv *priv = (struct es325_priv *)arg;
	int rc;

	pr_info("%s(): called\n", __func__);
	pr_info("%s(): es325 gen0 LA=%d\n", __func__, priv->gen0_client->laddr);
#ifdef BUS_TRANSACTIONS
	rc = es325_bootup(priv);
#endif
    if (rc == 0)
        es325_present = 1;
    else
        es325_present = 0;

	pr_info("%s(): bootup rc=%d\n", __func__, rc);
	pr_info("%s(): would release firmware here...\n", __func__);
	release_firmware(priv->fw);

	pr_info("%s(): registering sound soc\n", __func__);
	rc = register_snd_soc(priv);
	pr_info("%s(): register_snd_soc rc=%d\n", __func__, rc);

#ifdef FIXED_CONFIG
	es325_fixed_config(priv);
#endif

      es325_sleep_request(priv);
	pr_info("%s(): release module\n", __func__);
	module_put(THIS_MODULE);
	pr_info("%s(): exiting fw_download\n", __func__);
	return 0;
}



static int es325_sleep(struct es325_priv *es325)
{
	char msg[16];
	int rc;

	pr_info("%s(): entry\n", __func__);

	memset(msg, 0, 16);
	msg[0] = ES325_SET_SMOOTH >> 8;
	msg[1] = ES325_SET_SMOOTH  & 0x00ff;
	msg[2] = ES325_SET_SMOOTH_RATE   >> 8;
	msg[3] = ES325_SET_SMOOTH_RATE   & 0x00ff;
	rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
			     ES325_WRITE_VE_WIDTH, msg, 4, 1);
	if (rc < 0) {
		pr_err("%s(): es325_slim_write()", __func__);
		return rc;
	}

	/* write 0x8000_0001
	 * sleep 20 ms
	 * clocks off
	 */
	memset(msg, 0, 16);
	msg[0] = ES325_SET_POWER_STATE >> 8;
	msg[1] = ES325_SET_POWER_STATE & 0x00ff;
	msg[2] = ES325_SET_POWER_STATE_SLEEP >> 8;
	msg[3] = ES325_SET_POWER_STATE_SLEEP & 0x00ff;
	rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
			     ES325_WRITE_VE_WIDTH, msg, 4, 1);
	if (rc < 0) {
		pr_err("%s(): request sleep failed", __func__);
		return rc;
	}

	msleep(25);

	/* clocks off */
	if (es325->pdata->es325_clk_cb)
	{
		es325->pdata->es325_clk_cb(0); 
	}

	msleep(5);

	pr_info("%s(): exit\n", __func__);
	return 0;
}

static void es325_delayed_sleep(struct work_struct *w)
{
    int ch_tot;
    int ports_active = (es325_rx1_route_enable ||
            es325_rx2_route_enable || es325_tx1_route_enable);
    /* If there are active streams we do not sleep.
     * Count the front end (FE) streams ONLY.
     */
    ch_tot = 0;
    ch_tot += es325_priv.dai[ES325_SLIM_1_PB].ch_tot;
    ch_tot += es325_priv.dai[ES325_SLIM_2_PB].ch_tot;

    ch_tot += es325_priv.dai[ES325_SLIM_1_CAP].ch_tot;
    pr_info("%s %d active channels, ports_active: %d \n", __func__,
		    ch_tot, ports_active);
    mutex_lock(&es325_priv.power_lock);
    if ((ch_tot <= 0) && (ports_active == 0) && (es325_priv.power_state ==  ES325_POWER_SLEEP_PENDING)) 
    {
	    es325_sleep(&es325_priv);
	    es325_priv.power_state = ES325_POWER_SLEEP; 
	    es325_power_state = 0;
    } else {
           es325_priv.power_state = ES325_POWER_AWAKE;
           es325_power_state = 1;
          
    	}
    mutex_unlock(&es325_priv.power_lock);
}


static void es325_sleep_request(struct es325_priv *es325)
{
    pr_info("%s \n", __func__);
    mutex_lock(&es325->power_lock); 
    pr_info("%s power_state = %d\n", __func__, es325_priv.power_state);
    if (es325_priv.power_state == ES325_POWER_AWAKE)
    {
        schedule_delayed_work(&es325->sleep_work,
                msecs_to_jiffies(ES325_SLEEP_DELAY));
        es325_priv.power_state = ES325_POWER_SLEEP_PENDING;
    } 
    mutex_unlock(&es325->power_lock);
}

	/*
 * Delay for receiving response can be up to 20 ms.
 * To minimize waiting time, response is checking
 * up to 20 times with 1ms delay.
*/
#define MAX_TRIALS	20
#define SMB_DELAY	30000
const char NOT_READY[4] = {0x00, 0x00, 0x00, 0x00};
#if 0
static int es325_request_response(struct es325_priv *es325,
				  char *rqst_ptr, int rqst_len, int rqst_order,
				  char *rspn_ptr, int rspn_len, int rspn_order,
				  int match, int return_if_fail)
{
	int rc = 0;
	int i = 0;
	char msg[4];
	struct slim_device *sbdev = es325->gen0_client;

	for (i = 0; i < MAX_TRIALS; i++)
    {
        rc = 0;
        memcpy(msg, rqst_ptr, 4);
        rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
                ES325_WRITE_VE_WIDTH, msg, 4, rqst_order);
        if (rc < 0) {
            dev_err(&sbdev->dev, "=[ES325]=%s: slim write fail, rc=%d\n", __func__, rc);
            msleep(30); 
        }		
        else 
            break; 
        usleep_range(SMB_DELAY, SMB_DELAY + 1000);
    }
    if (rc == 0) {
        //es325_slim_read has 5 trials. 
        for (i = 0; i <  MAX_TRIALS ; i++)
        {
            memset(msg, 0, 4);
            rc = ES325_BUS_READ(es325, ES325_READ_VE_OFFSET,
                    ES325_READ_VE_WIDTH, msg, rspn_len, rspn_order);
            if (rc < 0) { 
                dev_err(&sbdev->dev, "=[ES325]=%s: slim read fail, rc=%d\n", __func__, rc); 
                usleep_range(SMB_DELAY, SMB_DELAY + 1000);

            } else if (memcmp(msg, NOT_READY, 4) != 0) {
                rc = -1;
                memcpy(rspn_ptr, msg, rspn_len);
                if (memcmp(rqst_ptr, msg, rspn_len) == 0)
                {
                    pr_info("%s():sync reply = 0x%02x%02x%02x%02x\n", __func__, msg[0], msg[1], msg[2], msg[3]);
                    rc = 0;
                    break;
                }
            }
        }
    } 	
     pr_info("%s: tries %d with %d\n", __func__, i, rc);
    return rc;
}


#endif
static int es325_request_response(struct es325_priv *es325,
				  char *rqst_ptr, int rqst_len, int rqst_order,
				  char *rspn_ptr, int rspn_len, int rspn_order,
				  int match, int return_if_fail)
{
	int rc = 0;
	int i = 0;
	char msg[4];
	struct slim_device *sbdev = es325->gen0_client;

	pr_info("%s: Enter new \n", __func__);
	for (i = 0; i < MAX_TRIALS; i++)
    {
        rc = 0;

	usleep_range(SMB_DELAY, SMB_DELAY + 1000);
        memcpy(msg, rqst_ptr, 4);
		pr_info("%s: Sync Write \n", __func__);
        rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
                ES325_WRITE_VE_WIDTH, msg, 4, rqst_order);
        if (rc < 0) {
            dev_err(&sbdev->dev, "=[ES325]=%s: slim write fail, rc=%d\n", __func__, rc);
           
        }		
        
    	if (rc == 0) {
        //es325_slim_read has 5 trials.
            msleep(5);
            memset(msg, 0, 4);
			pr_info("%s: Sync Read \n", __func__);
            rc = ES325_BUS_READ(es325, ES325_READ_VE_OFFSET,
                    ES325_READ_VE_WIDTH, msg, rspn_len, rspn_order);
            if (rc < 0) { 
                dev_err(&sbdev->dev, "=[ES325]=%s: slim read fail, rc=%d\n", __func__, rc); 
                //usleep_range(SMB_DELAY, SMB_DELAY + 1000);

            } else if (memcmp(msg, NOT_READY, 4) != 0) {
                rc = -1;
                memcpy(rspn_ptr, msg, rspn_len);
                if (memcmp(rqst_ptr, msg, rspn_len) == 0)
                {
                    pr_info("%s():sync reply = 0x%02x%02x%02x%02x\n", __func__, msg[0], msg[1], msg[2], msg[3]);
                    rc = 0;
                    break;
                }
            }
        }
    } 	
     pr_info("%s: tries %d with %d\n", __func__, i, rc);
    return rc;
}


#define SYNC_DELAY 30
static int es325_wakeup(struct es325_priv *es325)
{
	static char sync_ok[] = { 0x80, 0, 0, 0x0 };
  	char resp[4];
	int rc;
	pr_info("%s(): entry\n", __func__);
	/* 1 - clocks on
	 * 2 - wakeup 1 -> 0
	 * 3 - sleep 30 ms
	 * 4 - Send sync command (0x8000, 0x0001)
	 * 5 - Read sync ack
	 * 6 - wakeup 0 -> 1
	 */

	mutex_lock(&es325->power_lock);

	if (delayed_work_pending(&es325->sleep_work)) { 
		rc = cancel_delayed_work_sync(&es325->sleep_work); 
		pr_info("%s(): cancel delayed work\n", __func__);
		es325->power_state = ES325_POWER_AWAKE; 
		es325_power_state = 1;
		mutex_unlock(&es325->power_lock);
		return 0;
	}

	if ( es325->power_state !=  ES325_POWER_SLEEP) {
		mutex_unlock(&es325->power_lock);
		return 0;
	}

	if (es325->pdata->es325_clk_cb)
	{
		es325->pdata->es325_clk_cb(1); 
		usleep_range(3000, 3100);
	}
	pr_info("%s(): gpio low\n", __func__);
	gpio_set_value(es325->pdata->wakeup_gpio, 0);		
	pr_info("%s(): delay 30\n", __func__);
	msleep(SYNC_DELAY);

	rc = es325_request_response(es325, sync_ok, 4, 1, resp, 4, 1, 1, 0);
	if (rc < 0) {
		pr_err("%s: es325 wakeup FAIL\n", __func__);

		if (es325->pdata->es325_clk_cb)
		{
			es325->pdata->es325_clk_cb(0); 
			usleep_range(3000, 3100);
		}
		pr_info("%s(): gpio high\n", __func__);
		gpio_set_value(es325->pdata->wakeup_gpio, 1);
		usleep_range(5000, 5100);
		mutex_unlock(&es325->power_lock);
		return rc;
	}

	gpio_set_value(es325->pdata->wakeup_gpio, 1);
	usleep_range(5000, 5100);

	es325->power_state = ES325_POWER_AWAKE; 
	es325_power_state = 1;
	mutex_unlock(&es325->power_lock);

	pr_info("%s(): exit\n", __func__);
	return 0;
}

/* static const char *es325_algo_sample_rate_texts[] = { */
/*      "Narrow band", "Wide band", "Super wide band" */
/* }; */
/* static const struct soc_enum es325_algo_sample_rate_enum = */
/*      SOC_ENUM_SINGLE(ES325_ALGO_SAMPLE_RATE, 0, */
/*                      ARRAY_SIZE(es325_algo_sample_rate_texts), */
/*                      es325_algo_sample_rate_texts); */

/* static const char *es325_change_status_texts[] = { */
/*      "Active", "Muting", "Switching", "Unmuting", "Inactive" */
/* }; */
/* static const struct soc_enum es325_change_status_enum = */
/*      SOC_ENUM_SINGLE(ES325_CHANGE_STATUS, 0, */
/*                      ARRAY_SIZE(es325_change_status_texts), */
/*                      es325_change_status_texts); */

/* static const char *es325_rx_tx_mbc_texts[] = { */
/*      "Off", "On voice", "On", */
/* }; */
/* static const struct soc_enum es325_tx_mbc_enum = */
/*      SOC_ENUM_SINGLE(ES325_TX_MBC, 0, */
/*                      ARRAY_SIZE(es325_rx_tx_mbc_texts), */
/*                      es325_rx_tx_mbc_texts); */
/* static const struct soc_enum es325_rx_mbc_enum = */
/*      SOC_ENUM_SINGLE(ES325_RX_MBC, 0, */
/*                      ARRAY_SIZE(es325_rx_tx_mbc_texts), */
/*                      es325_rx_tx_mbc_texts); */

/* static const char *es325_rx_ns_adaptation_speed_texts[] = { */
/*      "Reserved", "Normal", "Fast", */
/* }; */
/* static const struct soc_enum es325_rx_ns_adaptation_speed_enum = */
/*      SOC_ENUM_SINGLE(ES325_RX_NS_ADAPTATION_SPEED, 0, */
/*                      ARRAY_SIZE(es325_rx_ns_adaptation_speed_texts), */
/*                      es325_rx_ns_adaptation_speed_texts); */

/* static const char *es325_avalon_av_config_texts[] = { */
/*      "AV all", "AV ISP", "AV ESP", "AV HPH", "AV REC", "AV DIG", "AV EAR" */
/* }; */
/* static const struct soc_enum es325_avalon_av_config_enum = */
/*      SOC_ENUM_SINGLE(ES325_AVALON_AV_CONFIG, 0, */
/*                      ARRAY_SIZE(es325_avalon_av_config_texts), */
/*                      es325_avalon_av_config_texts); */

/* static const char *es325_avalon_eq_preset_texts[] = { */
/*      "Flat", "Jazz", "Pop", "Classic", "Rock", "Bass boost", "Treble boost", */
/*      "R and B", "Hip hop", "Vocal boost", "Latin", "Piano", "New age", */
/*      "Country", "Blues" */
/* }; */
/* static const struct soc_enum es325_avalon_eq_preset_enum = */
/*      SOC_ENUM_SINGLE(ES325_AVALON_EQ_PRESET, 0, */
/*                      ARRAY_SIZE(es325_avalon_eq_preset_texts), */
/*                      es325_avalon_eq_preset_texts); */

/* static const char *es325_avalon_stat_ns_adap_speed_texts[] = { */
/*      "Reserved", "Slow", "Medium", "Fast", */
/* }; */
/* static const struct soc_enum es325_avalon_stat_ns_adap_speed_enum = */
/*      SOC_ENUM_SINGLE(ES325_AVALON_EQ_PRESET, 0, */
/*                      ARRAY_SIZE(es325_avalon_eq_preset_texts), */
/*                      es325_avalon_eq_preset_texts); */

/* static const char *es325_mode_config_texts[] = { */
/*      "Close talk", "Hand-held speakerphone", "Desktop speakerphone", */
/*      "1Mic headset", "1Mic bluetooth", "ASR assist close talk", */
/*      "ARS assist hand-held speakerphone" */
/* }; */
/* static const struct soc_enum es325_mode_config_enum = */
/*      SOC_ENUM_SINGLE(ES325_MIC_CONFIG, 0, */
/*                      ARRAY_SIZE(es325_mode_config_texts), */
/*                      es325_mode_config_texts); */

/* static const char *es325_audio_config_texts[] = { */
/*      "Reserved00", "Reserved01", "Headphone playback", "Audio recording" */
/* }; */
/* static const struct soc_enum es325_audio_config_enum = */
/*      SOC_ENUM_SINGLE(ES325_AVALON_AV_CONFIG, 0, */
/*                      ARRAY_SIZE(es325_audio_config_texts), */
/*                      es325_audio_config_texts); */

/* static const char *es325_parametric_eq_texts[] = { */
/*      "Off", "Jazz", "Pop", "Classic", "Rock", "Custom1", "Custom2", */
/*      "R and B", "Hip hop" */
/* }; */
/* static const struct soc_enum es325_parametric_eq_enum = */
/*      SOC_ENUM_SINGLE(ES325_AVALON_EQ_PRESET, 0, */
/*                      ARRAY_SIZE(es325_parametric_eq_texts), */
/*                      es325_parametric_eq_texts); */

/* static const char *es325_stereo_widening_width_texts[] = { */
/*      "Narrow", "Normal", "Wide" */
/* }; */
/* static const struct soc_enum es325_stereo_widening_width_enum = */
/*      SOC_ENUM_SINGLE(ES325_AVALON_STEREO_WIDTH, 0, */
/*                      ARRAY_SIZE(es325_stereo_widening_width_texts), */
/*                      es325_stereo_widening_width_texts); */

static int es325_put_control_value(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	/* struct snd_soc_codec *codec = es325_priv.codec; */
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	/* unsigned int shift = mc->shift; */
	/* unsigned int max = mc->max; */
	/* unsigned int invert = mc->invert; */
	unsigned int value;
	int rc = 0;

	/* dev_dbg(codec->dev, "%s(): reg = %d\n", __func__, reg); */
	/* dev_dbg(codec->dev, "%s(): shift = %d\n", __func__, shift); */
	/* dev_dbg(codec->dev, "%s(): max = %d\n", __func__, max); */
	/* dev_dbg(codec->dev, "%s(): invert = %d\n", __func__, invert); */
	/* dev_dbg(codec->dev, "%s(): value = %ld\n", __func__, */
	/*	ucontrol->value.integer.value[0]); */
	value = ucontrol->value.integer.value[0];
	/* rc = snd_soc_write(codec, reg, value); */
	rc = es325_write(NULL, reg, value);

	return 0;
}

static int es325_get_control_value(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	/* struct snd_soc_codec *codec = es325_priv.codec; */
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	/* unsigned int shift = mc->shift; */
	/* unsigned int max = mc->max; */
	/* unsigned int invert = mc->invert; */
	unsigned int value;

	/* dev_dbg(codec->dev, "%s(): reg = %d\n", __func__, reg); */
	/* dev_dbg(codec->dev, "%s(): shift = %d\n", __func__, shift); */
	/* dev_dbg(codec->dev, "%s(): max = %d\n", __func__, max); */
	/* dev_dbg(codec->dev, "%s(): invert = %d\n", __func__, invert); */
	/* value = snd_soc_read(codec, reg); */
	value = es325_read(NULL, reg);
	/* dev_dbg(codec->dev, "%s(): value = %d\n", __func__, value); */
	ucontrol->value.integer.value[0] = value;
	/* dev_dbg(codec->dev, "%s(): value = %ld\n", __func__, */
	/*	ucontrol->value.integer.value[0]); */

	return 0;
}

static int es325_preset_put_control_value(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	/* struct snd_soc_codec *codec = es325_priv.codec; */
	struct es325_priv *es325 = &es325_priv;
	unsigned int reg = ES325_PRESET;
	unsigned int value = 0;
	int rc = 0;
	char msg[16];
	int msg_len = 0;
	char *msg_ptr;
	int i;

	if(!es325_fw_ready){
		pr_err("%s(): ES325 Firmware is not ready\n", __func__);
		return rc;
	}
    if (es325->power_state == ES325_POWER_SLEEP)
        return -EINVAL;

	value = ucontrol->value.integer.value[0]; 
printk("%s:Enter , value = %d\n",__func__,value);
    rc = es325_build_preset_write_msg(msg, &msg_len, reg, value);

 	if (rc) {
		pr_err("%s(): failed to build write message for address = 0x%04x\n",
			__func__, reg);
	}        
    

    msg_ptr = msg;
	for (i = msg_len; i > 0; i -= 4) {
		rc = ES325_BUS_WRITE(es325, ES325_WRITE_VE_OFFSET,
				     ES325_WRITE_VE_WIDTH, msg_ptr, 4, 1);
		if (rc < 0) {
			pr_err("%s(): es325_xxxx_write() Failed\n", __func__);
			return rc;
		}
		msg_ptr += 4;
	} 
    	es325_presets = value & 0xffff; 
	pr_info("%s() exit: preset  = %d \n", __func__, es325_presets);
	return 0;
}

static int es325_preset_get_control_value(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = es325_presets;

	return 0;
}

static int es325_put_control_enum(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e =
		(struct soc_enum *)kcontrol->private_value;
	unsigned int reg = e->reg;
	unsigned int max = e->max;
	unsigned int value;
	int rc = 0;

	pr_info("%s(): reg = %d\n", __func__, reg);
	pr_info("%s(): max = %d\n", __func__, max);
	value = ucontrol->value.enumerated.item[0];
	rc = es325_write(NULL, reg, value);

	return 0;
}

static int es325_get_control_enum(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *e =
		(struct soc_enum *)kcontrol->private_value;
	unsigned int reg = e->reg;
	unsigned int max = e->max;
	unsigned int value;

	pr_info("%s(): reg = %d\n", __func__, reg);
	pr_info("%s(): max = %d\n", __func__, max);
	value = es325_read(NULL, reg);
	ucontrol->value.enumerated.item[0] = value;
       pr_info("%s(): value = %x\n", __func__, value);
	return 0;
}

static int es325_get_rx1_route_enable_value(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = es325_rx1_route_enable;
	pr_info("%s(): es325_rx1_route_enable = %d\n", __func__,
		es325_rx1_route_enable);

	return 0;
}

static int es325_put_rx1_route_enable_value(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{

	if(!es325_fw_ready){
		pr_err("%s():ES325 Firmware is not ready\n", __func__);
		return 0;
	}
	es325_rx1_route_enable = ucontrol->value.integer.value[0];
	pr_info("%s(): es325_rx1_route_enable = %d\n", __func__,
		es325_rx1_route_enable);

	return 0;
}

static int es325_get_tx1_route_enable_value(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	
	ucontrol->value.integer.value[0] = es325_tx1_route_enable;
	pr_info("%s(): es325_tx1_route_enable = %d\n", __func__,
		es325_tx1_route_enable);

	return 0;
}

static int es325_put_tx1_route_enable_value(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	if(!es325_fw_ready){
		pr_err("%s():ES325 Firmware is not ready\n", __func__);
		return 0;
	}
	es325_tx1_route_enable = ucontrol->value.integer.value[0];
	pr_info("%s(): es325_tx1_route_enable = %d\n", __func__,
		es325_tx1_route_enable);

	return 0;
}

static int es325_get_rx2_route_enable_value(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = es325_rx2_route_enable;
	pr_info("%s(): es325_rx2_route_enable = %d\n", __func__,
		es325_rx2_route_enable);

	return 0;
}

static int es325_put_rx2_route_enable_value(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	if(!es325_fw_ready){
		pr_err("%s():ES325 Firmware is not ready\n", __func__);
		return 0;
	}
	
	es325_rx2_route_enable = ucontrol->value.integer.value[0];
	pr_info("%s(): es325_rx2_route_enable = %d\n", __func__,
		es325_rx2_route_enable);

	return 0;
}

int es325_remote_route_enable(struct snd_soc_dai *dai)
{
	pr_info("GAC:%s(dai->name = %s): entry\n", __func__, dai->name);
	pr_info("GAC:%s(dai->id = %d): entry\n", __func__, dai->id);

	switch (dai->id) {
	case ES325_SLIM_1_PB:
		return es325_rx1_route_enable;
	case ES325_SLIM_1_CAP:
		return es325_tx1_route_enable;
	case ES325_SLIM_2_PB:
		return es325_rx2_route_enable;
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(es325_remote_route_enable);

static int es325_put_internal_route_config(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	pr_info("GAC:%s():new route index = %ld\n", __func__, ucontrol->value.integer.value[0]);
	es325_switch_route(ucontrol->value.integer.value[0]);
	return 0;
}

static int es325_get_internal_route_config(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = es325_internal_route_num;
	pr_info("%s(): internal_route_num = %ld\n", __func__,
		ucontrol->value.integer.value[0]);
	pr_info("%s(): internal_route_num = %ld\n", __func__,
		es325_internal_route_num);

	return 0;
}

static int es325_get_audio_custom_profile(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int es325_put_audio_custom_profile(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_value *ucontrol)
{
	int index = ucontrol->value.integer.value[0];

	if (index < ES325_CUSTOMER_PROFILE_MAX)
		es325_write_data_block_cmd(&es325_priv,
			&es325_audio_custom_profiles[index][0]);
	return 0;
}

static int es325_ap_put_tx1_ch_cnt(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	pr_info("GAC:%s():ucontrol = %d\n", __func__,
		ucontrol->value.enumerated.item[0]);
	es325_ap_tx1_ch_cnt = ucontrol->value.enumerated.item[0] + 1;
	pr_info("GAC:%s():tx1 ch cnt = %d\n", __func__,
		es325_ap_tx1_ch_cnt);
	return 0;
}

static int es325_ap_get_tx1_ch_cnt(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	pr_info("GAC:%s(): tx1 ch cnt = %d\n", __func__,
		es325_ap_tx1_ch_cnt);
	ucontrol->value.enumerated.item[0] = es325_ap_tx1_ch_cnt - 1;
	pr_info("%s(): ucontrol = %d\n", __func__,
		ucontrol->value.enumerated.item[0]);

	return 0;
}

static const char * const es325_ap_tx1_ch_cnt_texts[] = {
	"One", "Two"
};
static const struct soc_enum es325_ap_tx1_ch_cnt_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(es325_ap_tx1_ch_cnt_texts),
			es325_ap_tx1_ch_cnt_texts);

/* generic gain translation */
static int es325_index_to_gain(int min, int step, int index)
{
	return	min + (step * index);
}
static int es325_gain_to_index(int min, int step, int gain)
{
	return	(gain - min) / step;
}

/* dereverb gain */
static int es325_put_dereverb_gain_value(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int value;
	int rc = 0;

	if (ucontrol->value.integer.value[0] <= 12) {
		pr_info("GAC:%s() ucontrol = %ld\n", __func__,
			ucontrol->value.integer.value[0]);
		value = es325_index_to_gain(-12, 1, ucontrol->value.integer.value[0]);
		pr_info("GAC:%s() value = %d\n", __func__, value);
		rc = es325_write(NULL, reg, value);
	}

	return 0;
}

static int es325_get_dereverb_gain_value(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int value;

	value = es325_read(NULL, reg);
	pr_info("GAC:%s() value = %d\n", __func__, value);
	ucontrol->value.integer.value[0] = es325_gain_to_index(-12, 1, value);
	pr_info("GAC:%s() ucontrol = %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	return 0;
}

/* bwe high band gain */
static int es325_put_bwe_high_band_gain_value(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int value;
	int rc = 0;

	if (ucontrol->value.integer.value[0] <= 30) {
		pr_info("GAC:%s() ucontrol = %ld\n", __func__,
			ucontrol->value.integer.value[0]);
		value = es325_index_to_gain(-10, 1, ucontrol->value.integer.value[0]);
		pr_info("GAC:%s() value = %d\n", __func__,
			value);
		rc = es325_write(NULL, reg, value);
	}

	return 0;
}

static int es325_get_bwe_high_band_gain_value(struct snd_kcontrol *kcontrol,
					      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int value;

	value = es325_read(NULL, reg);
	pr_info("GAC:%s() value = %d\n", __func__, value);
	ucontrol->value.integer.value[0] = es325_gain_to_index(-10, 1, value);
	pr_info("GAC:%s() ucontrol = %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	return 0;
}

/* bwe max snr */
static int es325_put_bwe_max_snr_value(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int value;
	int rc = 0;

	if (ucontrol->value.integer.value[0] <= 70) {
		pr_info("GAC:%s() ucontrol = %ld\n", __func__,
			ucontrol->value.integer.value[0]);
		value = es325_index_to_gain(-20, 1, ucontrol->value.integer.value[0]);
		pr_info("GAC:%s() value = %d\n", __func__,
			value);
		rc = es325_write(NULL, reg, value);
	}

	return 0;
}

static int es325_get_bwe_max_snr_value(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int value;

	value = es325_read(NULL, reg);
	pr_info("GAC:%s() value = %d\n", __func__, value);
	ucontrol->value.integer.value[0] = es325_gain_to_index(-20, 1, value);
	pr_info("GAC:%s() ucontrol = %ld\n", __func__,
		ucontrol->value.integer.value[0]);

	return 0;
}

static const char * const es325_mic_config_texts[] = {
	"CT 2-mic", "FT 2-mic", "DV 1-mic", "EXT 1-mic", "BT 1-mic",
	"CT ASR 2-mic", "FT ASR 2-mic", "EXT ASR 1-mic", "FT ASR 1-mic",
};
static const struct soc_enum es325_mic_config_enum =
	SOC_ENUM_SINGLE(ES325_MIC_CONFIG, 0,
			ARRAY_SIZE(es325_mic_config_texts),
			es325_mic_config_texts);

static const char * const es325_aec_mode_texts[] = {
	"Off", "On", "rsvrd2", "rsvrd3", "rsvrd4", "On half-duplex"
};
static const struct soc_enum es325_aec_mode_enum =
	SOC_ENUM_SINGLE(ES325_AEC_MODE, 0, ARRAY_SIZE(es325_aec_mode_texts),
			es325_aec_mode_texts);

static const char * const es325_algo_rates_text[] = {
	"fs=8khz", "fs=16khz", "fs=24khz", "fs=48khz", "fs=96khz", "fs=192khz"
};
static const struct soc_enum es325_algo_sample_rate_enum =
	SOC_ENUM_SINGLE(ES325_ALGO_SAMPLE_RATE, 0,
			ARRAY_SIZE(es325_algo_rates_text),
			es325_algo_rates_text);
static const struct soc_enum es325_algo_mix_rate_enum =
	SOC_ENUM_SINGLE(ES325_MIX_SAMPLE_RATE, 0,
			ARRAY_SIZE(es325_algo_rates_text),
			es325_algo_rates_text);

static const char * const es325_algorithms_text[] = {
	"None", "VP", "Two CHREC", "AUDIO", "Four CHPASS"
};
static const struct soc_enum es325_algorithms_enum =
	SOC_ENUM_SINGLE(ES325_ALGO_SAMPLE_RATE, 0,
			ARRAY_SIZE(es325_algorithms_text),
			es325_algorithms_text);

static const char * const es325_off_on_texts[] = {
	"Off", "On"
};
static const struct soc_enum es325_veq_enable_enum =
	SOC_ENUM_SINGLE(ES325_VEQ_ENABLE, 0, ARRAY_SIZE(es325_off_on_texts),
			es325_off_on_texts);
static const struct soc_enum es325_dereverb_enable_enum =
	SOC_ENUM_SINGLE(ES325_DEREVERB_ENABLE, 0,
			ARRAY_SIZE(es325_off_on_texts),
			es325_off_on_texts);
static const struct soc_enum es325_bwe_enable_enum =
	SOC_ENUM_SINGLE(ES325_BWE_ENABLE, 0, ARRAY_SIZE(es325_off_on_texts),
			es325_off_on_texts);
static const struct soc_enum es325_bwe_post_eq_enable_enum =
	SOC_ENUM_SINGLE(ES325_BWE_POST_EQ_ENABLE, 0,
			ARRAY_SIZE(es325_off_on_texts),
			es325_off_on_texts);
static const struct soc_enum es325_algo_processing_enable_enum =
	SOC_ENUM_SINGLE(ES325_ALGO_PROCESSING, 0,
			ARRAY_SIZE(es325_off_on_texts),
			es325_off_on_texts);

static int es325_put_power_state_enum(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
	pr_info("GAC:%s():ucontrol = %d\n", __func__,
		ucontrol->value.enumerated.item[0]);
	pr_info("GAC:%s():power state= %d\n", __func__,
		es325_power_state);

	if (es325_power_state == ucontrol->value.enumerated.item[0]) {
		pr_info("GAC:%s():no power state change\n", __func__);
//		return 0;
	}
	if(!es325_fw_ready){
		pr_err("%s():ES325 Firmware is not ready\n", __func__);
		return ret;
	}

//	es325_power_state = ucontrol->value.enumerated.item[0];
	if (ucontrol->value.enumerated.item[0])
		ret = es325_wakeup(&es325_priv);
	else 
		es325_sleep_request(&es325_priv);

	return ret;
}

static int es325_get_power_state_enum(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	pr_info("GAC:%s(): power state = %d\n", __func__,
		es325_power_state);
	ucontrol->value.enumerated.item[0] = es325_power_state;
	pr_info("%s(): ucontrol = %d\n", __func__,
		ucontrol->value.enumerated.item[0]);

	return 0;
}

static const char * const es325_route_change_status_texts[] = {
	"Active", "Muting", "Switching", "Unmuting", "Inactive"
};
static const struct soc_enum es325_route_change_status_enum =
	SOC_ENUM_SINGLE(ES325_CHANGE_STATUS, 0,
			ARRAY_SIZE(es325_route_change_status_texts),
			es325_route_change_status_texts);

static const char * const es325_power_state_texts[] = {
	"Sleep", "Active"
};
static const struct soc_enum es325_power_state_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0,
			ARRAY_SIZE(es325_power_state_texts),
			es325_power_state_texts);

static struct snd_kcontrol_new es325_digital_ext_snd_controls[] = {
	/* commit controls */
	SOC_SINGLE_EXT("ES325 RX1 Enable", SND_SOC_NOPM, 0, 1, 0,
		       es325_get_rx1_route_enable_value,
		       es325_put_rx1_route_enable_value),
	SOC_SINGLE_EXT("ES325 TX1 Enable", SND_SOC_NOPM, 0, 1, 0,
		       es325_get_tx1_route_enable_value,
		       es325_put_tx1_route_enable_value),
	SOC_SINGLE_EXT("ES325 RX2 Enable", SND_SOC_NOPM, 0, 1, 0,
		       es325_get_rx2_route_enable_value,
		       es325_put_rx2_route_enable_value),
	SOC_ENUM_EXT("Mic Config", es325_mic_config_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_ENUM_EXT("AEC Mode", es325_aec_mode_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_ENUM_EXT("VEQ Enable", es325_veq_enable_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_ENUM_EXT("Dereverb Enable", es325_dereverb_enable_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_SINGLE_EXT("Dereverb Gain",
		       ES325_DEREVERB_GAIN, 0, 100, 0,
		       es325_get_dereverb_gain_value, es325_put_dereverb_gain_value),
	SOC_ENUM_EXT("BWE Enable", es325_bwe_enable_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_SINGLE_EXT("BWE High Band Gain",
		       ES325_BWE_HIGH_BAND_GAIN, 0, 100, 0,
		       es325_get_bwe_high_band_gain_value,
		       es325_put_bwe_high_band_gain_value),
	SOC_SINGLE_EXT("BWE Max SNR",
		       ES325_BWE_MAX_SNR, 0, 100, 0,
		       es325_get_bwe_max_snr_value, es325_put_bwe_max_snr_value),
	SOC_ENUM_EXT("BWE Post EQ Enable", es325_bwe_post_eq_enable_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_SINGLE_EXT("SLIMbus Link Multi Channel",
		       ES325_SLIMBUS_LINK_MULTI_CHANNEL, 0, 65535, 0,
		       es325_get_control_value, es325_put_control_value),
	SOC_ENUM_EXT("Set Power State", es325_power_state_enum,
		       es325_get_power_state_enum, es325_put_power_state_enum),
	SOC_ENUM_EXT("Algorithm Processing", es325_algo_processing_enable_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_ENUM_EXT("Algorithm Sample Rate", es325_algo_sample_rate_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_ENUM_EXT("Algorithm", es325_algorithms_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_ENUM_EXT("Mix Sample Rate", es325_algo_mix_rate_enum,
		     es325_get_control_enum, es325_put_control_enum),
	SOC_SINGLE_EXT("Internal Route Config",
		       SND_SOC_NOPM, 0, 100, 0, es325_get_internal_route_config,
		       es325_put_internal_route_config),
	SOC_SINGLE_EXT("Audio Custom Profile",
		       SND_SOC_NOPM, 0, 100, 0, es325_get_audio_custom_profile,
		       es325_put_audio_custom_profile),
	SOC_ENUM_EXT("ES325-AP Tx Channels", es325_ap_tx1_ch_cnt_enum,
		     es325_ap_get_tx1_ch_cnt, es325_ap_put_tx1_ch_cnt),
	SOC_SINGLE_EXT("ES325 Presets",SND_SOC_NOPM, 0, 65535, 0,
		       es325_preset_get_control_value, es325_preset_put_control_value),
	SOC_ENUM_EXT("ES325 Get Route Change Status", es325_route_change_status_enum,
		     es325_get_control_enum, es325_put_control_enum),

};

/*
static struct snd_kcontrol_new es325_digital_snd_controls[] = {
	\/\* commit controls \*\/
	SOC_SINGLE("Mic Config",
		   ES325_MIC_CONFIG, 0, 8, 0),
	SOC_SINGLE("AEC Mode",
		   ES325_AEC_MODE, 0, 6, 0),
	SOC_SINGLE("TX AGC",
		   ES325_TX_AGC, 0, 65535, 0),
	SOC_SINGLE("TX AGC Target Level",
		   ES325_TX_AGC_TARGET_LEVEL, 0, 65535, 0),
	SOC_SINGLE("TX AGC Noise Floor",
		   ES325_TX_AGC_NOISE_FLOOR, 0, 65535, 0),
	SOC_SINGLE("TX AGC SNR Improvement",
		   ES325_TX_AGC_SNR_IMPROVEMENT, 0, 4, 0),
	SOC_SINGLE("VEQ Enable",
		   ES325_VEQ_ENABLE, 0, 65535, 0),
	SOC_SINGLE("RX Out Limiter Max Level",
		   ES325_RX_OUT_LIMITER_MAX_LEVEL, 0, 65535, 0),
	SOC_SINGLE("RX Noise Suppress",
		   ES325_RX_NOISE_SUPPRESS, 0, 65535, 0),
	SOC_SINGLE("RX STS",
		   ES325_RX_STS, 0, 65535, 0),
	SOC_SINGLE("RX STS Rate",
		   ES325_RX_STS_RATE, 0, 65535, 0),
	SOC_SINGLE("AEC Speaker Volume",
		   ES325_AEC_SPEAKER_VOLUME, 0, 65535, 0),
	SOC_SINGLE("Sidetone",
		   ES325_SIDETONE, 0, 2, 0),
	SOC_SINGLE("Sidetone Gain",
		   ES325_SIDETONE_GAIN, 0, 65535, 0),
	SOC_SINGLE("TX Comfort Noise",
		   ES325_TX_COMFORT_NOISE, 0, 1, 0),
	SOC_SINGLE("TX Comfort Noise Level",
		   ES325_TX_COMFORT_NOISE_LEVEL, 0, 65535, 0),
	SOC_SINGLE("Algorithm Reset",
		   ES325_ALGORITHM_RESET, 0, 31, 0),
	SOC_SINGLE("RX Post EQ",
		   ES325_RX_POST_EQ, 0, 65535, 0),
	SOC_SINGLE("TX Post EQ",
		   ES325_TX_POST_EQ, 0, 65535, 0),
	SOC_SINGLE("AEC CNG",
		   ES325_AEC_CNG, 0, 1, 0),
	SOC_SINGLE("VEQ Nosie Est Adjustment",
		   ES325_VEQ_NOISE_ESTIMATION_ADJUSTMENT, 0, 65535, 0),
	SOC_SINGLE("TX AGC Slew Rate Up",
		   ES325_TX_AGC_SLEW_RATE_UP, 0, 65535, 0),
	SOC_SINGLE("TX AGC Slew Rate Down",
		   ES325_TX_AGC_SLEW_RATE_DOWN, 0, 65535, 0),
	SOC_SINGLE("RX AGC",
		   ES325_RX_AGC, 0, 65535, 0),
	SOC_SINGLE("RX AGC Target Level",
		   ES325_RX_AGC_TARGET_LEVEL, 0, 65535, 0),
	SOC_SINGLE("RX AGC Noise Floor",
		   ES325_RX_AGC_NOISE_FLOOR, 0, 65535, 0),
	SOC_SINGLE("RX AGC SNR Improvement",
		   ES325_RX_AGC_SNR_IMPROVEMENT, 0, 4, 0),
	SOC_SINGLE("RX AGC Slew Rate Up",
		   ES325_RX_AGC_SLEW_RATE_UP, 0, 65535, 0),
	SOC_SINGLE("RX AGC Slew Rate Down",
		   ES325_RX_AGC_SLEW_RATE_DOWN, 0, 65535, 0),
	SOC_SINGLE("AEC CNG Gain",
		   ES325_AEC_CNG_GAIN, 0, 65535, 0),
	SOC_SINGLE("TX MBC",
		   ES325_TX_MBC, 0, 2, 0),
	SOC_SINGLE("RX MBC",
		   ES325_RX_MBC, 0, 2, 0),
	SOC_SINGLE("AEC ESE",
		   ES325_AEC_ESE, 0, 65535, 0),
	SOC_SINGLE("TX NS Adaptation Speed",
		   ES325_TX_NS_ADAPTATION_SPEED, 0, 2, 0),
	SOC_SINGLE("TX SNR Est",
		   ES325_TX_SNR_ESTIMATE, 0, 1, 0),
	SOC_SINGLE("VEQ Max Gain",
		   ES325_VEQ_MAX_GAIN, 0, 15, 0),
	SOC_SINGLE("TX AGC Guardband",
		   ES325_TX_AGC_GUARDBAND, 0, 15, 0),
	SOC_SINGLE("RX AGC Guardband",
		   ES325_RX_AGC_GUARDBAND, 0, 15, 0),
	SOC_SINGLE("TX Out Limiter Max Level",
		   ES325_TX_OUT_LIMITER_MAX_LEVEL, 0, 65535, 0),
	SOC_SINGLE("TX In Limiter Max Level",
		   ES325_TX_IN_LIMITER_MAX_LEVEL, 0, 65535, 0),
	SOC_SINGLE("RX NS Adaptation Speed",
		   ES325_RX_NS_ADAPTATION_SPEED, 0, 2, 0),
	SOC_SINGLE("AEC Variable Echo Ref Delay",
		   ES325_AEC_VARIABLE_ECHO_REF_DELAY, 0, 65535, 0),
	SOC_SINGLE("TX Noise Suppress level",
		   ES325_TX_NOISE_SUPPRESS_LEVEL, 0, 65535, 0),
	SOC_SINGLE("RX Noise Suppress level",
		   ES325_RX_NOISE_SUPPRESS_LEVEL, 0, 65535, 0),
	SOC_SINGLE("RX CNG",
		   ES325_RX_CNG, 0, 1, 0),
	SOC_SINGLE("RX CNG Gain",
		   ES325_RX_CNG_GAIN, 0, 65535, 0),
	SOC_SINGLE("TX AGC Max Gain",
		   ES325_TX_AGC_MAX_GAIN, 0, 65535, 0),
	SOC_SINGLE("RX AGC Max Gain",
		   ES325_RX_AGC_MAX_GAIN, 0, 45, 0),
	SOC_SINGLE("Avalon API Version Lo",
		   ES325_AVALON_API_VERSION_LO, 0, 65535, 0),
	SOC_SINGLE("Avalon API Version Hi",
		   ES325_AVALON_API_VERSION_HI, 0, 65535, 0),
	SOC_SINGLE("Avalon Enable AV Processor",
		   ES325_AVALON_AV_PROCESSOR, 0, 1, 0),
	SOC_SINGLE("Avalon AV Config",
		   ES325_AVALON_AV_CONFIG, 0, 6, 0),
	SOC_SINGLE("Avalon EQ Preset",
		   ES325_AVALON_EQ_PRESET, 0, 14, 0),
	SOC_SINGLE("Avalon Stereo Width",
		   ES325_AVALON_STEREO_WIDTH, 0, 65535, 0),
	SOC_SINGLE("Avalon AV Digital Out Gain",
		   ES325_AVALON_AV_DIGITAL_OUT_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon Enable TdMBC",
		   ES325_AVALON_TDMBC, 0, 1, 0),
	SOC_SINGLE("Avalong AV Out Limit",
		   ES325_AVALON_AV_OUT_LIMIT, 0, 65535, 0),
	SOC_SINGLE("Avalon Enable Stereo Widening",
		   ES325_AVALON_STEREO_WIDENING, 0, 1, 0),
	SOC_SINGLE("Avalon Enable stat NS",
		   ES325_AVALON_STAT_NS, 0, 1, 0),
	SOC_SINGLE("Avalon Stat NS Suppress",
		   ES325_AVALON_STAT_NS_SUPPRESS, 0, 65535, 0),
	SOC_SINGLE("Avalon Stat NS Adaptation Speed",
		   ES325_AVALON_STAT_NS_ADAP_SPEED, 0, 3, 0),
	SOC_SINGLE("Avalon Stat NS Mode",
		   ES325_AVALON_STAT_NS_MODE, 0, 3, 0),
	SOC_SINGLE("Avalon Stat NS Max Noise Energy",
		   ES325_AVLALON_STAT_NS_MAX_NOISE_ENERGY, 0, 65535, 0),
	SOC_SINGLE("Avalon Enable VBB",
		   ES325_AVALON_VBB, 0, 1, 0),
	SOC_SINGLE("Avalon VBB Strength",
		   ES325_AVALON_VBB_STRENGTH, 0, 100, 0),
	SOC_SINGLE("Avalon EQ Mode",
		   ES325_AVALON_EQ_MODE, 0, 2, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 1 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND1_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 2 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND2_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 3 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND3_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 4 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND4_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 5 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND5_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 6 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND6_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 7 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND7_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 8 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND8_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 9 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND9_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon EQ Graphic Band 10 Gain",
		   ES325_AVALON_EQ_GRAPHIC_BAND10_GAIN, 0, 65535, 0),
	SOC_SINGLE("Avalon TdDRC Enable",
		   ES325_AVALON_TDDRC, 0, 1, 0),
	SOC_SINGLE("Avalon TdDRC Strength",
		   ES325_AVALON_TDDRC_STRENGTH, 0, 100, 0),
	SOC_SINGLE("Avalon Enable Limiter",
		   ES325_AVALON_LIMITER, 0, 1, 0),
	SOC_SINGLE("Avalon Enable EQ",
		   ES325_AVALON_EQ, 0, 1, 0),
	SOC_SINGLE("DirAC Enable",
		   ES325_DIRAC, 0, 1, 0),
	SOC_SINGLE("DirAC Out Headroom Limiter",
		   ES325_DIRAC_OUT_HEADROOM_LIMITER, 0, 65535, 0),
	SOC_SINGLE("DirAC Mode",
		   ES325_DIRAC_MODE, 0, 2, 0),
	SOC_SINGLE("DirAC In Headroom Limiter",
		   ES325_DIRAC_IN_HEADROOM_LIMITER, 0, 65535, 0),
	SOC_SINGLE("DirAC Comfort Noise ",
		   ES325_DIRAC_COMFORT_NOISE, 0, 1, 0),
	SOC_SINGLE("DirAC Comfort Noise level",
		   ES325_DIRAC_COMFORT_NOISE_LEVEL, 0, 65535, 0),
	SOC_SINGLE("DirAC Narrator VQOS",
		   ES325_DIRAC_NARRATOR_VQOS, 0, 15, 0),
	SOC_SINGLE("DirAC Narrator Position Suppress",
		   ES325_DIRAC_NARRATOR_POSITION_SUPPRESS, 0, 7, 0),
	SOC_SINGLE("DirAC Narrator Use AGC Out",
		   ES325_DIRAC_NARRATOR_AGC_OUT, 0, 1, 0),
	SOC_SINGLE("DirAC Narrator AGC Speech Target",
		   ES325_DIRAC_NARRATOR_AGC_SPEECH_TARGET, 0, 65535, 0),
	SOC_SINGLE("DirAC Narrator AGC SNR Improve",
		   ES325_DIRAC_NARRATOR_AGC_SNR_IMPROVE, 0, 4, 0),
	SOC_SINGLE("DirAC Narrator AGC Noise Floor",
		   ES325_DIRAC_NARRATOR_AGC_NOISE_FLOOR, 0, 65535, 0),
	SOC_SINGLE("DirAC Narrator AGC Max Gain",
		   ES325_DIRAC_NARRATOR_AGC_MAX_GAIN, 0, 45, 0),
	SOC_SINGLE("DirAC Narrator AGC Up Rate",
		   ES325_DIRAC_NARRATOR_AGC_UP_RATE, 0, 50, 0),
	SOC_SINGLE("DirAC Narrator AGC Down Rate",
		   ES325_DIRAC_NARRATOR_AGC_DOWN_RATE, 0, 50, 0),
	SOC_SINGLE("DirAC Narrator AGC Guardband",
		   ES325_DIRAC_NARRATOR_AGC_GUARDBAND, 0, 15, 0),
	SOC_SINGLE("DirAC Narrator Post EQ Mode",
		   ES325_DIRAC_NARRATOR_POST_EQ_MODE, 0, 1, 0),
	SOC_SINGLE("DirAC Narrator MBC Mode",
		   ES325_DIRAC_NARRATOR_MBC_MODE, 0, 1, 0),
	SOC_SINGLE("DirAC Scene Beam Width Deg",
		   ES325_DIRAC_SCENE_BEAM_WIDTH, 0, 90, 0),
	SOC_SINGLE("DirAC Scene Use AGC Out",
		   ES325_DIRAC_SCENE_AGC_OUT, 0, 1, 0),
	SOC_SINGLE("DirAC Scene AGC Speech Target",
		   ES325_DIRAC_SCENE_AGC_SPEECH_TARGET, 0, 65535, 0),
	SOC_SINGLE("DirAC Scene AGC SNR Improve",
		   ES325_DIRAC_SCENE_AGC_SNR_IMPROVE, 0, 4, 0),
	SOC_SINGLE("DirAC Scene AGC Noise Floor",
		   ES325_DIRAC_SCENE_AGC_NOISE_FLOOR, 0, 65535, 0),
	SOC_SINGLE("DirAC Scene AGC Max Gain",
		   ES325_DIRAC_SCENE_AGC_MAX_GAIN, 0, 45, 0),
	SOC_SINGLE("DirAC Scene AGC Up Rate",
		   ES325_DIRAC_SCENE_AGC_UP_RATE, 0, 50, 0),
	SOC_SINGLE("DirAC Scene AGC Down Rate",
		   ES325_DIRAC_SCENE_AGC_DOWN_RATE, 0, 50, 0),
	SOC_SINGLE("DirAC Scene AGC Guardband",
		   ES325_DIRAC_SCENE_AGC_GUARDBAND, 0, 15, 0),
	SOC_SINGLE("DirAC Scene VQOS",
		   ES325_DIRAC_SCENE_VQOS, 0, 15, 0),
	SOC_SINGLE("DirACC Scene Post EQ Mode",
		   ES325_DIRAC_SCENE_POST_EQ_MODE, 0, 1, 0),
	SOC_SINGLE("DirAC Scene MBC Mode",
		   ES325_DIRAC_SCENE_MBC_MODE, 0, 1, 0),
	SOC_SINGLE("Tone Param API Version Lo",
		   ES325_TONE_PARAM_API_VERSION_LO, 0, 65535, 0),
	SOC_SINGLE("Tone Param API Version Hi",
		   ES325_TONE_PARAM_API_VERSION_HI, 0, 65535, 0),
	SOC_SINGLE("Tone Param Enable Beep Sys",
		   ES325_TONE_PARAM_ENABLE_BEEP_SYS, 0, 1, 0),
	SOC_SINGLE("Tone Param Enable Gen Beep",
		   ES325_TONE_PARAM_ENABLE_GEN_BEEP, 0, 1, 0),
	SOC_SINGLE("Tone Param Gen Beep On",
		   ES325_TONE_PARAM_GEN_BEEP_ON, 0, 1, 0),
	SOC_SINGLE("Tone Param Gen Beep Freq1",
		   ES325_TONE_PARAM_GEN_BEEP_FREQ1, 0, 20000, 0),
	SOC_SINGLE("Tone Param Gen Beep Freq2",
		   ES325_TONE_PARAM_GEN_BEEP_FREQ2, 0, 20000, 0),
	SOC_SINGLE("Tone Param Gen Beep Pan LR",
		   ES325_TONE_PARAM_GEN_BEEP_PAN_LR, 0, 65535, 0),
	SOC_SINGLE("Tone Param Gen Beep Gain",
		   ES325_TONE_PARAM_GEN_BEEP_GAIN, 0, 65535, 0),
	SOC_SINGLE("Dereverb Enable",
		   ES325_DEREVERB_ENABLE, 0, 1, 0),
	SOC_SINGLE("Dereverb Gain",
		   ES325_DEREVERB_GAIN, 0, 65535, 0),
	SOC_SINGLE("BWE Enable",
		   ES325_BWE_ENABLE, 0, 1, 0),
	SOC_SINGLE("BWE High Band Gain",
		   ES325_BWE_HIGH_BAND_GAIN, 0, 65535, 0),
	SOC_SINGLE("BWE Max SNR",
		   ES325_BWE_MAX_SNR, 0, 65535, 0),
	SOC_SINGLE("BWE Post EQ Enable",
		   ES325_BWE_MAX_SNR, 0, 1, 0),
	SOC_SINGLE("PortA Word Length",
		   ES325_PORTA_WORD_LEN, 0, 31, 0),
	SOC_SINGLE("PortA TDM Slots Per Frame",
		   ES325_PORTA_TDM_SLOTS_PER_FRAME, 0, 7, 0),
	SOC_SINGLE("PortA Delay From FS TX",
		   ES325_PORTA_TX_DELAY_FROM_FS, 0, 31, 0),
	SOC_SINGLE("PortA Delay From FS RX",
		   ES325_PORTA_RX_DELAY_FROM_FS, 0, 31, 0),
	SOC_SINGLE("PortA Latch Edge",
		   ES325_PORTA_LATCH_EDGE, 0, 3, 0),
	SOC_SINGLE("PortA Endian",
		   ES325_PORTA_ENDIAN, 0, 1, 0),
	SOC_SINGLE("PortA Tristate Enable",
		   ES325_PORTA_TRISTATE, 0, 1, 0),
	SOC_SINGLE("PortA Audio Port Mode",
		   ES325_PORTA_AUDIO_PORT_MODE, 0, 1, 0),
	SOC_SINGLE("PortA TDM Enabled",
		   ES325_PORTA_TDM_ENABLED, 0, 1, 0),
	SOC_SINGLE("PortA Clock Control",
		   ES325_PORTA_CLOCK_CONTROL, 0, 12736, 0),
	SOC_SINGLE("PortA Data Justification",
		   ES325_PORTA_DATA_JUSTIFICATION, 0, 1, 0),
	SOC_SINGLE("PortA FS Duration",
		   ES325_PORTA_FS_DURATION, 0, 255, 0),
	SOC_SINGLE("PortB Word Length",
		   ES325_PORTB_WORD_LEN, 0, 31, 0),
	SOC_SINGLE("PortB TDM Slots Per Frame",
		   ES325_PORTB_TDM_SLOTS_PER_FRAME, 0, 7, 0),
	SOC_SINGLE("PortB Delay From FS TX",
		   ES325_PORTB_TX_DELAY_FROM_FS, 0, 31, 0),
	SOC_SINGLE("PortB Delay From FS RX",
		   ES325_PORTB_RX_DELAY_FROM_FS, 0, 31, 0),
	SOC_SINGLE("PortB Latch Edge",
		   ES325_PORTB_LATCH_EDGE, 0, 3, 0),
	SOC_SINGLE("PortB Endian",
		   ES325_PORTB_ENDIAN, 0, 1, 0),
	SOC_SINGLE("PortB Tristate Enable",
		   ES325_PORTB_TRISTATE, 0, 1, 0),
	SOC_SINGLE("PortB Audio Port Mode",
		   ES325_PORTB_AUDIO_PORT_MODE, 0, 1, 0),
	SOC_SINGLE("PortB TDM Enabled",
		   ES325_PORTB_TDM_ENABLED, 0, 1, 0),
	SOC_SINGLE("PortB Clock Control",
		   ES325_PORTB_CLOCK_CONTROL, 0, 12736, 0),
	SOC_SINGLE("PortB Data Justification",
		   ES325_PORTB_DATA_JUSTIFICATION, 0, 1, 0),
	SOC_SINGLE("PortB FS Duration",
		   ES325_PORTB_FS_DURATION, 0, 255, 0),
	SOC_SINGLE("PortC Word Length",
		   ES325_PORTC_WORD_LEN, 0, 31, 0),
	SOC_SINGLE("PortC TDM Slots Per Frame",
		   ES325_PORTC_TDM_SLOTS_PER_FRAME, 0, 7, 0),
	SOC_SINGLE("PortC Delay From FS TX",
		   ES325_PORTC_TX_DELAY_FROM_FS, 0, 31, 0),
	SOC_SINGLE("PortC Delay From FS RX",
		   ES325_PORTC_RX_DELAY_FROM_FS, 0, 31, 0),
	SOC_SINGLE("PortC Latch Edge",
		   ES325_PORTC_LATCH_EDGE, 0, 3, 0),
	SOC_SINGLE("PortC Endian",
		   ES325_PORTC_ENDIAN, 0, 1, 0),
	SOC_SINGLE("PortC Tristate Enable",
		   ES325_PORTC_TRISTATE, 0, 1, 0),
	SOC_SINGLE("PortC Audio Port Mode",
		   ES325_PORTC_AUDIO_PORT_MODE, 0, 1, 0),
	SOC_SINGLE("PortC TDM Enabled",
		   ES325_PORTC_TDM_ENABLED, 0, 1, 0),
	SOC_SINGLE("PortC Clock Control",
		   ES325_PORTC_CLOCK_CONTROL, 0, 12736, 0),
	SOC_SINGLE("PortC Data Justification",
		   ES325_PORTC_DATA_JUSTIFICATION, 0, 1, 0),
	SOC_SINGLE("PortC FS Duration",
		   ES325_PORTC_FS_DURATION, 0, 255, 0),
	SOC_SINGLE("PortD Word Length",
		   ES325_PORTD_WORD_LEN, 0, 31, 0),
	SOC_SINGLE("PortD TDM Slots Per Frame",
		   ES325_PORTD_TDM_SLOTS_PER_FRAME, 0, 7, 0),
	SOC_SINGLE("PortD Delay From FS TX",
		   ES325_PORTD_TX_DELAY_FROM_FS, 0, 31, 0),
	SOC_SINGLE("PortD Delay From FS RX",
		   ES325_PORTD_RX_DELAY_FROM_FS, 0, 31, 0),
	SOC_SINGLE("PortD Latch Edge",
		   ES325_PORTD_LATCH_EDGE, 0, 3, 0),
	SOC_SINGLE("PortD Endian",
		   ES325_PORTD_ENDIAN, 0, 1, 0),
	SOC_SINGLE("PortD Tristate Enable",
		   ES325_PORTD_TRISTATE, 0, 1, 0),
	SOC_SINGLE("PortD Audio Port Mode",
		   ES325_PORTD_AUDIO_PORT_MODE, 0, 1, 0),
	SOC_SINGLE("PortD TDM Enabled",
		   ES325_PORTD_TDM_ENABLED, 0, 1, 0),
	SOC_SINGLE("PortD Clock Control",
		   ES325_PORTD_CLOCK_CONTROL, 0, 12736, 0),
	SOC_SINGLE("PortD Data Justification",
		   ES325_PORTD_DATA_JUSTIFICATION, 0, 1, 0),
	SOC_SINGLE("PortD FS Duration",
		   ES325_PORTD_FS_DURATION, 0, 255, 0),
	SOC_SINGLE("SLIMbus Link Multi Channel",
		   ES325_SLIMBUS_LINK_MULTI_CHANNEL, 0, 65535, 0),
	\/\* SOC_SINGLE("Set Power State", \*\/
	\/\*	ES325_POWER_STATE, 0, 1, 0), \*\/
	SOC_SINGLE("Streaming",
		   ES325_STREAMING, 0, 5, 0),
	SOC_SINGLE("Far End Streaming",
		   ES325_FE_STREAMING, 0, 8, 0),
	\/\* SOC_SINGLE("Preset", \*\/
	\/\*	ES325_PRESET, 0, 15, 0), \*\/
	SOC_SINGLE("Algorithm Statistics",
		   ES325_ALGO_STATS, 0, 15, 0),
	SOC_SINGLE("Algorithm Processing",
		   ES325_ALGO_PROCESSING, 0, 1, 0),
	SOC_SINGLE("Algorithm Sample Rate",
		   ES325_ALGO_SAMPLE_RATE, 0, 5, 0),
	SOC_SINGLE("Smooth Rate",
		   ES325_SMOOTH_RATE, 0, 65535, 0),
	SOC_SINGLE("Change Status",
		   ES325_CHANGE_STATUS, 0, 4, 0),
	SOC_SINGLE("Digital Pass Through",
		   ES325_DIGITAL_PASS_THROUGH, 0, 65535, 0),
	SOC_SINGLE("Data Path",
		   ES325_DATA_PATH, 0, 65535, 0),
	SOC_SINGLE("Algorithm",
		   ES325_ALGORITHM, 0, 4, 0),
	SOC_SINGLE("Mix Sample Rate",
		   ES325_MIX_SAMPLE_RATE, 0, 5, 0),
	SOC_SINGLE("Sig RMS PortA DIN Left",
		   ES325_SIGNAL_RMS_PORTA_DIN_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortA DIN Right",
		   ES325_SIGNAL_RMS_PORTA_DIN_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortA DOUT Left",
		   ES325_SIGNAL_RMS_PORTA_DOUT_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortA DOUT Right",
		   ES325_SIGNAL_RMS_PORTA_DOUT_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortB DIN Left",
		   ES325_SIGNAL_RMS_PORTB_DIN_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortB DIN Right",
		   ES325_SIGNAL_RMS_PORTB_DIN_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortB DOUT Left",
		   ES325_SIGNAL_RMS_PORTB_DOUT_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortB DOUT Right",
		   ES325_SIGNAL_RMS_PORTB_DOUT_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortC DIN Left",
		   ES325_SIGNAL_RMS_PORTC_DIN_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortC DIN Right",
		   ES325_SIGNAL_RMS_PORTC_DIN_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortC DOUT Left",
		   ES325_SIGNAL_RMS_PORTC_DOUT_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortC DOUT Right",
		   ES325_SIGNAL_RMS_PORTC_DOUT_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortD DIN Left",
		   ES325_SIGNAL_RMS_PORTD_DIN_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortD DIN Right",
		   ES325_SIGNAL_RMS_PORTD_DIN_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortD DOUT Left",
		   ES325_SIGNAL_RMS_PORTD_DOUT_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig RMS PortD DOUT Right",
		   ES325_SIGNAL_RMS_PORTD_DOUT_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortA DIN Left",
		   ES325_SIGNAL_PEAK_PORTA_DIN_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortA DIN Right",
		   ES325_SIGNAL_PEAK_PORTA_DIN_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortA DOUT Left",
		   ES325_SIGNAL_PEAK_PORTA_DOUT_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortA DOUT Right",
		   ES325_SIGNAL_PEAK_PORTA_DOUT_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortB DIN Left",
		   ES325_SIGNAL_PEAK_PORTB_DIN_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortB DIN Right",
		   ES325_SIGNAL_PEAK_PORTB_DIN_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortB DOUT Left",
		   ES325_SIGNAL_PEAK_PORTB_DOUT_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortB DOUT Right",
		   ES325_SIGNAL_PEAK_PORTB_DOUT_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortC DIN Left",
		   ES325_SIGNAL_PEAK_PORTC_DIN_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortC DIN Right",
		   ES325_SIGNAL_PEAK_PORTC_DIN_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortC DOUT Left",
		   ES325_SIGNAL_PEAK_PORTC_DOUT_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortC DOUT Right",
		   ES325_SIGNAL_PEAK_PORTC_DOUT_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortD DIN Left",
		   ES325_SIGNAL_PEAK_PORTD_DIN_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortD DIN Right",
		   ES325_SIGNAL_PEAK_PORTD_DIN_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortD DOUT Left",
		   ES325_SIGNAL_PEAK_PORTD_DOUT_LEFT, 0, 65535, 0),
	SOC_SINGLE("Sig Peak PortD DOUT Right",
		   ES325_SIGNAL_PEAK_PORTD_DOUT_RIGHT, 0, 65535, 0),
	SOC_SINGLE("Digital Gain Primary Volume",
		   ES325_DIGITAL_GAIN_PRIMARY, 0, 255, 0),
	SOC_SINGLE("Digital Gain Secondary Volume",
		   ES325_DIGITAL_GAIN_SECONDARY, 0, 255, 0),
	SOC_SINGLE("Digital Gain Tertiary Volume",
		   ES325_DIGITAL_GAIN_TERTIARY, 0, 255, 0),
	SOC_SINGLE("Digital Gain Quad Volume",
		   ES325_DIGITAL_GAIN_QUAD, 0, 255, 0),
	SOC_SINGLE("Digital Gain FEIN Volume",
		   ES325_DIGITAL_GAIN_FEIN, 0, 255, 0),
	SOC_SINGLE("Digital Gain AUDIN1 Volume",
		   ES325_DIGITAL_GAIN_AUDIN1, 0, 255, 0),
	SOC_SINGLE("Digital Gain AUDIN2 Volume",
		   ES325_DIGITAL_GAIN_AUDIN2, 0, 255, 0),
	SOC_SINGLE("Digital Gain AUDIN3 Volume",
		   ES325_DIGITAL_GAIN_AUDIN3, 0, 255, 0),
	SOC_SINGLE("Digital Gain AUDIN4 Volume",
		   ES325_DIGITAL_GAIN_AUDIN4, 0, 255, 0),
	SOC_SINGLE("Digital Gain UITONE1 Volume",
		   ES325_DIGITAL_GAIN_UITONE1, 0, 255, 0),
	SOC_SINGLE("Digital Gain UITONE2 Volume",
		   ES325_DIGITAL_GAIN_UITONE2, 0, 255, 0),
	SOC_SINGLE("Digital Gain CSOUT Volume",
		   ES325_DIGITAL_GAIN_CSOUT, 0, 255, 0),
	SOC_SINGLE("Digital Gain FEOUT1 Volume",
		   ES325_DIGITAL_GAIN_FEOUT1, 0, 255, 0),
	SOC_SINGLE("Digital Gain FEOUT2 Volume",
		   ES325_DIGITAL_GAIN_FEOUT2, 0, 255, 0),
	SOC_SINGLE("Digital Gain AUDOUT1 Volume",
		   ES325_DIGITAL_GAIN_AUDOUT1, 0, 255, 0),
	SOC_SINGLE("Digital Gain AUDOUT2 Volume",
		   ES325_DIGITAL_GAIN_AUDOUT2, 0, 255, 0),
	SOC_SINGLE("Digital Gain AUDOUT3 Volume",
		   ES325_DIGITAL_GAIN_AUDOUT3, 0, 255, 0),
	SOC_SINGLE("Digital Gain AUDOUT4 Volume",
		   ES325_DIGITAL_GAIN_AUDOUT4, 0, 255, 0),
	SOC_SINGLE("PortA timing",
		   ES325_PORTA_TIMING, 0, 15, 0),
	SOC_SINGLE("PortB timing",
		   ES325_PORTB_TIMING, 0, 15, 0),
	SOC_SINGLE("PortC timing",
		   ES325_PORTC_TIMING, 0, 15, 0),
	SOC_SINGLE("PortD timing",
		   ES325_PORTD_TIMING, 0, 15, 0),
	SOC_SINGLE("Flush",
		   ES325_FLUSH, 0, 0, 0),

	SOC_SINGLE_EXT("Internal Route Config",
		       SND_SOC_NOPM, 0, 0, 6, es325_put_internal_route_config,
		       es325_get_internal_route_config)
};
*/

static int es325_set_bias_level(struct snd_soc_codec *codec,
				enum snd_soc_bias_level level)
{
	int ret = 0;

	/* dev_dbg(codec->dev, "%s(codec, level = 0x%04x): entry\n", __func__, level); */

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}
	codec->dapm.bias_level = level;

	/* dev_dbg(codec->dev, "%s(): exit\n", __func__); */
	return ret;
}

#if defined(CONFIG_SND_SOC_ES325_I2S)
static int es325_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_set_pll(struct snd_soc_dai *dai, int pll_id,
			     int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_set_clkdiv(struct snd_soc_dai *dai, int div_id,
				int div)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
				  unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_set_channel_map(struct snd_soc_dai *dai, unsigned int tx_num,
				     unsigned int *tx_slot, unsigned int rx_num, unsigned int *rx_slot)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int paramid = 0;
	unsigned int val = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	switch (dai->id) {
	case 0:
		break;
	case 1:
		break;
	default:
		return -EINVAL;
	}

	if (tristate)
		val = 0x0001;
	else
		val = 0x0000;

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return snd_soc_write(codec, paramid, val);
}

static int es325_i2s_port_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int paramid = 0;
	unsigned int val = 0;

	/* Is this valid since DACs are not statically mapped to DAIs? */
	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);
	switch (dai->id) {
	case 0:
		break;
	case 1:
		break;
	default:
		return -EINVAL;
	}

	if (mute)
		val = 0x0000;
	else
		val = 0x0001;

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return snd_soc_write(codec, paramid, val);
}

static int es325_i2s_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);


	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static void es325_i2s_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
}

static int es325_i2s_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	/* struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec); */
	int bits_per_sample = 0;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);
	switch (dai->id) {
	case 0:
		dev_dbg(codec->dev, "%s(): ES325_PORTA_PARAMID\n", __func__);
		break;
	case 1:
		dev_dbg(codec->dev, "%s(): ES325_PORTB_PARAMID\n", __func__);
		break;
	default:
		dev_dbg(codec->dev, "%s(): unknown port\n", __func__);
		dev_dbg(codec->dev, "%s(): exit\n", __func__);
		return -EINVAL;
	}

	dev_dbg(codec->dev, "%s(): params_channels(params) = %d\n", __func__,
		params_channels(params));
	switch (params_channels(params)) {
	case 1:
		dev_dbg(codec->dev, "%s(): 1 channel\n", __func__);
		break;
	case 2:
		dev_dbg(codec->dev, "%s(): 2 channels\n", __func__);
		break;
	case 4:
		dev_dbg(codec->dev, "%s(): 4 channels\n", __func__);
		break;
	default:
		dev_dbg(codec->dev, "%s(): unsupported number of channels\n",
			__func__);
		dev_dbg(codec->dev, "%s(): exit\n", __func__);
		return -EINVAL;
	}

	dev_dbg(codec->dev, "%s(): params_rate(params) = %d\n", __func__,
		params_rate(params));
	switch (params_rate(params)) {
	case 8000:
		dev_dbg(codec->dev, "%s(): 8000Hz\n", __func__);
		break;
	case 11025:
		dev_dbg(codec->dev, "%s(): 11025\n", __func__);
		break;
	case 16000:
		dev_dbg(codec->dev, "%s(): 16000\n", __func__);
		break;
	case 22050:
		dev_dbg(codec->dev, "%s(): 22050\n", __func__);
		break;
	case 32000:
		dev_dbg(codec->dev, "%s(): 32000\n", __func__);
		break;
	case 48000:
		dev_dbg(codec->dev, "%s(): 48000\n", __func__);
		break;
	case 96000:
		dev_dbg(codec->dev, "%s(): 96000\n", __func__);
		break;
	case 192000:
		dev_dbg(codec->dev, "%s(): 96000\n", __func__);
		break;
	default:
		dev_dbg(codec->dev, "%s(): unsupported rate = %d\n", __func__,
			params_rate(params));
		dev_dbg(codec->dev, "%s(): exit\n", __func__);
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dev_dbg(codec->dev, "%s(): S16_LE\n", __func__);
		bits_per_sample = 16;
		break;
	case SNDRV_PCM_FORMAT_S16_BE:
		dev_dbg(codec->dev, "%s(): S16_BE\n", __func__);
		bits_per_sample = 16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		dev_dbg(codec->dev, "%s(): S20_3LE\n", __func__);
		bits_per_sample = 20;
		break;
	case SNDRV_PCM_FORMAT_S20_3BE:
		dev_dbg(codec->dev, "%s(): S20_3BE\n", __func__);
		bits_per_sample = 20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dev_dbg(codec->dev, "%s(): S24_LE\n", __func__);
		bits_per_sample = 24;
		break;
	case SNDRV_PCM_FORMAT_S24_BE:
		dev_dbg(codec->dev, "%s(): S24_BE\n", __func__);
		bits_per_sample = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dev_dbg(codec->dev, "%s(): S32_LE\n", __func__);
		bits_per_sample = 32;
		break;
	case SNDRV_PCM_FORMAT_S32_BE:
		dev_dbg(codec->dev, "%s(): S32_BE\n", __func__);
		bits_per_sample = 32;
		break;
	default:
		dev_dbg(codec->dev, "%s(): unknown format\n", __func__);
		dev_dbg(codec->dev, "%s(): exit\n", __func__);
		return -EINVAL;
	}
	if (ret) {
		dev_dbg(codec->dev, "%s(): snd_soc_update_bits() failed\n",
			__func__);
		dev_dbg(codec->dev, "%s(): exit\n", __func__);
		return ret;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dev_dbg(codec->dev, "%s(): PLAYBACK\n", __func__);
	else
		dev_dbg(codec->dev, "%s(): CAPTURE\n", __func__);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_hw_free(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dev_dbg(codec->dev, "%s(): PLAYBACK\n", __func__);
	else
		dev_dbg(codec->dev, "%s(): CAPTURE\n", __func__);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_prepare(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}

static int es325_i2s_trigger(struct snd_pcm_substream *substream,
			     int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int ret;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);
	dev_dbg(codec->dev, "%s(): cmd = %d\n", __func__, cmd);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
}
#endif

static int es325_slim_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	/*
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
	*/
	return 0;
}

int es325_slim_set_channel_map(struct snd_soc_dai *dai, unsigned int tx_num,
			       unsigned int *tx_slot, unsigned int rx_num, unsigned int *rx_slot)
{
	struct snd_soc_codec *codec = dai->codec;
	/* local codec access */
	/* struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec); */
	/* remote codec access */
	struct es325_priv *es325 = &es325_priv;
	int id = dai->id;
	int i;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	if (id == ES325_SLIM_1_PB ||
	    id == ES325_SLIM_2_PB ||
	    id == ES325_SLIM_3_PB) {
		es325->dai[DAI_INDEX(id)].ch_tot = rx_num;
		es325->dai[DAI_INDEX(id)].ch_act = 0;
		for (i = 0; i < rx_num; i++)
			es325->dai[DAI_INDEX(id)].ch_num[i] = rx_slot[i];
	} else if (id == ES325_SLIM_1_CAP ||
		 id == ES325_SLIM_2_CAP ||
		 id == ES325_SLIM_3_CAP) {
		es325->dai[DAI_INDEX(id)].ch_tot = tx_num;
		es325->dai[DAI_INDEX(id)].ch_act = 0;
		pr_info("GAC:%s() id = %d\n", __func__, id);
		pr_info("GAC:%s() ch_tot = %d\n", __func__, tx_num);
		for (i = 0; i < tx_num; i++) {
			es325->dai[DAI_INDEX(id)].ch_num[i] = tx_slot[i];
			pr_info("GAC:%s() tx_slot[] = %d\n", __func__, tx_slot[i]);
		}
	}

	/* dev_dbg(codec->dev, "%s(): exit\n", __func__); */
	return ret;
}
EXPORT_SYMBOL_GPL(es325_slim_set_channel_map);

int es325_slim_get_channel_map(struct snd_soc_dai *dai,
			       unsigned int *tx_num, unsigned int *tx_slot,
			       unsigned int *rx_num, unsigned int *rx_slot)
{
	struct snd_soc_codec *codec = dai->codec;
	/* local codec access */
	/* struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec); */
	/* remote codec access */
	struct es325_priv *es325 = &es325_priv;
	struct es325_slim_ch *rx = es325->slim_rx;
	struct es325_slim_ch *tx = es325->slim_tx;
	int id = dai->id;
	int i;
	int ret = 0;

	dev_dbg(codec->dev, "GAC:%s(): dai->id = %d\n", __func__, dai->id);

	if (id == ES325_SLIM_1_PB) {
		*rx_num = es325_dai[DAI_INDEX(id)].playback.channels_max;
		dev_info(codec->dev, "GAC:%s(): *rx_num = %d\n", __func__, *rx_num);
		for (i = 0; i < *rx_num; i++) {
			rx_slot[i] = rx[ES325_SLIM_1_PB_OFFSET + i].ch_num;
			dev_info(codec->dev, "GAC:%s(): rx_slot[] = %d\n", __func__, rx_slot[i]);
		}
	} else if (id == ES325_SLIM_2_PB) {
		*rx_num = es325_dai[DAI_INDEX(id)].playback.channels_max;
		dev_info(codec->dev, "GAC:%s(): *rx_num = %d\n", __func__, *rx_num);
		for (i = 0; i < *rx_num; i++) {
			rx_slot[i] = rx[ES325_SLIM_2_PB_OFFSET + i].ch_num;
			dev_info(codec->dev, "GAC:%s(): rx_slot[] = %d\n", __func__, rx_slot[i]);
		}
	} else if (id == ES325_SLIM_3_PB) {
		*rx_num = es325_dai[DAI_INDEX(id)].playback.channels_max;
		dev_info(codec->dev, "GAC:%s(): *rx_num = %d\n", __func__, *rx_num);
		for (i = 0; i < *rx_num; i++) {
			rx_slot[i] = rx[ES325_SLIM_3_PB_OFFSET + i].ch_num;
			dev_info(codec->dev, "GAC:%s(): rx_slot[] = %d\n", __func__, rx_slot[i]);
		}
	} else if (id == ES325_SLIM_1_CAP) {
		*tx_num = es325_dai[DAI_INDEX(id)].capture.channels_max;
		dev_info(codec->dev, "GAC:%s(): *tx_num = %d\n", __func__, *tx_num);
		for (i = 0; i < *tx_num; i++) {
			tx_slot[i] = tx[ES325_SLIM_1_CAP_OFFSET + i].ch_num;
			dev_info(codec->dev, "GAC:%s(): tx_slot[] = %d\n", __func__, tx_slot[i]);
		}
	} else if (id == ES325_SLIM_2_CAP) {
		*tx_num = es325_dai[DAI_INDEX(id)].capture.channels_max;
		dev_info(codec->dev, "GAC:%s(): *tx_num = %d\n", __func__, *tx_num);
		for (i = 0; i < *tx_num; i++) {
			tx_slot[i] = tx[ES325_SLIM_2_CAP_OFFSET + i].ch_num;
			dev_info(codec->dev, "GAC:%s(): tx_slot[] = %d\n", __func__, tx_slot[i]);
		}
	} else if (id == ES325_SLIM_3_CAP) {
		*tx_num = es325_dai[DAI_INDEX(id)].capture.channels_max;
		for (i = 0; i < *tx_num; i++) {
			tx_slot[i] = tx[ES325_SLIM_3_CAP_OFFSET + i].ch_num;
			dev_info(codec->dev, "GAC:%s(): tx_slot[] = %d\n", __func__, tx_slot[i]);
		}
	}

	/* dev_dbg(codec->dev, "%s(): exit\n", __func__); */
	return ret;
}
EXPORT_SYMBOL_GPL(es325_slim_get_channel_map);

static int es325_slim_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	/*
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
	*/
	return 0;
}

static int es325_slim_port_mute(struct snd_soc_dai *dai, int mute)
{
	/*
	struct snd_soc_codec *codec = dai->codec;
	int ret = 0;

	\/\* Is this valid since DACs are not statically mapped to DAIs? \*\/
	dev_dbg(codec->dev, "%s(): entry\n", __func__);
	dev_dbg(codec->dev, "%s(): dai->id = %d\n", __func__, dai->id);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
	*/
	return 0;
}

static int es325_slim_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	/*
	struct snd_soc_codec *codec = dai->codec;
	struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
	*/
	return 0;
}

static void es325_slim_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	/*
	struct snd_soc_codec *codec = dai->codec;
	struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s(): entry\n", __func__);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	*/
}

int es325_slim_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params,
			 struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	/* local codec access */
	/* struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec); */
	/* remote codec access */
	struct es325_priv *es325 = &es325_priv;
	int id = dai->id;
	int channels;
	int rate;
	int ret = 0;

	dev_info(codec->dev, "%s(): entry\n", __func__);
	dev_info(codec->dev, "GAC:%s(): stream_name playback= %s, capture=%s \n", __func__,
		 es325_dai[DAI_INDEX(id)].playback.stream_name, es325_dai[DAI_INDEX(id)].capture.stream_name);
	dev_info(codec->dev, "GAC:%s(): id = %d\n", __func__,
		 es325_dai[DAI_INDEX(id)].id);

	channels = params_channels(params);
	switch (channels) {
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
		es325->dai[DAI_INDEX(id)].ch_tot = channels;
		break;
	default:
		dev_err(codec->dev,
			"%s(): unsupported number of channels, %d\n",
			__func__, channels);
		return -EINVAL;
	}
	rate = params_rate(params);
	switch (rate) {
	case 8000:
	case 16000:
	case 32000:
	case 48000:
		es325->dai[DAI_INDEX(id)].rate = rate;
		break;
	default:
		dev_err(codec->dev,
			"%s(): unsupported rate, %d\n",
			__func__, rate);
		return -EINVAL;
	}

	dev_info(codec->dev, "%s(): exit\n", __func__);
	return ret;
}
EXPORT_SYMBOL_GPL(es325_slim_hw_params);

static int es325_slim_hw_free(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	int rc = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return rc;
}

static int es325_slim_prepare(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	/*
	struct snd_soc_codec *codec = dai->codec;
	struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(codec->dev, "%s(): entry\n", __func__);

	dev_dbg(codec->dev, "%s(): exit\n", __func__);
	return ret;
	*/
	return 0;
}

int es325_slim_trigger(struct snd_pcm_substream *substream,
		       int cmd, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	/* local codec access */
	/* struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec); */
	/* remote codec access */
	/* struct es325_priv *es325 = &es325_priv; */
	int id = dai->id;
	int ret = 0;

	dev_info(codec->dev, "%s(): entry\n", __func__);
	dev_info(codec->dev, "GAC:%s(): stream_name = %s\n",
		 __func__,
		 es325_dai[DAI_INDEX(id)].playback.stream_name);
	dev_info(codec->dev, "GAC:%s(): id = %d\n",
		 __func__, es325_dai[DAI_INDEX(id)].id);
	dev_info(codec->dev, "GAC:%s(): cmd = %d\n",
		 __func__, cmd);

	return ret;
}
EXPORT_SYMBOL_GPL(es325_slim_trigger);

#define ES325_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |	\
		     SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 |	\
		     SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_48000 |	\
		     SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)
#define ES325_SLIMBUS_RATES (SNDRV_PCM_RATE_48000)

#define ES325_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE | \
		       SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S20_3BE | \
		       SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE | \
		       SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S32_BE)
#define ES325_SLIMBUS_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE)

#if defined(CONFIG_SND_SOC_ES325_I2S)
static struct snd_soc_dai_ops es325_portx_dai_ops = {
	.set_sysclk	= es325_i2s_set_sysclk,
	.set_pll	= es325_i2s_set_pll,
	.set_clkdiv	= es325_i2s_set_clkdiv,
	.set_fmt	= es325_i2s_set_dai_fmt,
	.set_tdm_slot	= es325_i2s_set_tdm_slot,
	.set_channel_map	= es325_i2s_set_channel_map,
	.set_tristate	= es325_i2s_set_tristate,
	.digital_mute	= es325_i2s_port_mute,
	.startup	= es325_i2s_startup,
	.shutdown	= es325_i2s_shutdown,
	.hw_params	= es325_i2s_hw_params,
	.hw_free	= es325_i2s_hw_free,
	.prepare	= es325_i2s_prepare,
	.trigger	= es325_i2s_trigger,
};
#endif

#if defined(CONFIG_SND_SOC_ES325_SLIM)
static struct snd_soc_dai_ops es325_slim_port_dai_ops = {
	.set_fmt	= es325_slim_set_dai_fmt,
	.set_channel_map	= es325_slim_set_channel_map,
	.get_channel_map	= es325_slim_get_channel_map,
	.set_tristate	= es325_slim_set_tristate,
	.digital_mute	= es325_slim_port_mute,
	.startup	= es325_slim_startup,
	.shutdown	= es325_slim_shutdown,
	.hw_params	= es325_slim_hw_params,
	.hw_free	= es325_slim_hw_free,
	.prepare	= es325_slim_prepare,
	.trigger	= es325_slim_trigger,
};
#endif

static struct snd_soc_dai_driver es325_dai[] = {
#if defined(CONFIG_SND_SOC_ES325_I2S)
/* initial support is for slimbus, not i2s */
	{
		.name = "es325-porta",
		.playback = {
			.stream_name = "PORTA Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ES325_RATES,
			.formats = ES325_FORMATS,
		},
		.capture = {
			.stream_name = "PORTA Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ES325_RATES,
			.formats = ES325_FORMATS,
		},
		.ops = &es325_portx_dai_ops,
	},
	{
		.name = "es325-portb",
		.playback = {
			.stream_name = "PORTB Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ES325_RATES,
			.formats = ES325_FORMATS,
		},
		.capture = {
			.stream_name = "PORTB Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ES325_RATES,
			.formats = ES325_FORMATS,
		},
		.ops = &es325_portx_dai_ops,
	},
	{
		.name = "es325-portc",
		.playback = {
			.stream_name = "PORTC Playback",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ES325_RATES,
			.formats = ES325_FORMATS,
		},
		.capture = {
			.stream_name = "PORTC Capture",
			.channels_min = 2,
			.channels_max = 2,
			.rates = ES325_RATES,
			.formats = ES325_FORMATS,
		},
		.ops = &es325_portx_dai_ops,
	},
#endif
#if defined(CONFIG_SND_SOC_ES325_SLIM)
	{
		.name = "es325-slim-rx1",
		.id = ES325_SLIM_1_PB,
		.playback = {
			.stream_name = "SLIM_PORT-1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES325_SLIMBUS_RATES,
			.formats = ES325_SLIMBUS_FORMATS,
		},
		.ops = &es325_slim_port_dai_ops,
	},
	{
		.name = "es325-slim-tx1",
		.id = ES325_SLIM_1_CAP,
		.capture = {
			.stream_name = "SLIM_PORT-1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES325_SLIMBUS_RATES,
			.formats = ES325_SLIMBUS_FORMATS,
		},
		.ops = &es325_slim_port_dai_ops,
	},
	{
		.name = "es325-slim-rx2",
		.id = ES325_SLIM_2_PB,
		.playback = {
			.stream_name = "SLIM_PORT-2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES325_SLIMBUS_RATES,
			.formats = ES325_SLIMBUS_FORMATS,
		},
		.ops = &es325_slim_port_dai_ops,
	},
	{
		.name = "es325-slim-tx2",
		.id = ES325_SLIM_2_CAP,
		.capture = {
			.stream_name = "SLIM_PORT-2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES325_SLIMBUS_RATES,
			.formats = ES325_SLIMBUS_FORMATS,
		},
		.ops = &es325_slim_port_dai_ops,
	},
	{
		.name = "es325-slim-rx3",
		.id = ES325_SLIM_3_PB,
		.playback = {
			.stream_name = "SLIM_PORT-3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES325_SLIMBUS_RATES,
			.formats = ES325_SLIMBUS_FORMATS,
		},
		.ops = &es325_slim_port_dai_ops,
	},
	{
		.name = "es325-slim-tx3",
		.id = ES325_SLIM_3_CAP,
		.capture = {
			.stream_name = "SLIM_PORT-3 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = ES325_SLIMBUS_RATES,
			.formats = ES325_SLIMBUS_FORMATS,
		},
		.ops = &es325_slim_port_dai_ops,
	},
#endif
};

#ifdef CONFIG_PM
static int es325_codec_suspend(struct snd_soc_codec *codec)
{
//	struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec);

	es325_set_bias_level(codec, SND_SOC_BIAS_OFF);

//	es325_sleep(es325);

	return 0;
}

static int es325_codec_resume(struct snd_soc_codec *codec)
{
//	struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec);

//	es325_wakeup(es325);

	es325_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define es325_codec_suspend NULL
#define es325_codec_resume NULL
#endif

int es325_remote_add_codec_controls(struct snd_soc_codec *codec)
{
	int rc;

	dev_info(codec->dev, "%s(): entry\n", __func__);
	dev_info(codec->dev, "%s(): codec->name = %s\n", __func__, codec->name);

	rc = snd_soc_add_codec_controls(codec, es325_digital_ext_snd_controls,
					ARRAY_SIZE(es325_digital_ext_snd_controls));
	if (rc)
		dev_err(codec->dev, "%s(): es325_digital_ext_snd_controls failed\n", __func__);

	return rc;
}

static int es325_codec_probe(struct snd_soc_codec *codec)
{
	struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec);

	dev_info(codec->dev, "%s(): entry\n", __func__);
	dev_info(codec->dev, "%s(): codec->name = %s\n", __func__, codec->name);
	dev_info(codec->dev, "%s(): codec = 0x%08x\n", __func__,
		 (unsigned int)codec);
	dev_info(codec->dev, "%s(): es325 = 0x%08x\n", __func__,
		 (unsigned int)es325);
	es325->codec = codec;

	codec->control_data = snd_soc_codec_get_drvdata(codec);
	dev_info(codec->dev, "%s(): codec->control_data = 0x%08x\n", __func__, (unsigned int)codec->control_data);

	es325_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/*
	rc = snd_soc_add_codec_controls(codec, es325_digital_snd_controls,
					ARRAY_SIZE(es325_digital_snd_controls));
	if (rc)
		dev_err(codec->dev, "%s(): es325_digital_snd_controls failed\n", __func__);
	*/

	dev_info(codec->dev, "%s(): exit\n", __func__);
	return 0;
}

static int  es325_codec_remove(struct snd_soc_codec *codec)
{
	struct es325_priv *es325 = snd_soc_codec_get_drvdata(codec);

	es325_set_bias_level(codec, SND_SOC_BIAS_OFF);

	kfree(es325);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_es325 = {
	.probe =	es325_codec_probe,
	.remove =	es325_codec_remove,
	.suspend =	es325_codec_suspend,
	.resume =	es325_codec_resume,
	.read =		es325_read,
	.write =	es325_write,
	.set_bias_level =	es325_set_bias_level,
};

#if defined(CONFIG_SND_SOC_ES325_I2C)
static int es325_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct esxxx_platform_data *pdata = i2c->dev.platform_data;
	struct es325_priv *es325;
	const char *filename = "audience-es325-fw.bin";
	int rc;

	dev_dbg(&i2c->dev, "%s(): entry\n", __func__);

	if (pdata == NULL) {
		dev_err(&i2c->dev, "%s(): pdata is NULL", __func__);
		rc = -EIO;
		goto pdata_error;
	}

	es325 = kzalloc(sizeof(struct es325_priv), GFP_KERNEL);
	if (es325 == NULL) {
		dev_err(&i2c->dev, "%s(): kzalloc failed", __func__);
		return -ENOMEM;
	}
	i2c_set_clientdata(i2c, es325);
	es325->this_client = i2c;

	rc = gpio_request(pdata->reset_gpio, "es325_reset");
	if (rc < 0) {
		dev_err(&i2c->dev, "%s(): es325_reset request failed",
			__func__);
		goto reset_gpio_request_error;
	}
	rc = gpio_direction_output(pdata->reset_gpio, 1);
	if (rc < 0) {
		dev_err(&i2c->dev, "%s(): es325_reset direction failed",
			__func__);
		goto reset_gpio_direction_error;
	}

	rc = gpio_request(pdata->wakeup_gpio, "es325_wakeup");
	if (rc < 0) {
		dev_err(&i2c->dev, "%s(): es325_wakeup request failed",
			__func__);
		goto wakeup_gpio_request_error;
	}
	rc = gpio_direction_output(pdata->wakeup_gpio, 1);
	if (rc < 0) {
		dev_err(&i2c->dev, "%s(): es325_wakeup direction failed",
			__func__);
		goto wakeup_gpio_direction_error;
	}

	dev_dbg(&i2c->dev, "%s(): initialize interrupt\n", __func__);
	dev_dbg(&i2c->dev, "%s(): TODO: interrupts\n", __func__);
	/*
	  connect 2 interrupts: accessory detect (accdet_gpio)
	  and int (int_gpio)

	  what interrupts are supported to the host?
	*/

	rc = gpio_request(pdata->gpioa_gpio, "es325_gpioa");
	if (rc < 0) {
		dev_err(&i2c->dev, "%s(): es325_gpioa request failed",
			__func__);
		goto gpioa_gpio_request_error;
	}
	rc = gpio_direction_input(pdata->gpioa_gpio);
	if (rc < 0) {
		dev_err(&i2c->dev, "%s(): es325_gpioa direction failed",
			__func__);
		goto gpioa_gpio_direction_error;
	}
	/*
	rc = request_irq(gpio_to_irq(pdata->gpioa_gpio), accdet_irq,
			 IRQF_DISABLED, "es325 accessory detect", i2c);
	if (rc) {
		dev_err(&i2c->dev, "%s(): es325_gpioa irq request failed\n",
			__func__);
		goto gpioa_request_irq_error;
	}
	*/

	rc = gpio_request(pdata->gpiob_gpio, "es325_gpiob");
	if (rc < 0) {
		dev_err(&i2c->dev, "%s(): es325_gpiob request failed",
			__func__);
		goto gpiob_gpio_request_error;
	}
	rc = gpio_direction_input(pdata->gpiob_gpio);
	if (rc < 0) {
		dev_err(&i2c->dev, "%s(): es325_gpiob direction failed",
			__func__);
		goto gpiob_gpio_direction_error;
	}

	/* rc = request_irq(gpio_to_irq(pdata->gpiob_gpio), es325_status_irq, */
	/*		 IRQF_DISABLED, "es325 status", i2c); */
	/* if (rc) { */
	/*	dev_err(&i2c->dev, "%s(): es325_gpiob irq request failed\n", */
	/*		__func__); */
	/*	goto gpiob_request_irq_error; */
	/* } */

	gpio_set_value(pdata->reset_gpio, 0);
	gpio_set_value(pdata->wakeup_gpio, 1);

	es325->pdata = pdata;

	rc = request_firmware(&es325->fw, filename, &i2c->dev);
	if (rc) {
		dev_err(&i2c->dev, "%s(): request_firmware(%s) failed %d\n",
			__func__, filename, rc);
		goto request_firmware_error;
	}
	rc = es325_bootup(es325);
	if (rc) {
		dev_err(&i2c->dev, "%s(): es325_bootup failed %d\n",
			__func__, rc);
		goto bootup_error;
	}
	release_firmware(es325->fw);

	rc = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_es325, es325_dai,
				    ARRAY_SIZE(es325_dai));
	dev_dbg(&i2c->dev, "%s(): rc = snd_soc_regsiter_codec() = %d\n", __func__, rc);

	dev_dbg(&i2c->dev, "%s(): exit\n", __func__);
	return rc;

bootup_error:
request_firmware_error:
gpiob_request_irq_error:
gpiob_gpio_direction_error:
	gpio_free(pdata->gpiob_gpio);
gpiob_gpio_request_error:
gpioa_request_irq_error:
gpioa_gpio_direction_error:
	gpio_free(pdata->gpioa_gpio);
gpioa_gpio_request_error:
wakeup_gpio_direction_error: 
	gpio_free(pdata->wakeup_gpio);
wakeup_gpio_request_error:
reset_gpio_direction_error:
	gpio_free(pdata->reset_gpio);
reset_gpio_request_error:
pdata_error:
	dev_dbg(&i2c->dev, "%s(): exit with error\n", __func__);
	return rc;
}

static int es325_i2c_remove(struct i2c_client *i2c)
{
	struct esxxx_platform_data *pdata = i2c->dev.platform_data;

	gpio_free(pdata->reset_gpio);
	gpio_free(pdata->wakeup_gpio);
	gpio_free(pdata->gpioa_gpio);

	snd_soc_unregister_codec(&i2c->dev);

	kfree(i2c_get_clientdata(i2c));

	return 0;
}

static const struct i2c_device_id es325_i2c_id[] = {
	{ "es325", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, es325_i2c_id);

static struct i2c_driver es325_i2c_driver = {
	.driver = {
		.name = "es325-codec",
		.owner = THIS_MODULE,
	},
	.probe = es325_i2c_probe,
	.remove = es325_i2c_remove,
	.id_table = es325_i2c_id,
};
#else
static int es325_slim_device_up(struct slim_device *sbdev);
static int es325_fw_thread(void *priv)
{
	struct es325_priv *es325 = (struct es325_priv *)priv;

	do {
		slim_get_logical_addr(es325->gen0_client,
				      es325->gen0_client->e_addr,
				      6, &(es325->gen0_client->laddr));
		usleep_range(1000, 2000);
	} while (es325->gen0_client->laddr == 0xf0);
	dev_dbg(&es325->gen0_client->dev, "%s(): gen0_client LA = %d\n",
		__func__, es325->gen0_client->laddr);
	do {
		slim_get_logical_addr(es325->intf_client,
				      es325->intf_client->e_addr,
				      6, &(es325->intf_client->laddr));
		usleep_range(1000, 2000);
	} while (es325->intf_client->laddr == 0xf0);
	dev_dbg(&es325->intf_client->dev, "%s(): intf_client LA = %d\n",
		__func__, es325->intf_client->laddr);
	es325_slim_device_up(es325->gen0_client);
	return 0;
}

#if 0
static int es325_dt_parse_slim_interface_dev_info(struct device *dev,
						struct slim_device *slim_ifd)
{
	int ret = 0;
	struct property *prop;

	ret = of_property_read_string(dev->of_node, "adnc,es325-codec-gen0",
				      &slim_ifd->name);
	if (ret) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"adnc,es325-codec-gen0", dev->of_node->full_name);
		return -ENODEV;
	}
	prop = of_find_property(dev->of_node,
			"adnc,es325-codec-gen0-eaddr", NULL);
	if (!prop) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"adnc,es325-codec-gen0-eaddr",
			dev->of_node->full_name);
		return -ENODEV;
	} else if (prop->length != 6) {
		dev_err(dev, "invalid codec slim ifd addr. addr length = %d\n",
			      prop->length);
		return -ENODEV;
	}
	memcpy(slim_ifd->e_addr, prop->value, 6);

	return 0;
}
#else
static int es325_dt_parse_slim_interface_dev_info(struct device *dev,
						struct slim_device *slim_ifd)
{
	struct property *prop;
	u8* e_addr;

	prop = of_find_property(dev->of_node,
			"elemental-addr", NULL);
	if (!prop) {
		dev_err(dev, "Looking up %s property in node %s failed",
			"elemental-addr",
			dev->of_node->full_name);
		return -ENODEV;
	} else if (prop->length != 6) {
		dev_err(dev, "invalid codec slim ifd addr. addr length = %d\n",
			      prop->length);
		return -ENODEV;
	}
	memcpy(slim_ifd->e_addr, prop->value, 6);

	e_addr = (u8*)prop->value;
	dev_dbg(dev, "eaddr:%02x %02x %02x %02x %02x %02x\n", 
		e_addr[0], e_addr[1], e_addr[2], e_addr[3], e_addr[4], e_addr[5]);

	return 0;
}

#endif

static int es325_clk_ctl(int enable)
{
	int ret = 0;

	if(enable)
		ret = qpnp_clkdiv_enable(codec_clk);
	else
		ret = qpnp_clkdiv_disable(codec_clk);
	
	if(EINVAL!=ret)
		ret = 0;

	return ret;
}

static struct esxxx_platform_data *es325_populate_dt_pdata(struct device *dev, struct slim_device *slim_ifd)
{
	struct esxxx_platform_data *pdata;
	#ifdef CONFIG_VENDOR_EDIT
	//liuyan 2013-9-11 add for es325
	//static int count_mclk=0;
	//int ret;
	struct msm_slim_ctrl *parent_ctl=NULL;
	#endif
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "could not allocate memory for platform data\n");
		return NULL;
	}

	es325_dt_parse_slim_interface_dev_info(dev, slim_ifd);

	#ifdef CONFIG_VENDOR_EDIT
	//liuyan 2013-9-11 add for es325 reset gpio
	if(dev->parent){
	    parent_ctl=dev_get_drvdata(dev->parent);
	    if(parent_ctl){
               pdata->reset_gpio =parent_ctl->reset_gpio;
	        printk("%s: reset gpio %d\n",__func__,pdata->reset_gpio);
	    }else{
               pdata->reset_gpio=0;
	    }
       }else{
           pdata->reset_gpio=0;
	}
	#else
	pdata->reset_gpio = of_get_named_gpio(dev->of_node,
				"adnc,reset-gpio", 0);
	if (pdata->reset_gpio < 0) {
		dev_err(dev, "Looking up %s property in node %s failed %d\n",
			"adnc,reset-gpio", dev->of_node->full_name,
			pdata->reset_gpio);
		goto err;
	}
	dev_dbg(dev, "%s: reset gpio %d", __func__, pdata->reset_gpio);
	#endif

	pdata->wakeup_gpio= of_get_named_gpio(dev->of_node,
				"adnc,wakeup-gpio", 0);
	if (pdata->wakeup_gpio < 0) {
		dev_err(dev, "Looking up %s property in node %s failed %d\n",
			"adnc,wakeup-gpio", dev->of_node->full_name,
			pdata->wakeup_gpio);
		goto err;
	}
	dev_dbg(dev, "%s: wakeup gpio %d", __func__, pdata->wakeup_gpio);
/*
	pdata->power_gpio= of_get_named_gpio(dev->of_node,
				"adnc,power-gpio", 0);
	if (pdata->power_gpio < 0) {
		dev_err(dev, "Looking up %s property in node %s failed %d\n",
			"adnc,power-gpio", dev->of_node->full_name,
			pdata->power_gpio);
		goto err;
	}
	dev_dbg(dev, "%s: power gpio %d", __func__, pdata->power_gpio);
*/

	//liuyan 2013-9-11 add for es325
	#ifndef CONFIG_VENDOR_EDIT
	pdata->mclk_gpio= of_get_named_gpio(dev->of_node,
				"adnc,es325-mclk-gpios", 0);
	printk("ES325 gpio:%d\n",pdata->mclk_gpio);
	if (pdata->mclk_gpio < 0) {
		dev_err(dev, "Looking up %s property in node %s failed %d\n",
			"adnc,power-gpio", dev->of_node->full_name,
			pdata->mclk_gpio);
		}
	ret = of_property_read_u32(dev->of_node,
			"adnc,es325-mclk-clk-freq", &pdata->mclk_freq);
	printk("pdata->mclk_freq:%d\n", pdata->mclk_freq);
	if (count_mclk<1) {
		count_mclk++;
		ret = gpio_request(pdata->mclk_gpio, "ES_CODEC_PMIC_MCLK");
		if (ret) {
			dev_err(dev,
				"%s: Failed to request taiko mclk gpio %d\n",
				__func__, pdata->mclk_gpio);
		}
	}
	printk("pdata->mclk_gpio:%d\n", pdata->mclk_gpio);
	#endif
	codec_clk = qpnp_clkdiv_get(dev, "es325-mclk");
	if (IS_ERR(codec_clk)) {
		dev_err(dev,
			"%s: Failed to request es325 mclk from pmic %ld\n",
			__func__, PTR_ERR(codec_clk));
		goto err;
	}else
		pdata->es325_clk_cb = es325_clk_ctl;

	return pdata;
err:
	devm_kfree(dev, pdata);
	return NULL;
}
#if 0
int32_t es325_get_dt_vreg_data(struct device_node *of_node,
	struct es325_vreg_t **es325_vreg, int *num_vreg)
{
	int32_t rc = 0, i = 0;
	uint32_t count = 0;
	uint32_t *vreg_array = NULL;
	struct es325_vreg_t *vreg = NULL;

	count = of_property_count_strings(of_node, "adnc,es325-vreg-name");
	CDBG("%s adnc,es325-vreg-name count %d\n", __func__, count);

	if (!count)
		return 0;

	vreg = kzalloc(sizeof(struct es325_vreg_t) * count,
		GFP_KERNEL);
	if (!vreg) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		return -ENOMEM;
	}
	*es325_vreg = vreg;
	*num_vreg = count;
	
	for (i = 0; i < count; i++) {
		rc = of_property_read_string_index(of_node,
			"adnc,es325-vreg-name", i,
			&vreg[i].reg_name);
		CDBG("%s reg_name[%d] = %s\n", __func__, i,
			vreg[i].reg_name);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR1;
		}
	}

	vreg_array = kzalloc(sizeof(uint32_t) * count, GFP_KERNEL);
	if (!vreg_array) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto ERROR1;
	}

	rc = of_property_read_u32_array(of_node, "adnc,es325-vreg-type",
		vreg_array, count);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto ERROR2;
	}
	for (i = 0; i < count; i++) {
		vreg[i].type = vreg_array[i];
		CDBG("%s es325_vreg[%d].type = %d\n", __func__, i,
			vreg[i].type);
	}

	rc = of_property_read_u32_array(of_node, "adnc,es325-vreg-min-voltage",
		vreg_array, count);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto ERROR2;
	}
	for (i = 0; i < count; i++) {
		vreg[i].min_voltage = vreg_array[i];
		CDBG("%s es325_vreg[%d].min_voltage = %d\n", __func__,
			i, vreg[i].min_voltage);
	}

	rc = of_property_read_u32_array(of_node, "adnc,es325-vreg-max-voltage",
		vreg_array, count);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto ERROR2;
	}
	for (i = 0; i < count; i++) {
		vreg[i].max_voltage = vreg_array[i];
		CDBG("%s es325_vreg[%d].max_voltage = %d\n", __func__,
			i, vreg[i].max_voltage);
	}

	rc = of_property_read_u32_array(of_node, "adnc,es325-vreg-op-mode",
		vreg_array, count);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto ERROR2;
	}
	for (i = 0; i < count; i++) {
		vreg[i].op_mode = vreg_array[i];
		CDBG("%s es325_vreg[%d].op_mode = %d\n", __func__, i,
			vreg[i].op_mode);
	}

	kfree(vreg_array);
	return rc;
ERROR2:
	kfree(vreg_array);
ERROR1:
	kfree(vreg);
	*num_vreg = 0;
	return rc;
}

int es325_config_single_vreg(struct device *dev,
	struct es325_vreg_t *es325_vreg, struct regulator **reg_ptr, int config)
{
	int rc = 0;
	if (config) {
		CDBG("%s enable %s\n", __func__, es325_vreg->reg_name);
		*reg_ptr = regulator_get(dev, es325_vreg->reg_name);
		if (IS_ERR(*reg_ptr)) {
			pr_err("%s: %s get failed\n", __func__,
				es325_vreg->reg_name);
			*reg_ptr = NULL;
			goto vreg_get_fail;
		}
		if (es325_vreg->type == REG_LDO) {
			rc = regulator_set_voltage(
				*reg_ptr, es325_vreg->min_voltage,
				es325_vreg->max_voltage);
			if (rc < 0) {
				pr_err("%s: %s set voltage failed\n",
					__func__, es325_vreg->reg_name);
				goto vreg_set_voltage_fail;
			}
			if (es325_vreg->op_mode >= 0) {
				rc = regulator_set_optimum_mode(*reg_ptr,
					es325_vreg->op_mode);
				CDBG("regulator_set_optimum_mode\n");
				if (rc < 0) {
					pr_err(
					"%s: %s set optimum mode failed\n",
					__func__, es325_vreg->reg_name);
					goto vreg_set_opt_mode_fail;
				}
			}
		}
		rc = regulator_enable(*reg_ptr);
		if (rc < 0) {
			pr_err("%s: %s enable failed\n",
				__func__, es325_vreg->reg_name);
			goto vreg_unconfig;
		}
	} else {
		if (*reg_ptr) {
			CDBG("%s disable %s\n", __func__, es325_vreg->reg_name);
			regulator_disable(*reg_ptr);
			if (es325_vreg->type == REG_LDO) {
				if (es325_vreg->op_mode >= 0)
					regulator_set_optimum_mode(*reg_ptr, 0);
				regulator_set_voltage(
					*reg_ptr, 0, es325_vreg->max_voltage);
			}
			regulator_put(*reg_ptr);
			*reg_ptr = NULL;
		}
	}
	return 0;

vreg_unconfig:
if (es325_vreg->type == REG_LDO)
	regulator_set_optimum_mode(*reg_ptr, 0);

vreg_set_opt_mode_fail:
if (es325_vreg->type == REG_LDO)
	regulator_set_voltage(*reg_ptr, 0, es325_vreg->max_voltage);

vreg_set_voltage_fail:
	regulator_put(*reg_ptr);
	*reg_ptr = NULL;

vreg_get_fail:
	return -ENODEV;
}
#endif


static int es325_slim_probe(struct slim_device *sbdev)
{
	//struct es325_vreg_t *es325_vreg;
	//int num_vreg;
	struct regulator *reg_ptr = reg_ptr;
	struct esxxx_platform_data *pdata;
	const char *filename = "audience-es325-fw.bin";
	int rc;
	struct task_struct *thread = NULL;


	printk("es325_slim_probe...\n");

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);
	dev_dbg(&sbdev->dev, "%s(): sbdev->name = %s\n", __func__, sbdev->name);
	dev_dbg(&sbdev->dev, "%s(): es325_priv = 0x%08x\n", __func__,
		(unsigned int)&es325_priv);

	if (sbdev->dev.of_node) {
		dev_info(&sbdev->dev, "Platform data from device tree\n");
		pdata = es325_populate_dt_pdata(&sbdev->dev, sbdev);
		sbdev->dev.platform_data = pdata;

	} else {
		dev_info(&sbdev->dev, "Platform data from board file\n");
		pdata = sbdev->dev.platform_data;
	}

	if (pdata == NULL) {
		dev_err(&sbdev->dev, "%s(): pdata is NULL", __func__);
		rc = -EIO;
		goto pdata_error;
	}

	rc = device_create_file(&sbdev->dev, &dev_attr_route_status);
	if (rc)
		dev_err(&sbdev->dev, "%s(): error file sysfs create\n", __func__);
	rc = device_create_file(&sbdev->dev, &dev_attr_route_config);
	if (rc)
		dev_err(&sbdev->dev, "%s(): error file sysfs create\n", __func__);
	rc = device_create_file(&sbdev->dev, &dev_attr_txhex);
	if (rc)
		dev_err(&sbdev->dev, "%s(): error file sysfs create\n", __func__);
	
	rc = device_create_file(&sbdev->dev, &dev_attr_es325_reset);
	if (rc)
		dev_err(&sbdev->dev, "%s(): error file sysfs create\n", __func__);

    rc = device_create_file(&sbdev->dev, &dev_attr_es325_live);
	if (rc)
		dev_err(&sbdev->dev, "%s(): error file sysfs create\n", __func__);

	slim_set_clientdata(sbdev, &es325_priv);

	if (strcmp(sbdev->name, "es325-codec-intf") == 0) {
		dev_dbg(&sbdev->dev, "%s(): interface device probe\n", __func__);
		es325_priv.intf_client = sbdev;
	}
	if (strcmp(sbdev->name, "es325-codec-gen0") == 0) {
		dev_dbg(&sbdev->dev, "%s(): generic device probe\n", __func__);
		es325_priv.gen0_client = sbdev;
	}

	if (es325_priv.intf_client == NULL ||
	    es325_priv.gen0_client == NULL) {
		dev_dbg(&sbdev->dev, "%s() incomplete initialization\n",
			__func__);
		return 0;
	}
/*
	es325_get_dt_vreg_data(sbdev->dev.of_node, &es325_vreg, &num_vreg);
	es325_config_single_vreg(&sbdev->dev, es325_vreg, &reg_ptr, 1);

	dev_dbg(&sbdev->dev, "%s(): power_gpio = %d\n", __func__,
		pdata->power_gpio);
	rc = gpio_request(pdata->power_gpio, "es325_power");
	//gpio_request_one(pdata->power_gpio, GPIOF_OUT_INIT_HIGH, "es325_power");
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_power request failed",
			__func__);
		goto power_gpio_request_error;
	}
	
	rc = gpio_direction_output(pdata->power_gpio, 1);
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_power direction failed",
			__func__);
		goto power_gpio_direction_error;
	}
	gpio_set_value(pdata->power_gpio, 1);
	
	dev_dbg(&sbdev->dev, "%s(): power_gpio_level = %d\n", __func__, __gpio_get_value(pdata->power_gpio));	
*/
	if (pdata->es325_clk_cb)
		pdata->es325_clk_cb(1);

	dev_dbg(&sbdev->dev, "%s(): reset_gpio = %d\n", __func__,
		pdata->reset_gpio);
	#ifndef CONFIG_VENDOR_EDIT
	//liuyan 2013-9-11 del for es325 gpio
	rc = gpio_request(pdata->reset_gpio, "es325_reset");
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_reset request failed",
			__func__);
		goto reset_gpio_request_error;
	}
	rc = gpio_direction_output(pdata->reset_gpio, 0);
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_reset direction failed",
			__func__);
		goto reset_gpio_direction_error;
	}
	gpio_set_value(pdata->reset_gpio, 0);
	dev_dbg(&sbdev->dev, "%s(): reset_gpio_level = %d\n", __func__, __gpio_get_value(pdata->reset_gpio));	
       #endif	

	dev_dbg(&sbdev->dev, "%s(): wakeup_gpio = %d\n", __func__, pdata->wakeup_gpio);
	rc = gpio_request(pdata->wakeup_gpio, "es325_wakeup");
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_wakeup request failed",
			__func__);
		goto wakeup_gpio_request_error;
	}

	rc = gpio_direction_output(pdata->wakeup_gpio, 1);
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_wakeup direction failed",
			__func__);
		goto wakeup_gpio_direction_error;
	}
	gpio_set_value(pdata->wakeup_gpio, 1);
	dev_dbg(&sbdev->dev, "%s(): wakeup_gpio_level = %d\n", __func__, __gpio_get_value(pdata->wakeup_gpio));	
/*
	dev_dbg(&sbdev->dev, "%s(): gpioa_gpio = %d\n", __func__, pdata->gpioa_gpio);
	
	rc = gpio_request(pdata->gpioa_gpio, "es325_gpioa");
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_gpioa request failed",
			__func__);
		goto gpioa_gpio_request_error;
	}
	rc = gpio_direction_input(pdata->gpioa_gpio);
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_gpioa direction failed",
			__func__);
		goto gpioa_gpio_direction_error;
	}
	*/

	dev_dbg(&sbdev->dev, "%s(): gpiob_gpio = %d\n", __func__, pdata->gpiob_gpio);
	/*
	rc = gpio_request(pdata->gpiob_gpio, "es325_gpiob");
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_gpiob request failed",
			__func__);
		goto gpiob_gpio_request_error;
	}
	rc = gpio_direction_input(pdata->gpiob_gpio);
	if (rc < 0) {
		dev_err(&sbdev->dev, "%s(): es325_gpiob direction failed",
			__func__);
		goto gpiob_gpio_direction_error;
	}
	*/

	gpio_set_value(pdata->reset_gpio, 0);
	mdelay(1);
	gpio_set_value(pdata->reset_gpio, 1);
	mdelay(50);

	es325_priv.pdata = pdata;

	rc = request_firmware((const struct firmware **)&es325_priv.fw,
			      filename, &sbdev->dev);
	if (rc) {
		dev_err(&sbdev->dev, "%s(): request_firmware(%s) failed %d\n", __func__, filename, rc);
		goto request_firmware_error;
	}

#if !defined(ES325_DEVICE_UP)
	thread = kthread_run(es325_fw_thread, &es325_priv, "audience thread");
	if (IS_ERR(thread)) {
		dev_err(&sbdev->dev, "%s(): can't create es325 firmware thread = %p\n", __func__, thread);
		return -1;
	}
#endif

	return 0;

request_firmware_error:
/*
gpiob_gpio_direction_error:
	gpio_free(pdata->gpiob_gpio);
gpiob_gpio_request_error:
gpioa_gpio_direction_error:
	gpio_free(pdata->gpioa_gpio);
gpioa_gpio_request_error:
*/
wakeup_gpio_direction_error:
	gpio_free(pdata->wakeup_gpio);
wakeup_gpio_request_error:
//liuyan 2013-9-11 del for es325
#ifndef CONFIG_VENDOR_EDIT
//reset_gpio_direction_error:
//	gpio_free(pdata->reset_gpio);
//reset_gpio_request_error:
//power_gpio_direction_error:
//	gpio_free(pdata->reset_gpio);
//power_gpio_request_error:
//liuyan del end
#endif
pdata_error:
	dev_dbg(&sbdev->dev, "%s(): exit with error\n", __func__);
	return rc;
}

static int register_snd_soc(struct es325_priv *priv)
{
	int rc;
	int i;
	int ch_cnt;
	struct slim_device *sbdev = priv->gen0_client;

	rc = es325_init_slim_slave(sbdev);

	dev_dbg(&sbdev->dev, "%s(): name = %s\n", __func__, sbdev->name);
	rc = snd_soc_register_codec(&sbdev->dev, &soc_codec_dev_es325, es325_dai,
				    ARRAY_SIZE(es325_dai));
	dev_dbg(&sbdev->dev, "%s(): rc = snd_soc_regsiter_codec() = %d\n", __func__, rc);

	/* allocate ch_num array for each DAI */
	for (i = 0; i < ARRAY_SIZE(es325_dai); i++) {
		switch (es325_dai[i].id) {
		case ES325_SLIM_1_PB:
		case ES325_SLIM_2_PB:
		case ES325_SLIM_3_PB:
			ch_cnt = es325_dai[i].playback.channels_max;
			break;
		case ES325_SLIM_1_CAP:
		case ES325_SLIM_2_CAP:
		case ES325_SLIM_3_CAP:
			ch_cnt = es325_dai[i].capture.channels_max;
			break;
		default:
			continue;
		}
		es325_priv.dai[i].ch_num =
			kzalloc((ch_cnt * sizeof(unsigned int)), GFP_KERNEL);
	}
#if defined(CONFIG_SND_SOC_WCD9310)
	/* front end for RX1 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_1_PB)].ch_num[0] = 152;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_1_PB)].ch_num[1] = 153;
	/* back end for RX1 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_2_CAP)].ch_num[0] = 138;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_2_CAP)].ch_num[1] = 139;
	/* front end for TX1 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_1_CAP)].ch_num[0] = 156;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_1_CAP)].ch_num[1] = 157;
	/* back end for TX1 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_3_PB)].ch_num[0] = 134;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_3_PB)].ch_num[1] = 135;
	/* front end for RX2 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_2_PB)].ch_num[0] = 154;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_2_PB)].ch_num[1] = 155;
	/* back end for RX2 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_3_CAP)].ch_num[0] = 143;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_3_CAP)].ch_num[1] = 144;
#elif defined(CONFIG_SND_SOC_WCD9320)
	/* front end for RX1 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_1_PB)].ch_num[0] = 152;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_1_PB)].ch_num[1] = 153;
	/* back end for RX1 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_2_CAP)].ch_num[0] = 144;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_2_CAP)].ch_num[1] = 145;
	/* front end for TX1 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_1_CAP)].ch_num[0] = 156;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_1_CAP)].ch_num[1] = 157;
	/* back end for TX1 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_3_PB)].ch_num[0] = 134;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_3_PB)].ch_num[1] = 135;
	/* front end for RX2 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_2_PB)].ch_num[0] = 154;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_2_PB)].ch_num[1] = 155;
	/* back end for RX2 */
	es325_priv.dai[DAI_INDEX(ES325_SLIM_3_CAP)].ch_num[0] = 146;
	es325_priv.dai[DAI_INDEX(ES325_SLIM_3_CAP)].ch_num[1] = 147;
#else
#error "no CODEC port to channel map specified"
#endif

	dev_dbg(&sbdev->dev, "%s(): exit\n", __func__);
	return rc;
}

static int es325_slim_remove(struct slim_device *sbdev)
{
	struct esxxx_platform_data *pdata = sbdev->dev.platform_data;

	dev_dbg(&sbdev->dev, "%s(): entry\n", __func__);
	dev_dbg(&sbdev->dev, "%s(): sbdev->name = %s\n", __func__, sbdev->name);

	gpio_free(pdata->reset_gpio);
	gpio_free(pdata->wakeup_gpio);
	//gpio_free(pdata->gpioa_gpio);
	//gpio_free(pdata->power_gpio);

	snd_soc_unregister_codec(&sbdev->dev);

	dev_dbg(&sbdev->dev, "%s(): exit\n", __func__);

	return 0;
}

static int es325_slim_device_up(struct slim_device *sbdev)
{
	struct es325_priv *priv;
	int rc;
	dev_info(&sbdev->dev, "%s: name=%s\n", __func__, sbdev->name);
	dev_info(&sbdev->dev, "%s: laddr=%d\n", __func__, sbdev->laddr);
	/* Start the firmware download in the workqueue context. */
	priv = slim_get_devicedata(sbdev);
	dev_info(&sbdev->dev, "%s: priv=%p\n", __func__, priv);
	if (strcmp(sbdev->name, "es325-codec-intf") == 0)
		return 0;
	rc = fw_download(priv);
	BUG_ON(rc != 0);
	return rc;
}

#if 0
static const struct slim_device_id es325_slim_id[] = {
	{ "es325-codec", 0 },
	{ "es325-codec-intf", 0 },
	{ "es325-codec-gen0", 0 },
	{  }
};
#else
static const struct slim_device_id es325_slim_id[] = {
	{ "es325-codec-gen0", 0 },
	{ "es325-codec-intf", 0 },
	{  }
};
#endif
MODULE_DEVICE_TABLE(slim, es325_slim_id);

static struct slim_driver es325_slim_driver = {
	.driver = {
		.name = "es325-codec",
		.owner = THIS_MODULE,
	},
	.probe = es325_slim_probe,
	.remove = es325_slim_remove,
#if defined(ES325_DEVICE_UP)
	.device_up = es325_slim_device_up,
#endif
	.id_table = es325_slim_id,
};
#endif

static __init int es325_init(void)
{
	int ret = 0;
      //liuyan add for dvt
     #ifdef CONFIG_VENDOR_EDIT
     int pcb_version;
     #endif
     //liuyan add end
	pr_info("%s(): entry", __func__);
#if defined(CONFIG_SND_SOC_ES325_I2C)
	ret = i2c_add_driver(&es325_i2c_driver);
	if (ret)
		pr_err("Failed to register Audience eS325 I2C driver: %d\n",
		       ret);
#else
	pr_info("%s(): slim_driver_register()", __func__);
       //liuyan add for dvt
      #ifdef CONFIG_VENDOR_EDIT
      pcb_version=get_pcb_version();
      printk("%s:pcb version:%d\n",__func__,pcb_version);
      if(pcb_version==HW_VERSION__10){
      #endif
     //liuyan add end
    mutex_init(&es325_priv.power_lock); 
    INIT_DELAYED_WORK(&es325_priv.sleep_work, es325_delayed_sleep);
    es325_priv.power_state = ES325_POWER_BOOT;

	ret = slim_driver_register(&es325_slim_driver);
	if (ret)
		pr_err("Error registering Audience eS325 SLIMbus driver: %d\n",
		       ret);
      	}
#endif

	pr_info("%s(): exit\n", __func__);
	return ret;
}
module_init(es325_init);

static __exit void es325_exit(void)
{
	pr_info("%s(): entry\n", __func__);
#if defined(CONFIG_SND_SOC_ES325_I2C)
	i2c_del_driver(&es325_i2c_driver);
#else
	/* no support from QCOM to unregister
	 * slim_driver_unregister(&es325_slim_driver);
	 */
#endif
	pr_info("%s(): exit\n", __func__);
}
module_exit(es325_exit);


MODULE_DESCRIPTION("ASoC ES325 driver");
MODULE_AUTHOR("Greg Clemson <gclemson@audience.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:es325-codec");
MODULE_FIRMWARE("audience-es325-fw.bin");
