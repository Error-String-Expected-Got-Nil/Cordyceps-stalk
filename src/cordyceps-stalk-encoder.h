#pragma once

#include <obs-module.h>
#include <plugin-support.h>
#include <x264.h>
#include <util/darray.h>
#include <util/threading.h>

struct cordyceps_stalk_encoder {
	obs_encoder_t* encoder;

	x264_param_t params;
	x264_t* context;

	DARRAY(uint8_t) packet_data;

	uint8_t* extra_data;
	uint8_t* sei;
	size_t extra_data_size;
	size_t sei_size;

	volatile bool realtime_mode;
	volatile int64_t requested_frames;
	pthread_mutex_t mutex;
};

static void
cordyceps_stalk_encoder_get_video_info(void* data,
				       struct video_scale_info* info);

static void ph_set_realtime_mode(void* data, calldata_t* calldata);
static void ph_request_frames(void* data, calldata_t* calldata);