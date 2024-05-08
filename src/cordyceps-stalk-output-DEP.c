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

// Almost all of this is copied directly from obs-ffmpeg-mux.c, with some
// trimming for uneeded features, and modifications for Cordyceps.

#include "cordyceps-stalk-output-DEP.h"

static const char* cordyceps_stalk_output_get_name(void* type)
{
	UNUSED_PARAMETER(type);
	// No translation because I don't want to deal with that
	return "Cordyceps Stalk Output";
}

static uint64_t cordyceps_stalk_output_total_bytes(void* data)
{
	struct cordyceps_stalk_output* stream = data;
	return stream->total_bytes;
}

static void* cordyceps_stalk_output_create(obs_data_t* settings,
					   obs_output_t* output)
{
	UNUSED_PARAMETER(settings);

	struct cordyceps_stalk_output* stream = bzalloc(sizeof(*stream));
	stream->output = output;
	dstr_init_copy(&stream->path, "");

	proc_handler_t* ph = obs_output_get_proc_handler(output);
	proc_handler_add(ph, "void set_path(in string path)", ph_set_path,
			 stream);

	return stream;
}

static void cordyceps_stalk_output_destroy(void* data)
{
	struct cordyceps_stalk_output* stream = data;

	os_process_pipe_destroy(stream->pipe);

	bfree(stream);
}

static inline bool cordyceps_stalk_output_start_internal(
	struct cordyceps_stalk_output* stream, obs_data_t* settings)
{
	if (dstr_is_empty(&stream->path)) {
		obs_log(LOG_WARNING, "Cordyceps-stalk output failed to start; "
				     "path was empty string");
		return false;
	}

	// TODO: Video path hacky right now
	const char* path = stream->path.array;

	// Basic startup checks
	if (!obs_output_can_begin_data_capture(stream->output, 0)) {
		obs_log(LOG_WARNING, "Cordyceps-stalk output failed to start; "
				     "not ready to begin data capture");
		return false;
	}

	if (!obs_output_initialize_encoders(stream->output, 0)) {
		obs_log(LOG_WARNING, "Cordyceps-stalk output failed to start; "
				     "failed to initialize encoders");
		return false;
	}

	stream->sent_headers = false;

	obs_log(LOG_INFO, "attempting to open file '%s'", path);
	// Check file path is writable
	FILE* test_file = fopen(path, "wb");
	if (!test_file) {
		obs_log(LOG_ERROR, "failed to start output, targeted file path"
				   "was not writable");
		return false;
	}
	obs_log(LOG_INFO, "attempting to close test file");
	fclose(test_file);

	obs_log(LOG_INFO, "attempting to delete file");
	// Delete any existing file at path
	os_unlink(path);

	// Create output pipe, mostly copied from obs-ffmpeg-mux.c
	obs_encoder_t* vencoder = obs_output_get_video_encoder(stream->output);

	// This is a video-only output so if there's no video encoder, it
	//  should just fail to start.
	if (!vencoder) return false;

	// Will likely only work on Windows, but oh well.
	struct dstr cmd;
	dstr_init_move_array(&cmd, os_get_executable_path_ptr
			     ("obs-ffmpeg-mux.exe"));
	dstr_insert_ch(&cmd, 0, '\"');
	dstr_cat(&cmd, "\" \"");

	dstr_copy(&stream->path, path);
	dstr_replace(&stream->path, "\"", "\"\"");
	dstr_cat_dstr(&cmd, &stream->path);

	dstr_catf(&cmd, "\" %d %d ", 1, 0);

	// TODO: Bitrate hardcoded!
	//int bitrate = (int)obs_data_get_int(settings, "bitrate");
	int bitrate = 2500;
	video_t *video = obs_get_video();
	const struct video_output_info *info = video_output_get_info(video);

