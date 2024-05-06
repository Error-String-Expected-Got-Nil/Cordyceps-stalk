/*
Cordyceps-stalk
Copyright (C) 2024 ErrorStringExpectedGotNil

This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "cordyceps-stalk-encoder.h"

static const char* cordyceps_stalk_encoder_get_name(void* unused)
{
	UNUSED_PARAMETER(unused);

	return "Cordyceps Stalk Encoder";
}

static void* cordyceps_stalk_encoder_create(obs_data_t* settings,
					    obs_encoder_t* encoder)
{
	video_t *video = obs_encoder_video(encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	switch (voi->format) {
	case VIDEO_FORMAT_I010:
	case VIDEO_FORMAT_P010:
	case VIDEO_FORMAT_P216:
	case VIDEO_FORMAT_P416:
		obs_log(LOG_WARNING, "Cordyceps stalk encoder is based on the "
				     "obs_x264 encoder, and likewise does not "
				     "support high-precision formats");
		return NULL;
	default:
		if (voi->colorspace == VIDEO_CS_2100_PQ ||
		    voi->colorspace == VIDEO_CS_2100_HLG) {
			obs_log(LOG_WARNING, "Cordyceps stalk encoder is based "
					     "on the obs_x264 encoder, and "
					     "likewise does not support using "
					     "x264 with Rec. 2100");
			return NULL;
		}
		break;
	}

	struct cordyceps_stalk_encoder* cordyceps_stalk_encoder = bzalloc(
		sizeof(struct cordyceps_stalk_encoder));
	cordyceps_stalk_encoder->encoder = encoder;

	int code = x264_param_default_preset(&cordyceps_stalk_encoder->params,
					     "veryfast", 0);

	if (code != 0) {
		obs_log(LOG_WARNING, "Cordyceps stalk encoder could not "
				     "initialize libx264 encoder, provided "
				     "presets were invalid");
		return NULL;
	}

	cordyceps_stalk_encoder->context = x264_encoder_open(
		&cordyceps_stalk_encoder->params);

	if (!cordyceps_stalk_encoder->context) {
		obs_log(LOG_WARNING, "Cordyceps stalk encoder libx264 encoder "
				     "failed to open!");
	} else {
		// Load headers
		x264_nal_t* nals;
		int nal_count;
		DARRAY(uint8_t) header;

		da_init(header);

		x264_encoder_headers(cordyceps_stalk_encoder->context, &nals,
				     &nal_count);

		// TODO: Not finished, copy OBS implementation verbatim!
	}

	if (!cordyceps_stalk_encoder->context) {
		bfree(cordyceps_stalk_encoder);
		return NULL;
	}

	return cordyceps_stalk_encoder;
}

struct obs_encoder_info cordyceps_stalk_encoder = {
	.id = "cordyceps-stalk-encoder",
	.type = OBS_ENCODER_VIDEO,
	.codec = "h264",
	.get_name = cordyceps_stalk_encoder_get_name,
	.create = cordyceps_stalk_encoder_create
};