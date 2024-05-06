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

static void log_x264(void *param, int level, const char *format, va_list args)
{
	static const int level_map[] = {
		LOG_ERROR,
		LOG_WARNING,
		LOG_INFO,
		LOG_DEBUG,
	};

	UNUSED_PARAMETER(param);
	if (level < X264_LOG_ERROR)
		level = X264_LOG_ERROR;
	else if (level > X264_LOG_DEBUG)
		level = X264_LOG_DEBUG;

	blogva(level_map[level], format, args);
}

static inline int get_x264_cs_val(const char *const name,
				  const char *const names[])
{
	int idx = 0;
	do {
		if (strcmp(names[idx], name) == 0)
			return idx;
	} while (!!names[++idx]);

	return 0;
}

// Below is copied from obs_x264. Not sure what it all does but it's probably
// important.
static void update_params(struct cordyceps_stalk_encoder*
				  cordyceps_stalk_encoder, obs_data_t* settings)
{
	video_t* video = obs_encoder_video(cordyceps_stalk_encoder->encoder);
	const struct video_output_info* voi = video_output_get_info(video);
	struct video_scale_info info;

	info.format = voi->format;
	info.colorspace = voi->colorspace;
	info.range = voi->range;

	cordyceps_stalk_encoder_get_video_info(cordyceps_stalk_encoder, &info);

	// TODO: Hardcoded keyframe interval
	cordyceps_stalk_encoder->params.i_keyint_max = 250;

	// Using CRF so this should all be 0
	cordyceps_stalk_encoder->params.rc.i_vbv_max_bitrate = 0;
	cordyceps_stalk_encoder->params.rc.i_bitrate = 0;
	cordyceps_stalk_encoder->params.rc.i_vbv_buffer_size = 0;

	cordyceps_stalk_encoder->params.i_width =
		(int) obs_encoder_get_width(cordyceps_stalk_encoder->encoder);
	cordyceps_stalk_encoder->params.i_height =
		(int) obs_encoder_get_height(cordyceps_stalk_encoder->encoder);

	cordyceps_stalk_encoder->params.i_fps_num = voi->fps_num;
	cordyceps_stalk_encoder->params.i_fps_den = voi->fps_den;
	cordyceps_stalk_encoder->params.i_timebase_num = voi->fps_num;
	cordyceps_stalk_encoder->params.i_timebase_den = voi->fps_den;

	cordyceps_stalk_encoder->params.pf_log = log_x264;
	cordyceps_stalk_encoder->params.p_log_private = cordyceps_stalk_encoder;
	cordyceps_stalk_encoder->params.i_log_level = X264_LOG_WARNING;

	// TODO: Hardcoded CRF
	cordyceps_stalk_encoder->params.rc.i_rc_method = X264_RC_CRF;
	cordyceps_stalk_encoder->params.rc.f_rf_constant = 23.0f;

	static const char *const smpte170m = "smpte170m";
	static const char *const bt709 = "bt709";
	const char *colorprim = bt709;
	const char *transfer = bt709;
	const char *colmatrix = bt709;
	switch (info.colorspace) {
	case VIDEO_CS_DEFAULT:
	case VIDEO_CS_709:
		colorprim = bt709;
		transfer = bt709;
		colmatrix = bt709;
		break;
	case VIDEO_CS_601:
		colorprim = smpte170m;
		transfer = smpte170m;
		colmatrix = smpte170m;
		break;
	case VIDEO_CS_SRGB:
		colorprim = bt709;
		transfer = "iec61966-2-1";
		colmatrix = bt709;
		break;
	default:
		break;
	}

	cordyceps_stalk_encoder->params.vui.i_sar_height = 1;
	cordyceps_stalk_encoder->params.vui.i_sar_width = 1;
	cordyceps_stalk_encoder->params.vui.b_fullrange =
		info.range == VIDEO_RANGE_FULL;
	cordyceps_stalk_encoder->params.vui.i_colorprim =
		get_x264_cs_val(colorprim, x264_colorprim_names);
	cordyceps_stalk_encoder->params.vui.i_transfer =
		get_x264_cs_val(transfer, x264_transfer_names);
	cordyceps_stalk_encoder->params.vui.i_colmatrix =
		get_x264_cs_val(colmatrix, x264_colmatrix_names);

	if (info.format == VIDEO_FORMAT_NV12)
		cordyceps_stalk_encoder->params.i_csp = X264_CSP_NV12;
	else if (info.format == VIDEO_FORMAT_I420)
		cordyceps_stalk_encoder->params.i_csp = X264_CSP_I420;
	else if (info.format == VIDEO_FORMAT_I444)
		cordyceps_stalk_encoder->params.i_csp = X264_CSP_I444;
	else
		cordyceps_stalk_encoder->params.i_csp = X264_CSP_NV12;
}

static const char* cordyceps_stalk_encoder_get_name(void* unused)
{
	UNUSED_PARAMETER(unused);

	return "Cordyceps Stalk Encoder";
}

