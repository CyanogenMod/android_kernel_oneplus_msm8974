/*
 * es325.h  --  ES325 Soc Audio access values
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ES325_ACCESS_H
#define _ES325_ACCESS_H

static unsigned short es325_algo_paramid[] = {
	0x0002, /* MIC_CONFIG */
	0x0003, /* AEC_MODE */
	0x0004, /* TX_AGC */
	0x0005, /* TX_AGC_TARGET_LEVEL */
	0x0006, /* TX_AGC_NOISE_FLOOR */
	0x0007, /* TX_AGC_SNR_IMPROVEMENT */
	0x0009, /* VEQ_ENABLE */
	0x000D, /* RX_OUT_LIMITER_MAX_LEVEL */
	0x000E, /* RX_NOISE_SUPPRESS */
	0x0010, /* RX_STS */
	0x0011, /* RX_STS_RATE */
	0x0012, /* AEC_SPEAKER_VOLUME */
	0x0015, /* SIDETONE */
	0x0016, /* SIDETONE_GAIN */
	0x001A, /* TX_COMFORT_NOISE */
	0x001B, /* TX_COMFORT_NOISE_LEVEL */
	0x001C, /* ALGORITHM_RESET */
	0x001F, /* RX_POST_EQ */
	0x0020, /* TX_POST_EQ */
	0x0023, /* AEC_CNG */
	0x0025, /* VEQ_NOISE_ESTIMATION_ADJUSTMENT */
	0x0026, /* TX_AGC_SLEW_RATE_UP */
	0x0027, /* TX_AGC_SLEW_RATE_DOWN */
	0x0028, /* RX_AGC */
	0x0029, /* RX_AGC_TARGET_LEVEL */
	0x002A, /* RX_AGC_NOISE_FLOOR */
	0x002B, /* RX_AGC_SNR_IMPROVEMENT */
	0x002C, /* RX_AGC_SLEW_RATE_UP */
	0x002D, /* RX_AGC_SLEW_RATE_DOWN */
	0x002E, /* AEC_CNG_GAIN */
	0x0030, /* TX_MBC */
	0x0031, /* RX_MBC */
	0x0034, /* AEC_ESE */
	0x0039, /* TX_NS_ADAPTATION_SPEED */
	0x003A, /* TX_SNR_ESTIMATE */
	0x003D, /* VEQ_MAX_GAIN */
	0x003E, /* TX_AGC_GUARDBAND */
	0x003F, /* RX_AGC_GUARDBAND */
	0x0040, /* TX_OUT_LIMITER_MAX_LEVEL */
	0x0042, /* TX_IN_LIMITER_MAX_LEVEL */
	0x0043, /* RX_NS_ADAPTATION_SPEED */
	0x0047, /* AEC_VARIABLE_ECHO_REF_DELAY */
	0x004B, /* TX_NOISE_SUPPRESS_LEVEL */
	0x004C, /* RX_NOISE_SUPPRESS_LEVEL */
	0x004D, /* RX_CNG */
	0x004E, /* RX_CNG_GAIN */
	0x0100, /* TX_AGC_MAX_GAIN */
	0x0102, /* RX_AGC_MAX_GAIN */
	0x1001, /* AVALON_API_VERSION_LO */
	0x1002, /* AVALON_API_VERSION_HI */
	0x1003, /* AVALON_AV_PROCESSOR */
	0x1004, /* AVALON_AV_CONFIG */
	0x1005, /* AVALON_EQ_PRESET */
	0x1006, /* AVALON_STEREO_WIDTH */
	0x1007, /* AVALON_AV_DIGITAL_OUT_GAIN */
	0x1008, /* AVALON_TDMBC */
	0x1009, /* AVALON_AV_OUT_LIMIT */
	0x100A, /* AVALON_STEREO_WIDENING */
	0x100B, /* AVALON_STAT_NS */
	0x100C, /* AVALON_STAT_NS_SUPPRESS */
	0x100D, /* AVALON_STAT_NS_ADAP_SPEED */
	0x100E, /* AVALON_STAT_NS_MODE */
	0x100F, /* AVLALON_STAT_NS_MAX_NOISE_ENERGY */
	0x1010, /* AVALON_VBB */
	0x1011, /* AVALON_VBB_STRENGTH */
	0x1012, /* AVALON_EQ_MODE */
	0x1013, /* AVALON_EQ_GRAPHIC_BAND1_GAIN */
	0x1014, /* AVALON_EQ_GRAPHIC_BAND2_GAIN */
	0x1015, /* AVALON_EQ_GRAPHIC_BAND3_GAIN */
	0x1016, /* AVALON_EQ_GRAPHIC_BAND4_GAIN */
	0x1017, /* AVALON_EQ_GRAPHIC_BAND5_GAIN */
	0x1018, /* AVALON_EQ_GRAPHIC_BAND6_GAIN */
	0x1019, /* AVALON_EQ_GRAPHIC_BAND7_GAIN */
	0x101A, /* AVALON_EQ_GRAPHIC_BAND8_GAIN */
	0x101B, /* AVALON_EQ_GRAPHIC_BAND9_GAIN */
	0x101C, /* AVALON_EQ_GRAPHIC_BAND10_GAIN */
	0x101D, /* AVALON_TDDRC */
	0x101E, /* AVALON_TDDRC_STRENGTH */
	0x101F, /* AVALON_LIMITER */
	0x1020, /* AVALON_EQ */
	0x2000, /* DIRAC */
	0x2001, /* DIRAC_OUT_HEADROOM_LIMITER */
	0x2002, /* DIRAC_MODE */
	0x2004, /* DIRAC_IN_HEADROOM_LIMITER */
	0x2006, /* DIRAC_COMFORT_NOISE */
	0x2007, /* DIRAC_COMFORT_NOISE_LEVEL */
	0x2008, /* DIRAC_NARRATOR_VQOS */
	0x2009, /* DIRAC_NARRATOR_POSITION_SUPPRESS */
	0x200A, /* DIRAC_NARRATOR_AGC_OUT */
	0x200B, /* DIRAC_NARRATOR_AGC_SPEECH_TARGET */
	0x200C, /* DIRAC_NARRATOR_AGC_SNR_IMPROVE */
	0x200D, /* DIRAC_NARRATOR_AGC_NOISE_FLOOR */
	0x200E, /* DIRAC_NARRATOR_AGC_MAX_GAIN */
	0x200F, /* DIRAC_NARRATOR_AGC_UP_RATE */
	0x2010, /* DIRAC_NARRATOR_AGC_DOWN_RATE */
	0x2011, /* DIRAC_NARRATOR_AGC_GUARDBAND */
	0x2013, /* DIRAC_NARRATOR_POST_EQ_MODE */
	0x2014, /* DIRAC_NARRATOR_MBC_MODE */
	0x2015, /* DIRAC_SCENE_BEAM_WIDTH */
	0x2016, /* DIRAC_SCENE_AGC_OUT */
	0x2017, /* DIRAC_SCENE_AGC_SPEECH_TARGET */
	0x2018, /* DIRAC_SCENE_AGC_SNR_IMPROVE */
	0x2019, /* DIRAC_SCENE_AGC_NOISE_FLOOR */
	0x201A, /* DIRAC_SCENE_AGC_MAX_GAIN */
	0x201B, /* DIRAC_SCENE_AGC_UP_RATE */
	0x201C, /* DIRAC_SCENE_AGC_DOWN_RATE */
	0x201D, /* DIRAC_SCENE_AGC_GUARDBAND */
	0x201F, /* DIRAC_SCENE_VQOS */
	0x2020, /* DIRAC_SCENE_POST_EQ_MODE */
	0x2021, /* DIRAC_SCENE_MBC_MODE */
	0x3001, /* TONE_PARAM_API_VERSION_LO */
	0x3002, /* TONE_PARAM_API_VERSION_HI */
	0x3003, /* TONE_PARAM_ENABLE_BEEP_SYS */
	0x3005, /* TONE_PARAM_ENABLE_GEN_BEEP */
	0x3006, /* TONE_PARAM_GEN_BEEP_ON */
	0x3007, /* TONE_PARAM_GEN_BEEP_FREQ1 */
	0x3008, /* TONE_PARAM_GEN_BEEP_FREQ2 */
	0x3009, /* TONE_PARAM_GEN_BEEP_PAN_LR */
	0x300A, /* TONE_PARAM_GEN_BEEP_GAIN */
	0x0053, /* DEREVERB_ENABLE */
	0x0054, /* DEREVERB_GAIN */
	0x0053, /* DEREVERB_ENABLE */
	0x0054, /* DEREVERB_GAIN */
	0x004f, /* BWE_ENABLE */
	0x0050, /* BWE_HIGH_BAND_GAIN */
	0x0051, /* BWE_MAX_SNR */
	0x0052, /* BWE_POST_EQ_ENABLE */
};