	enum AVColorPrimaries pri = AVCOL_PRI_UNSPECIFIED;
	enum AVColorTransferCharacteristic trc = AVCOL_TRC_UNSPECIFIED;
	enum AVColorSpace spc = AVCOL_SPC_UNSPECIFIED;
	switch (info->colorspace) {
	case VIDEO_CS_601:
		pri = AVCOL_PRI_SMPTE170M;
		trc = AVCOL_TRC_SMPTE170M;
		spc = AVCOL_SPC_SMPTE170M;
		break;
	case VIDEO_CS_DEFAULT:
	case VIDEO_CS_709:
		pri = AVCOL_PRI_BT709;
		trc = AVCOL_TRC_BT709;
		spc = AVCOL_SPC_BT709;
		break;
	case VIDEO_CS_SRGB:
		pri = AVCOL_PRI_BT709;
		trc = AVCOL_TRC_IEC61966_2_1;
		spc = AVCOL_SPC_BT709;
		break;
	case VIDEO_CS_2100_PQ:
		pri = AVCOL_PRI_BT2020;
		trc = AVCOL_TRC_SMPTE2084;
		spc = AVCOL_SPC_BT2020_NCL;
		break;
	case VIDEO_CS_2100_HLG:
		pri = AVCOL_PRI_BT2020;
		trc = AVCOL_TRC_ARIB_STD_B67;
		spc = AVCOL_SPC_BT2020_NCL;
	}

	const enum AVColorRange range = (info->range == VIDEO_RANGE_FULL)
						? AVCOL_RANGE_JPEG
						: AVCOL_RANGE_MPEG;

	const int max_luminance =
		(trc == AVCOL_TRC_SMPTE2084)
			? (int)obs_get_video_hdr_nominal_peak_level()
			: ((trc == AVCOL_TRC_ARIB_STD_B67) ? 1000 : 0);

	dstr_catf(&cmd, "%s %d %d %d %d %d %d %d %d %d %d %d ",
		  obs_encoder_get_codec(vencoder), bitrate,
		  obs_output_get_width(stream->output),
		  obs_output_get_height(stream->output), (int)pri, (int)trc,
		  (int)spc, (int)range,
		  (int)determine_chroma_location(
			  obs_to_ffmpeg_video_format(info->format), spc),
		  max_luminance, (int)info->fps_num, (int)info->fps_den);

	stream->pipe = os_process_pipe_create(cmd.array, "w");

	if (!stream->pipe) {
		obs_log(LOG_ERROR, "Failed to create output process pipe!");
		return false;
	}

	os_atomic_set_bool(&stream->active, true);
	os_atomic_set_bool(&stream->capturing, true);
	os_atomic_set_bool(&stream->stopping, false);
	stream->total_bytes = 0;
	obs_output_begin_data_capture(stream->output, 0);

	obs_log(LOG_INFO, "Cordyceps-stalk output starting, writing to "
			  "file '%s'...", stream->path.array);

	return true;
}

static bool cordyceps_stalk_output_start(void* data)
{
	struct cordyceps_stalk_output* stream = data;

	obs_data_t* settings = obs_output_get_settings(stream->output);
	bool success = cordyceps_stalk_output_start_internal(stream, settings);
	obs_data_release(settings);

	return success;
}

static void cordyceps_stalk_output_stop(void* data, uint64_t ts)
{
	struct cordyceps_stalk_output* stream = data;

	if (os_atomic_load_bool(&stream->capturing) || ts == 0) {
		stream->stop_ts = (int64_t) ts / 1000LL;
		os_atomic_set_bool(&stream->stopping, true);
		os_atomic_set_bool(&stream->capturing, false);
	}
}

// Only video headers for now
static bool send_headers(struct cordyceps_stalk_output* stream)
{
	obs_encoder_t* vencoder = obs_output_get_video_encoder(stream->output);

	struct encoder_packet packet = {
		.type = OBS_ENCODER_VIDEO,
		.timebase_den = 1
	};

	if (!obs_encoder_get_extra_data(vencoder, &packet.data, &packet.size))
		return false;

	return write_packet(stream, &packet);
}