static void* cordyceps_stalk_encoder_create(obs_data_t* settings,
					    obs_encoder_t* encoder)
{
	video_t* video = obs_encoder_video(encoder);
	const struct video_output_info* voi = video_output_get_info(video);
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

	// TODO: Hardcoded preset
	int code = x264_param_default_preset(&cordyceps_stalk_encoder->params,
					     "veryfast", 0);

	if (code != 0) {
		obs_log(LOG_WARNING, "Cordyceps stalk encoder could not "
				     "initialize libx264 encoder, provided "
				     "presets were invalid");
		return NULL;
	}

	update_params(cordyceps_stalk_encoder, NULL);

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
		DARRAY(uint8_t) sei;

		da_init(header);
		da_init(sei);

		x264_encoder_headers(cordyceps_stalk_encoder->context, &nals,
				     &nal_count);

		for (int i = 0; i < nal_count; i++) {
			x264_nal_t *nal = nals + i;

			if (nal->i_type == NAL_SEI)
				da_push_back_array(sei, nal->p_payload,
						   nal->i_payload);
			else
				da_push_back_array(header, nal->p_payload,
						   nal->i_payload);
		}

		cordyceps_stalk_encoder->extra_data = header.array;
		cordyceps_stalk_encoder->extra_data_size = header.num;
		cordyceps_stalk_encoder->sei = sei.array;
		cordyceps_stalk_encoder->sei_size = sei.num;
	}

	if (!cordyceps_stalk_encoder->context) {
		bfree(cordyceps_stalk_encoder);
		return NULL;
	}

	return cordyceps_stalk_encoder;
}

static void cordyceps_stalk_encoder_destroy(void* data)
{
	struct cordyceps_stalk_encoder* cordyceps_stalk_encoder = data;

	if (cordyceps_stalk_encoder) {
		if (cordyceps_stalk_encoder->context) {
			x264_encoder_close(cordyceps_stalk_encoder->context);
			bfree(cordyceps_stalk_encoder->sei);
			bfree(cordyceps_stalk_encoder->extra_data);

			cordyceps_stalk_encoder->context = NULL;
			cordyceps_stalk_encoder->extra_data = NULL;
			cordyceps_stalk_encoder->sei = NULL;

			da_free(cordyceps_stalk_encoder->packet_data);

			bfree(cordyceps_stalk_encoder);
		}
	}
}

static bool cordyceps_stalk_encoder_encode(void* data,
					   struct encoder_frame* frame,
					   struct encoder_packet* packet,
					   bool* received_packet)
{
	struct cordyceps_stalk_encoder* cordyceps_stalk_encoder = data;
	x264_nal_t* nals;
	int nal_count;
	int ret;
	x264_picture_t pic;
	x264_picture_t pic_out;

	if (!frame || !packet || !received_packet) return false;

	// Initialize picture data
	x264_picture_init(&pic);
	pic.i_pts = frame->pts;
	pic.img.i_csp = cordyceps_stalk_encoder->params.i_csp;

	if (cordyceps_stalk_encoder->params.i_csp == X264_CSP_NV12)
		pic.img.i_plane = 2;
	else if (cordyceps_stalk_encoder->params.i_csp == X264_CSP_I420
		 || cordyceps_stalk_encoder->params.i_csp == X264_CSP_I444)
		pic.img.i_plane = 3;

	for (int i = 0; i < pic.img.i_plane; i++) {
		pic.img.i_stride[i] = (int) frame->linesize[i];
		pic.img.plane[i] = frame->data[i];
	}

	ret = x264_encoder_encode(cordyceps_stalk_encoder->context, &nals,
				  &nal_count, &pic, &pic_out);

	if (ret < 0) {
		obs_log(LOG_WARNING, "Cordyceps-stalk encoder encode failed!");
		return false;
	}

	*received_packet = (nal_count != 0);

	// Parse packet. Copying obs_x264 exactly here, not sure what
	// everything actually does.
	if (!nal_count) return true;

	da_resize(cordyceps_stalk_encoder->packet_data, 0);

	for (int i = 0; i < nal_count; i++) {
		x264_nal_t *nal = nals + i;
		da_push_back_array(cordyceps_stalk_encoder->packet_data,
				   nal->p_payload, nal->i_payload);
	}

	packet->data = cordyceps_stalk_encoder->packet_data.array;
	packet->size = cordyceps_stalk_encoder->packet_data.num;
	packet->type = OBS_ENCODER_VIDEO;
	packet->pts = pic_out.i_pts;
	packet->dts = pic_out.i_dts;
	packet->keyframe = pic_out.b_keyframe != 0;

	return true;
}

static bool cordyceps_stalk_encoder_get_extra_data(void* data,
						   uint8_t** extra_data,
						   size_t* size)
{
	struct cordyceps_stalk_encoder* cordyceps_stalk_encoder = data;

	if (!cordyceps_stalk_encoder->context) {
		return false;
	}

	*extra_data = cordyceps_stalk_encoder->extra_data;
	*size = cordyceps_stalk_encoder->extra_data_size;
	return true;
}

static void cordyceps_stalk_encoder_get_video_info(void* data,
						   struct video_scale_info*
							   info)
{
	struct cordyceps_stalk_encoder* cordyceps_stalk_encoder = data;
	enum video_format pref_format;

	pref_format = obs_encoder_get_preferred_video_format(
		cordyceps_stalk_encoder->encoder);

	if (!(pref_format == VIDEO_FORMAT_I420
	      || pref_format == VIDEO_FORMAT_I444
	      || pref_format == VIDEO_FORMAT_NV12)) {
		pref_format = (info->format == VIDEO_FORMAT_I420
			       || info->format == VIDEO_FORMAT_I444
			       || info->format == VIDEO_FORMAT_NV12)
				      ? info->format
				      : VIDEO_FORMAT_NV12;
	}

	info->format = pref_format;
}

const struct obs_encoder_info cordyceps_stalk_encoder = {
	.id = "cordyceps-stalk-encoder",
	.type = OBS_ENCODER_VIDEO,
	.codec = "h264",
	.get_name = cordyceps_stalk_encoder_get_name,
	.create = cordyceps_stalk_encoder_create,
	.destroy = cordyceps_stalk_encoder_destroy,
	.encode = cordyceps_stalk_encoder_encode,
	.get_extra_data = cordyceps_stalk_encoder_get_extra_data,
	.get_video_info = cordyceps_stalk_encoder_get_video_info
};