static unsigned short es325_dev_paramid[] = {
	0x0A00, /* PORTA_WORD_LEN */
	0x0A01, /* PORTA_TDM_SLOTS_PER_FRAME */
	0x0A02, /* PORTA_TX_DELAY_FROM_FS */
	0x0A03, /* PORTA_RX_DELAY_FROM_FS */
	0x0A04, /* PORTA_LATCH_EDGE */
	0x0A05, /* PORTA_ENDIAN */
	0x0A06, /* PORTA_TRISTATE */
	0x0A07, /* PORTA_AUDIO_PORT_MODE */
	0x0A08, /* PORTA_TDM_ENABLED */
	0x0A09, /* PORTA_CLOCK_CONTROL */
	0x0A0A, /* PORTA_DATA_JUSTIFICATION */
	0x0A0B, /* PORTA_FS_DURATION */
	0x0B00, /* PORTB_WORD_LEN */
	0x0B01, /* PORTB_TDM_SLOTS_PER_FRAME */
	0x0B02, /* PORTB_TX_DELAY_FROM_FS */
	0x0B03, /* PORTB_RX_DELAY_FROM_FS */
	0x0B04, /* PORTB_LATCH_EDGE */
	0x0B05, /* PORTB_ENDIAN */
	0x0B06, /* PORTB_TRISTATE */
	0x0B07, /* PORTB_AUDIO_PORT_MODE */
	0x0B08, /* PORTB_TDM_ENABLED */
	0x0B09, /* PORTB_CLOCK_CONTROL */
	0x0B0A, /* PORTB_DATA_JUSTIFICATION */
	0x0B0B, /* PORTB_FS_DURATION */
	0x0C00, /* PORTC_WORD_LEN */
	0x0C01, /* PORTC_TDM_SLOTS_PER_FRAME */
	0x0C02, /* PORTC_TX_DELAY_FROM_FS */
	0x0C03, /* PORTC_RX_DELAY_FROM_FS */
	0x0C04, /* PORTC_LATCH_EDGE */
	0x0C05, /* PORTC_ENDIAN */
	0x0C06, /* PORTC_TRISTATE */
	0x0C07, /* PORTC_AUDIO_PORT_MODE */
	0x0C08, /* PORTC_TDM_ENABLED */
	0x0C09, /* PORTC_CLOCK_CONTROL */
	0x0C0A, /* PORTC_DATA_JUSTIFICATION */
	0x0C0B, /* PORTC_FS_DURATION */
	0x0D00, /* PORTD_WORD_LEN */
	0x0D01, /* PORTD_TDM_SLOTS_PER_FRAME */
	0x0D02, /* PORTD_TX_DELAY_FROM_FS */
	0x0D03, /* PORTD_RX_DELAY_FROM_FS */
	0x0D04, /* PORTD_LATCH_EDGE */
	0x0D05, /* PORTD_ENDIAN */
	0x0D06, /* PORTD_TRISTATE */
	0x0D07, /* PORTD_AUDIO_PORT_MODE */
	0x0D08, /* PORTD_TDM_ENABLED */
	0x0D09, /* PORTD_CLOCK_CONTROL */
	0x0D0A, /* PORTD_DATA_JUSTIFICATION */
	0x0D0B, /* PORTD_FS_DURATION */
	0x0900, /* SLIMBUS_LINK_MULTI_CHANNEL */
};

