#pragma once

#include <obs-module.h>
#include <plugin-support.h>
#include <x264.h>
#include <util/darray.h>

struct cordyceps_stalk_encoder {
	obs_encoder_t* encoder;

	x264_param_t params;
	x264_t* context;
};