#pragma once

#include <obs-module.h>
#include <plugin-support.h>
#include <x264.h>
#include <util/darray.h>

struct cordyceps_stalk_encoder {
	obs_encoder_t* encoder;

	x264_param_t params;
	x264_t* context;

	DARRAY(uint8_t) packet_data;

	uint8_t* extra_data;
	uint8_t* sei;
	size_t extra_data_size;
	size_t sei_size;
};

static void cordyceps_stalk_encoder_get_video_info(void* data,
						   struct video_scale_info*
							   info);