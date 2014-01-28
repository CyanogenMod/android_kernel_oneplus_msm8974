/*
 * es325-export.h  -- 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ES325_EXPORT_H
#define _ES325_EXPORT_H

#include <sound/soc.h>

int es325_slim_set_channel_map(struct snd_soc_dai *dai,
	unsigned int tx_num, unsigned int *tx_slot,
	unsigned int rx_num, unsigned int *rx_slot);
int es325_slim_get_channel_map(struct snd_soc_dai *dai,
	unsigned int *tx_num, unsigned int *tx_slot,
	unsigned int *rx_num, unsigned int *rx_slot);
int es325_slim_trigger(struct snd_pcm_substream *substream,
	int cmd, struct snd_soc_dai *dai);
int es325_slim_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai);
int es325_remote_cfg_slim_rx(int dai_id);
int es325_remote_cfg_slim_tx(int dai_id);
int es325_remote_close_slim_rx(int dai_id);
int es325_remote_close_slim_tx(int dai_id);
int es325_remote_add_codec_controls(struct snd_soc_codec *codec);

int es325_remote_route_enable(struct snd_soc_dai *dai);

#define ES325_DAI_ID_OFFSET	10
/* remote dai-id -> local dai->id */
/* ES325_AIF1_PB -> ES325_SLIM_1_PB */
/* ES325_AIF1_CAP -> ES325_SLIM_1_CAP */
#define ES325_AIF1_PB		(ES325_DAI_ID_OFFSET + 1)
#define ES325_AIF1_CAP		(ES325_DAI_ID_OFFSET + 2)

#endif /* _ES325_EXPORT_H */