static struct es325_cmd_access es325_cmd_access[] = {
	{ /* POWER_STATE */
		.read_msg = { 0x80, 0x10, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x10, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 1,
	},
	{ /* STREAMING */
		.read_msg = { 0x80, 0x25, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x25, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 5,
	},
	{ /* FE_STREAMING */
		.read_msg = { 0x80, 0x28, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x28, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 8,
	},
	{ /* PRESET */
		.read_msg = { 0x80, 0x31, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x31, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* ALGO_STATS */
		.read_msg = { 0x80, 0x42, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x42, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* ALGO_PROCESSING */
		.read_msg = { 0x80, 0x43, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x1c, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 1,
	},
	{ /* ALGO_SAMPLE_RATE */
		.read_msg = { 0x80, 0x4b, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x4c, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 5,
	},
	{ /* SMOOTH_RATE */
		.read_msg = { 0x80, 0x4d, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x4e, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* CHANGE_STATUS */
		.read_msg = { 0x80, 0x4f, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x4f, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 4,
	},
	{ /* DIGITAL_PASS_THROUGH */
		.read_msg = { 0x80, 0x52, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x52, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* DATA_PATH */
		.read_msg = { 0x80, 0x5b, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* ALGORITHM */
		.read_msg = { 0x80, 0x5d, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5c, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 4,
	},
	{ /* MIX_SAMPLE_RATE */
		.read_msg = { 0x80, 0x65, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5e, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 5,
	},
	{ /* SIGNAL_RMS_PORTA_DIN_LEFT */
		.read_msg = { 0x80, 0x13, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTA_DIN_RIGHT */
		.read_msg = { 0x80, 0x13, 0x00, 0x01 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x01 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTA_DOUT_LEFT */
		.read_msg = { 0x80, 0x13, 0x00, 0x02 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x02 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTA_DOUT_RIGHT */
		.read_msg = { 0x80, 0x13, 0x00, 0x03 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x03 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTB_DIN_LEFT */
		.read_msg = { 0x80, 0x13, 0x00, 0x04 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x04 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTB_DIN_RIGHT */
		.read_msg = { 0x80, 0x13, 0x00, 0x05 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x05 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTB_DOUT_LEFT */
		.read_msg = { 0x80, 0x13, 0x00, 0x06 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x06 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTB_DOUT_RIGHT */
		.read_msg = { 0x80, 0x13, 0x00, 0x07 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x07 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTC_DIN_LEFT */
		.read_msg = { 0x80, 0x13, 0x00, 0x08 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x08 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTC_DIN_RIGHT */
		.read_msg = { 0x80, 0x13, 0x00, 0x09 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x09 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTC_DOUT_LEFT */
		.read_msg = { 0x80, 0x13, 0x00, 0x0a },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x0a },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTC_DOUT_RIGHT */
		.read_msg = { 0x80, 0x13, 0x00, 0x0b },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x0b },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTD_DIN_LEFT */
		.read_msg = { 0x80, 0x13, 0x00, 0x0c },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x0c },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTD_DIN_RIGHT */
		.read_msg = { 0x80, 0x13, 0x00, 0x0d },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x0d },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTD_DOUT_LEFT */
		.read_msg = { 0x80, 0x13, 0x00, 0x0e },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x0e },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_RMS_PORTD_DOUT_RIGHT */
		.read_msg = { 0x80, 0x13, 0x00, 0x0f },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x13, 0x00, 0x0f },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTA_DIN_LEFT */
		.read_msg = { 0x80, 0x14, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTA_DIN_RIGHT */
		.read_msg = { 0x80, 0x14, 0x00, 0x01 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x01 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTA_DOUT_LEFT */
		.read_msg = { 0x80, 0x14, 0x00, 0x02 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x02 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTA_DOUT_RIGHT */
		.read_msg = { 0x80, 0x14, 0x00, 0x03 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x03 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTB_DIN_LEFT */
		.read_msg = { 0x80, 0x14, 0x00, 0x04 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x04 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTB_DIN_RIGHT */
		.read_msg = { 0x80, 0x14, 0x00, 0x05 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x05 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTB_DOUT_LEFT */
		.read_msg = { 0x80, 0x14, 0x00, 0x06 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x06 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTB_DOUT_RIGHT */
		.read_msg = { 0x80, 0x14, 0x00, 0x07 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x07 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTC_DIN_LEFT */
		.read_msg = { 0x80, 0x14, 0x00, 0x08 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x08 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTC_DIN_RIGHT */
		.read_msg = { 0x80, 0x14, 0x00, 0x09 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x09 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTC_DOUT_LEFT */
		.read_msg = { 0x80, 0x14, 0x00, 0x0a },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x0a },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTC_DOUT_RIGHT */
		.read_msg = { 0x80, 0x14, 0x00, 0x0b },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x0b },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTD_DIN_LEFT */
		.read_msg = { 0x80, 0x14, 0x00, 0x0c },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x0c },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTD_DIN_RIGHT */
		.read_msg = { 0x80, 0x14, 0x00, 0x0d },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x0d },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTD_DOUT_LEFT */
		.read_msg = { 0x80, 0x14, 0x00, 0x0e },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x0e },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* SIGNAL_PEAK_PORTD_DOUT_RIGHT */
		.read_msg = { 0x80, 0x14, 0x00, 0x0f },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x14, 0x00, 0x0f },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 65535,
	},
	{ /* DIGITAL_GAIN_PRIMARY */
		.read_msg = { 0x80, 0x1D, 0x00, 0x01 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x00, 0x01 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_SECONDARY */
		.read_msg = { 0x80, 0x1D, 0x00, 0x02 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x02, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_TERTIARY */
		.read_msg = { 0x80, 0x1D, 0x00, 0x03 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x03, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_QUAD */
		.read_msg = { 0x80, 0x1D, 0x00, 0x04 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x04, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_FEIN */
		.read_msg = { 0x80, 0x1D, 0x00, 0x05 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x05, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_AUDIN1 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x06 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x06, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_AUDIN2 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x07 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x07, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_AUDIN3 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x08 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x08, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_AUDIN4 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x09 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x09, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_UITONE1 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x0A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x0a, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_UITONE2 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x0B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x0b, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_CSOUT */
		.read_msg = { 0x80, 0x1D, 0x00, 0x10 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x10, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_FEOUT1 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x11 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x11, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_FEOUT2 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x12 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x12, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_AUDOUT1 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x13 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x13, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_AUDOUT2 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x14 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x14, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_AUDOUT3 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x15 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x15, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* DIGITAL_GAIN_AUDOUT4 */
		.read_msg = { 0x80, 0x1D, 0x00, 0x16 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x15, 0x16, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 255,
	},
	{ /* PORTA_TIMING */
		.read_msg = { 0x80, 0x59, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x58, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* PORTB_TIMING */
		.read_msg = { 0x80, 0x59, 0x10, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x58, 0x10, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* PORTC_TIMING */
		.read_msg = { 0x80, 0x59, 0x20, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x58, 0x20, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* PORTD_TIMING */
		.read_msg = { 0x80, 0x59, 0x30, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x58, 0x30, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* PRIMARY_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x04, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x04, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* SECONDARY_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x08, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x08, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* TERTIARY_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x0C, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x0c, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* QUAD_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x10, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x10, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* FEIN_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x14, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x14, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* AUDIN1_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x18, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x18, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* AUDIN2_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x1C, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x1c, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* AUDIN3_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x20, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x20, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* AUDIN4_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x24, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x24, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* UITONE1_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x28, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x28, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* UITONE2_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x2C, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x2c, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 15,
	},
	{ /* PCM0_0_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x00 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_1_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x01 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x01 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_2_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x02 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x02 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_3_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x03 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x03 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_4_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x04 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x04 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_5_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x05 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x05 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_6_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x06 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x06 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_7_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x07 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x07 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_8_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x08 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x08 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_9_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x09 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x09 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_10_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x0A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x0a },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_11_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x0B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x0b },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_12_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x0C },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x0c },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_13_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x0D },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x0d },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_14_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x0E },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x0e },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_15_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x0F },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x0f },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_16_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x10 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x10 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_17_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x11 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x11 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_18_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x12 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x12 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_19_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x13 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x13 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_20_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x14 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x14 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_21_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x15 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x15 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_22_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x16 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x16 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_23_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x17 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x17 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_24_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x18 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x18 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_25_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x19 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x19 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_26_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x1A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x1a },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_27_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x1B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x1b },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_28_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x1C },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x1c },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_29_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x1D },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x1d },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_30_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x1E },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x1e },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM0_31_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x1F },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x1f },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_0_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x20 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x20 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_1_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x21 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x21 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_2_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x22 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x22 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_3_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x23 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x23 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_4_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x24 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x24 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_5_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x25 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x25 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_6_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x26 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x26 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_7_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x27 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x27 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_8_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x28 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x28 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_9_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x29 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x29 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_10_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x2A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x2a },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_11_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x2B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x2b },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_12_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x2C },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x2c },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_13_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x2D },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x2d },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_14_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x2E },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x2e },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_15_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x2F },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x2f },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_16_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x30 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x30 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_17_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x31 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x31 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_18_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x32 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x32 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_19_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x33 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x33 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_20_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x34 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x34 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_21_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x35 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x35 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_22_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x36 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x36 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_23_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x37 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x37 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_24_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x38 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x38 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_25_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x39 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x39 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_26_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x3A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x3a },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_27_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x3B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x3b },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_28_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x3C },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x3c },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_29_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x3D },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x3d },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_30_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x3E },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x3e },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM1_31_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x3F },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x3f },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_0_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x40 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x40 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_1_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x41 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x41 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_2_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x42 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x42 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_3_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x43 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x43 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_4_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x44 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x44 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_5_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x45 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x45 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_6_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x46 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x46 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_7_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x47 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x47 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_8_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x48 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x48 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_9_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x49 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x49 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_10_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x4A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x4a },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_11_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x4B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x4b },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_12_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x4C },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x4c },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_13_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x4D },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x4d },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_14_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x4E },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x4e },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_15_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x4F },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x4f },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_16_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x50 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x50 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_17_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x51 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x51 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_18_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x52 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x52 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_19_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x53 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x53 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_20_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x54 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x54 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_21_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x55 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x55 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_22_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x56 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x56 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_23_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x57 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x57 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_24_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x58 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x58 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_25_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x59 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x59 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_26_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x5A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x5a },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_27_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x5B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x5b },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_28_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x5C },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x5c },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_29_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x5D },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x5d },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_30_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x5E },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x5e },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM2_31_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x5F },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x5f },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_0_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x60 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x60 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_1_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x61 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x61 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_2_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x62 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x62 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_3_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x63 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x63 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_4_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x64 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x64 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_5_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x65 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x65 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_6_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x66 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x66 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_7_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x67 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x67 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_8_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x68 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x68 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_9_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x69 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x69 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_10_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x6A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x6a },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_11_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x6B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x6b },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_12_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x6C },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x6c },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_13_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x6D },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x6d },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_14_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x6E },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x6e },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_15_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x6F },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x6f },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_16_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x70 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x70 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_17_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x71 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x71 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_18_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x72 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x72 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_19_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x73 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x73 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_20_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x74 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x74 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_21_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x75 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x75 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_22_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x76 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x76 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_23_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x77 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x77 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_24_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x78 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x78 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_25_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x79 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x79 },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_26_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x7A },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x7a },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_27_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x7B },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x7b },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_28_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x7C },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x7c },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_29_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x7D },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x7d },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_30_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x7E },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x7e },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* PCM3_31_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0x7F },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0x7f },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* SBUS_TX0_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0xAA },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0xaa },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* SBUS_TX1_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0xAB },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0xab },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* SBUS_TX2_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0xAC },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0xac },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* SBUS_TX3_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0xAD },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0xad },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* SBUS_TX4_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0xAE },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0xae },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* SBUS_TX5_PATH_MUX */
		.read_msg = { 0x80, 0x5B, 0x00, 0xAF },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x5a, 0x40, 0xaf },
		.write_msg_len = 4,
		.val_shift = 10,
		.val_max = 6,
	},
	{ /* FLUSH */
		.read_msg = { 0x80, 0x5A, 0x00, 0x00 },
		.read_msg_len = 4,
		.write_msg = { 0x80, 0x59, 0x00, 0x00 },
		.write_msg_len = 4,
		.val_shift = 0,
		.val_max = 0,
	},
};

#endif /* _ES325_ACCESS_H */