static void cordyceps_stalk_output_mux_data(void* data,
					    struct encoder_packet* packet)
{
	struct cordyceps_stalk_output* stream = data;

	if (!os_atomic_load_bool(&stream->active)) return;

	if (!stream->sent_headers) {
		if (!send_headers(stream)) return;
		stream->sent_headers = true;
	}

	if (!packet) {
		obs_log(LOG_ERROR, "Cordyceps-stalk output's encoder failed!");
		deactivate(stream, false);
		return;
	}

	if (os_atomic_load_bool(&stream->stopping)) {
		if (packet->sys_dts_usec >= stream->stop_ts
		    || packet->size == 0) {
			deactivate(stream, true);
			return;
		}
	}

	// Output takes a data size 0 packet to be one that was intentionally
	// skipped by the encoder
	if (packet->size == 0) {
		return;
	}

	write_packet(stream, packet);
}

static void signal_failure(struct cordyceps_stalk_output* stream)
{
	obs_log(LOG_WARNING, "Cordyceps-stalk output signaled failure");
	deactivate(stream, false);
	os_atomic_set_bool(&stream->capturing, false);
}

void deactivate(struct cordyceps_stalk_output* stream, bool success)
{
	if (os_atomic_load_bool(&stream->active)) {
		os_process_pipe_destroy(stream->pipe);
		stream->pipe = NULL;

		os_atomic_set_bool(&stream->active, false);
		os_atomic_set_bool(&stream->sent_headers, false);

		obs_log(LOG_INFO, "Cordyceps-stalk output stopped for "
				  "file '%s'", stream->path.array);
	}

	if (success) {
		obs_output_end_data_capture(stream->output);
	} else {
		obs_log(LOG_ERROR, "Cordyceps-stalk output stopped due "
				   "to error!");
		obs_output_signal_stop(stream->output, 1);
	}

	os_atomic_set_bool(&stream->stopping, false);
}

bool write_packet(struct cordyceps_stalk_output* stream,
		  struct encoder_packet* packet)
{
	struct ffm_packet_info info = {
		.pts = packet->pts,
		.dts = packet->dts,
		.size = (uint32_t) packet->size,
		.type = FFM_PACKET_VIDEO,
		.keyframe = packet->keyframe
	};

	size_t ret = os_process_pipe_write(stream->pipe,
					   (const uint8_t*) &info,
					   sizeof(info));
	if (ret != sizeof(info)) {
		obs_log(LOG_ERROR, "Cordyceps-stalk failed to write to pipe "
				   "for packet info!");
		signal_failure(stream);
		return false;
	}

	ret = os_process_pipe_write(stream->pipe, packet->data, packet->size);
	if (ret != packet->size) {
		obs_log(LOG_ERROR, "Cordyceps-stalk failed to write to pipe "
				   "for packet data!");
		signal_failure(stream);
		return false;
	}

	stream->total_bytes += packet->size;

	return true;
}

void ph_set_path(void* data, calldata_t* cd)
{
	struct cordyceps_stalk_output* stream = data;
	dstr_copy(&stream->path, calldata_string(cd, "path"));
	obs_log(LOG_INFO, "Proc triggered; Path set to '%s'",
		stream->path.array);
}

const struct obs_output_info cordyceps_stalk_output = {
	.id = "cordyceps-stalk-output",
	.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED,
	.encoded_video_codecs = "h264",
	.get_name = cordyceps_stalk_output_get_name,
	.get_total_bytes = cordyceps_stalk_output_total_bytes,
	.create = cordyceps_stalk_output_create,
	.destroy = cordyceps_stalk_output_destroy,
	.start = cordyceps_stalk_output_start,
	.stop = cordyceps_stalk_output_stop,
	.encoded_packet = cordyceps_stalk_output_mux_data
};