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

// Mostly copied from obs-ffmpeg-output.c

#include "cordyceps-stalk-output.h"

static void ffmpeg_log(void* param, int level, const char* format, va_list args)
{
	UNUSED_PARAMETER(param);

	if (level <= AV_LOG_INFO)
		blogva(LOG_DEBUG, format, args);
}

static void ffmpeg_deactivate(struct cso_data* cso)
{
	if (cso->write_thread_active) {
		os_event_signal(cso->stop_event);
		os_sem_post(cso->write_semaphore);
		pthread_join(cso->write_thread, NULL);
		cso->write_thread_active = false;
	}

	pthread_mutex_lock(&cso->write_mutex);

	for (size_t i = 0; i < cso->packets.num; i++)
		av_packet_free(cso->packets.array + i);
	da_free(cso->packets);

	pthread_mutex_unlock(&cso->write_mutex);

	if (cso->context.initialized) av_write_trailer(cso->context.output_ctx);

	if (cso->context.video_stream) {
		avcodec_free_context(&cso->context.video_ctx);
		av_frame_unref(cso->context.vframe);
		av_frame_free(&cso->context.vframe);
	}

	if (cso->context.output_ctx) {
		avio_close(cso->context.output_ctx->pb);
		avformat_free_context(cso->context.output_ctx);
	}

	memset(&cso->context, 0, sizeof(struct ffmpeg_context));
}

static int process_packet(struct cso_data* cso)
{
	AVPacket* packet = NULL;
	int ret;

	pthread_mutex_lock(&cso->write_mutex);
	if (cso->packets.num) {
		packet = cso->packets.array[0];
		da_erase(cso->packets, 0);
	}
	pthread_mutex_unlock(&cso->write_mutex);

	if (!packet) return 0;

	if (os_atomic_load_bool(&cso->stopping)) {
		av_packet_free(&packet);
		return 0;
	}

	cso->total_bytes += packet->size;

	ret = av_interleaved_write_frame(cso->context.output_ctx, packet);
	if (ret < 0) obs_log(LOG_WARNING, "Error while writing packet: %s",
			av_err2str(ret));

	av_packet_free(&packet);
	return ret;
}

static void* write_thread(void* data)
{
	struct cso_data* cso = data;

	while (os_sem_wait(cso->write_semaphore) == 0) {
		if (os_event_try(cso->stop_event) == 0) break;

		int ret = process_packet(cso);
		if (ret != 0) {
			int code = OBS_OUTPUT_ERROR;

			pthread_detach(cso->write_thread);
			cso->write_thread_active = false;

			if (ret == -ENOSPC) code = OBS_OUTPUT_NO_SPACE;

			obs_output_signal_stop(cso->output, code);
			ffmpeg_deactivate(cso);
			break;
		}
	}

	cso->active = false;
	return NULL;
}

static void cso_stop_full(struct cso_data* cso)
{
	if (cso->active) {
		obs_output_end_data_capture(cso->output);

		cso->requested_frames = 0;

		ffmpeg_deactivate(cso);
	}
}

static bool make_filepath(const char* dir, struct dstr* target)
{
	size_t len = strlen(dir);
	if (dir[len - 1] != '/' && dir[len - 1] != '\\') return false;

	char name_buf[1024];
	time_t cur_time = time(NULL);
	size_t ret = strftime(name_buf, 1024, "cordyceps %Y-%m-%d "
					      "%H:%M:%S", localtime(&cur_time));
	if (!ret) return false;

	dstr_cat(target, dir);
	dstr_cat(target, name_buf);

	return true;
}

static bool init_ffmpeg(struct cso_data* cso)
{
	video_t* video = obs_output_video(cso->output);
	const struct video_output_info* voi = video_output_get_info(video);
	struct ffmpeg_config config;
	obs_data_t* settings;

	settings = obs_output_get_settings(cso->output);

	struct dstr path;
	dstr_init(&path);

	bool path_create_success =
		make_filepath(obs_data_get_string(settings, "dirpath"), &path);

	if (!path_create_success) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "given path was not directory");
		return false;
	}

	config.filepath = path.array;
	config.gop_size = (int) obs_data_get_int(settings, "gop_size");

	double crf = obs_data_get_double(settings, "crf");
	const char* preset = obs_data_get_string(settings, "preset");

	obs_data_release(settings);

	config.width = (int) obs_output_get_width(cso->output);
	config.height = (int) obs_output_get_height(cso->output);

	config.pixel_format =
		obs_to_ffmpeg_video_format(video_output_get_format(video));

	config.color_range = voi->range == VIDEO_RANGE_FULL ? AVCOL_RANGE_JPEG
							    : AVCOL_RANGE_MPEG;
	config.colorspace = format_is_yuv(voi->format) ? AVCOL_SPC_BT709
						       : AVCOL_SPC_RGB;

	// Copied verbatim from obs-ffmpeg-output.c
	switch (voi->colorspace) {
	case VIDEO_CS_601:
		config.color_primaries = AVCOL_PRI_SMPTE170M;
		config.color_trc = AVCOL_TRC_SMPTE170M;
		config.colorspace = AVCOL_SPC_SMPTE170M;
		break;
	case VIDEO_CS_DEFAULT:
	case VIDEO_CS_709:
		config.color_primaries = AVCOL_PRI_BT709;
		config.color_trc = AVCOL_TRC_BT709;
		config.colorspace = AVCOL_SPC_BT709;
		break;
	case VIDEO_CS_SRGB:
		config.color_primaries = AVCOL_PRI_BT709;
		config.color_trc = AVCOL_TRC_IEC61966_2_1;
		config.colorspace = AVCOL_SPC_BT709;
		break;
	case VIDEO_CS_2100_PQ:
		config.color_primaries = AVCOL_PRI_BT2020;
		config.color_trc = AVCOL_TRC_SMPTE2084;
		config.colorspace = AVCOL_SPC_BT2020_NCL;
		break;
	case VIDEO_CS_2100_HLG:
		config.color_primaries = AVCOL_PRI_BT2020;
		config.color_trc = AVCOL_TRC_ARIB_STD_B67;
		config.colorspace = AVCOL_SPC_BT2020_NCL;
		break;
	}

	if (config.pixel_format == AV_PIX_FMT_NONE) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "pixel format was invalid.");
		return false;
	}

	if (!config.filepath || !*config.filepath) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "filepath was null or empty");
		return false;
	}

	cso->context.config = config;

	// Beginning of ffmpeg init
	const AVOutputFormat* output_format =
		av_guess_format("mp4", NULL, NULL);

	if (!output_format) {
		obs_log(LOG_ERROR, "Failed to start cordyceps stalk output; "
				   "could not get mp4 output format");
		return false;
	}

	avformat_alloc_output_context2(&cso->context.output_ctx, output_format,
				       NULL, cso->context.config.filepath);

	if (!cso->context.output_ctx) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "failed to create output context");
		return false;
	}

	if (cso->context.output_ctx->oformat->video_codec != AV_CODEC_ID_H264) {
		obs_log(LOG_ERROR, "Failed to start cordyceps stalk output; "
				   "output format video codec was not H264");
		return false;
	}

	// Init video output stream
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "no active videoo");
		return false;
	}

	cso->context.vcodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!cso->context.vcodec) {
		obs_log(LOG_ERROR, "Failed to start cordyceps stalk output; "
				   "failed to get H264 encoder");
		return false;
	}

	cso->context.video_stream = avformat_new_stream(
		cso->context.output_ctx,
		cso->context.vcodec);
	if (!cso->context.video_stream) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "failed to initialize video stream");
		return false;
	}

	// Init codec context
	enum AVPixelFormat closest_format = cso->context.config.pixel_format;
	if (cso->context.vcodec->pix_fmts)
		closest_format = avcodec_find_best_pix_fmt_of_list(
			cso->context.vcodec->pix_fmts, closest_format, 0, NULL);

	cso->context.video_ctx = avcodec_alloc_context3(cso->context.vcodec);
	cso->context.video_ctx->bit_rate = 0;
	cso->context.video_ctx->width = cso->context.config.width;
	cso->context.video_ctx->height = cso->context.config.height;
	cso->context.video_ctx->time_base = (AVRational) {(int) ovi.fps_den,
							 (int) ovi.fps_num};
	cso->context.video_ctx->framerate = (AVRational) {(int) ovi.fps_num,
							 (int) ovi.fps_den};
	cso->context.video_ctx->gop_size = cso->context.config.gop_size;
	cso->context.video_ctx->pix_fmt = closest_format;
	cso->context.video_ctx->color_range = cso->context.config.color_range;
	cso->context.video_ctx->color_primaries =
		cso->context.config.color_primaries;
	cso->context.video_ctx->color_trc = cso->context.config.color_trc;
	cso->context.video_ctx->colorspace = cso->context.config.colorspace;
	cso->context.video_ctx->chroma_sample_location =
		determine_chroma_location(closest_format,
					  cso->context.config.colorspace);
	cso->context.video_ctx->thread_count = 0;

	cso->context.video_stream->time_base =
		cso->context.video_ctx->time_base;
	cso->context.video_stream->avg_frame_rate =
		(AVRational) {(int) ovi.fps_num, (int) ovi.fps_den};

	// Might be unnecessary for my case but doesn't hurt to add
	if (cso->context.output_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		cso->context.video_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// Open video codec
	av_opt_set(cso->context.video_ctx->priv_data, "preset", preset, 0);
	av_opt_set_double(cso->context.video_ctx->priv_data, "crf", crf, 0);

	if (avcodec_open2(cso->context.video_ctx, cso->context.vcodec, NULL)
	    < 0) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "failed to open video codec");
		return false;
	}

	cso->context.vframe = av_frame_alloc();
	if (!cso->context.vframe) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "failed to allocate video frame");
		return false;
	}

	cso->context.vframe->format = cso->context.video_ctx->pix_fmt;
	cso->context.vframe->width = cso->context.video_ctx->width;
	cso->context.vframe->height = cso->context.video_ctx->height;
	cso->context.vframe->color_range = cso->context.config.color_range;
	cso->context.vframe->color_primaries =
		cso->context.config.color_primaries;
	cso->context.vframe->color_trc = cso->context.config.color_trc;
	cso->context.vframe->colorspace = cso->context.config.colorspace;
	cso->context.vframe->chroma_location = determine_chroma_location(
		cso->context.video_ctx->pix_fmt,
		cso->context.config.colorspace);

	if (av_frame_get_buffer(cso->context.vframe, base_get_alignment())
	    < 0) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "failed to allocate video frame buffer");
		return false;
	}

	avcodec_parameters_from_context(cso->context.video_stream->codecpar,
					cso->context.video_ctx);

	// Back to creating stream
	// Not sure what the following is for exactly but again, can't hurt to
	// add in case it's important
	const bool pq = cso->context.config.color_trc == AVCOL_TRC_SMPTE2084;
	const bool hlg = cso->context.config.color_trc ==
			 AVCOL_TRC_ARIB_STD_B67;

	if (pq || hlg) {
		const int hdr_nominal_peak_level =
			pq ? (int)obs_get_video_hdr_nominal_peak_level()
			   : (hlg ? 1000 : 0);

		size_t content_size;
		AVContentLightMetadata *const content =
			av_content_light_metadata_alloc(&content_size);
		content->MaxCLL = hdr_nominal_peak_level;
		content->MaxFALL = hdr_nominal_peak_level;

		av_packet_side_data_add(
			&cso->context.video_stream->codecpar->coded_side_data,
			&cso->context.video_stream->codecpar->
			 nb_coded_side_data,
			AV_PKT_DATA_CONTENT_LIGHT_LEVEL, (uint8_t*) content,
			content_size, 0);

		AVMasteringDisplayMetadata* const mastering =
			av_mastering_display_metadata_alloc();
		mastering->display_primaries[0][0] = av_make_q(17, 25);
		mastering->display_primaries[0][1] = av_make_q(8, 25);
		mastering->display_primaries[1][0] = av_make_q(53, 200);
		mastering->display_primaries[1][1] = av_make_q(69, 100);
		mastering->display_primaries[2][0] = av_make_q(3, 20);
		mastering->display_primaries[2][1] = av_make_q(3, 50);
		mastering->white_point[0] = av_make_q(3127, 10000);
		mastering->white_point[1] = av_make_q(329, 1000);
		mastering->min_luminance = av_make_q(0, 1);
		mastering->max_luminance = av_make_q(hdr_nominal_peak_level, 1);
		mastering->has_primaries = 1;
		mastering->has_luminance = 1;

		av_packet_side_data_add(
			&cso->context.video_stream->codecpar->coded_side_data,
			&cso->context.video_stream->codecpar->
			 nb_coded_side_data,
			AV_PKT_DATA_MASTERING_DISPLAY_METADATA,
			(uint8_t*) mastering, sizeof(*mastering), 0);
	}

	// Open output file
	if (avio_open2(&cso->context.output_ctx->pb,
		       cso->context.config.filepath, AVIO_FLAG_WRITE, NULL,
		       NULL) < 0) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "failed to open output filepath");
		return false;
	}

	if (avformat_write_header(cso->context.output_ctx, NULL) < 0) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "failed to write file header");
		return false;
	}

	cso->context.initialized = true;

	if (!obs_output_can_begin_data_capture(cso->output, 0)) {
		return false;
	}

	cso->active = true;

	if (pthread_create(&cso->write_thread, NULL, write_thread, cso) != 0) {
		obs_log(LOG_WARNING, "Failed to start cordyceps stalk output; "
				     "failed to create write thread");
		cso_stop_full(cso);
		return false;
	}

	obs_output_begin_data_capture(cso->output, 0);
	cso->write_thread_active = true;

	return true;
}

static void* start_thread(void* data)
{
	struct cso_data* cso = data;

	if (!init_ffmpeg(cso)) obs_output_signal_stop(cso->output,
				       OBS_OUTPUT_ERROR);

	// TODO: Possible memory leak on failure to init

	cso->starting = false;
	return NULL;
}

static const char* cso_get_name(void* unused)
{
	UNUSED_PARAMETER(unused);

	return "Cordyceps Stalk Output";
}

static void* cso_create(obs_data_t* settings, obs_output_t* output)
{
	UNUSED_PARAMETER(settings);

	struct cso_data* cso = bzalloc(sizeof(struct cso_data));

	cso->output = output;
	pthread_mutex_init(&cso->write_mutex, NULL);
	os_sem_init(&cso->write_semaphore, 0);
	os_event_init(&cso->stop_event, OS_EVENT_TYPE_AUTO);

	av_log_set_callback(ffmpeg_log);

	cso->realtime_mode = false;
	cso->requested_frames = 0;
	pthread_mutex_init(&cso->frame_request_mutex, NULL);

	proc_handler_t* ph = obs_output_get_proc_handler(cso->output);

	proc_handler_add(ph, "void set_realtime_mode(in bool value)",
			 proc_set_realtime_mode, cso);
	proc_handler_add(ph, "void get_realtime_mode(out bool value)",
			 proc_get_realtime_mode, cso);
	proc_handler_add(ph, "void request_frames(in int count)",
			 proc_request_frames, cso);

	return cso;
}

static void cso_destroy(void* data)
{
	struct cso_data* cso = data;

	if (cso) {
		if (cso->starting) pthread_join(cso->start_thread, NULL);

		cso_stop_full(cso);

		pthread_mutex_destroy(&cso->write_mutex);
		os_sem_destroy(cso->write_semaphore);
		os_event_destroy(cso->stop_event);

		pthread_mutex_destroy(&cso->frame_request_mutex);

		bfree(cso);
	}
}

static bool cso_start(void* data)
{
	struct cso_data* cso = data;

	if (cso->starting) return false;

	os_atomic_set_bool(&cso->stopping, false);
	cso->total_bytes = 0;

	int ret = pthread_create(&cso->start_thread, NULL, start_thread, cso);
	return (cso->starting = (ret == 0));
}

static void cso_stop(void* data, uint64_t stop_ts)
{
	struct cso_data* cso = data;

	if (os_atomic_load_bool(&cso->active))
	{
		if (stop_ts > 0) {
			os_atomic_set_bool(&cso->stopping, true);
		}

		cso_stop_full(cso);
	}
}

static void cso_get_frame(void* data, struct video_data* frame)
{
	struct cso_data* cso = data;

	bool quit_early = false;
	pthread_mutex_lock(&cso->frame_request_mutex);

	// total_bytes check is there so that at least one frame gets written
	if (!cso->realtime_mode && cso->total_bytes != 0) {
		if (cso->requested_frames > 0) {
			cso->requested_frames--;
		} else {
			quit_early = true;
		}
	}

	pthread_mutex_unlock(&cso->frame_request_mutex);
	if (quit_early) return;

	AVPacket* packet = NULL;
	int ret;
	int got_packet;

	if (av_frame_make_writable(cso->context.vframe) < 0) {
		obs_log(LOG_WARNING, "Cordyceps stalk output failed to get "
				     "writable vframe!");
		// Should probably stop the output if this happens but I don't
		// feel like it
		return;
	}

	// Copy frame data to AVFrame
	int h_chroma_shift;
	int v_chroma_shift;
	av_pix_fmt_get_chroma_sub_sample(cso->context.video_ctx->pix_fmt,
					 &h_chroma_shift, &v_chroma_shift);

	for (int plane = 0; plane < MAX_AV_PLANES; plane++) {
		if (!frame->data[plane]) continue;

		int frame_rowsize = (int) frame->linesize[plane];
		int pic_rowsize = cso->context.vframe->linesize[plane];
		int bytes = frame_rowsize < pic_rowsize ? frame_rowsize
							: pic_rowsize;
		int plane_height = cso->context.video_ctx->height
				   >> (plane ? v_chroma_shift : 0);

		for (int y = 0; y < plane_height; y++) {
			int pos_frame = y * frame_rowsize;
			int pos_pic = y * pic_rowsize;

			memcpy(cso->context.vframe->data[plane] + pos_pic,
			       frame->data[plane] + pos_frame, bytes);
		}
	}

	packet = av_packet_alloc();

	cso->context.vframe->pts = cso->context.total_frames;
	ret = avcodec_send_frame(cso->context.video_ctx, cso->context.vframe);
	if (ret == 0)
		ret = avcodec_receive_packet(cso->context.video_ctx, packet);

	got_packet = (ret == 0);

	if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) ret = 0;

	if (ret < 0) {
		obs_log(LOG_WARNING, "Cordyceps stalk output encode failure!");
		av_packet_free(&packet);
		// Should stop here but still don't feel like it
		return;
	}

	if (!ret && got_packet && packet->size) {
		packet->pts = rescale_ts(packet->pts, cso->context.video_ctx,
					 cso->context.video_stream->time_base);
		packet->dts = rescale_ts(packet->dts, cso->context.video_ctx,
					 cso->context.video_stream->time_base);
		packet->duration =
			(int) av_rescale_q(packet->duration,
					  cso->context.video_ctx->time_base,
					  cso->context.video_stream->time_base);

		pthread_mutex_lock(&cso->write_mutex);
		da_push_back(cso->packets, &packet);
		packet = NULL;
		pthread_mutex_unlock(&cso->write_mutex);
		os_sem_post(cso->write_semaphore);
	}

	cso->context.total_frames++;
}

static void cso_update(void* data, obs_data_t* settings)
{
	struct cso_data* cso = data;
	obs_data_t* cso_settings = obs_output_get_settings(cso->output);

	obs_data_set_string(cso_settings, "dirpath",
			    obs_data_get_string(settings, "dirpath"));
	obs_data_set_int(cso_settings, "gop_size",
			 obs_data_get_int(settings, "gop_size"));
	obs_data_set_double(cso_settings, "crf",
			    obs_data_get_double(settings, "crf"));
	obs_data_set_string(cso_settings, "preset",
			    obs_data_get_string(settings, "preset"));
}

static uint64_t cso_get_total_bytes(void* data)
{
	struct cso_data* cso = data;

	return cso->total_bytes;
}

static void proc_set_realtime_mode(void* data, calldata_t* cd)
{
	struct cso_data* cso = data;

	bool value = calldata_bool(cd, "value");

	pthread_mutex_lock(&cso->frame_request_mutex);
	cso->realtime_mode = value;
	pthread_mutex_unlock(&cso->frame_request_mutex);
}

static void proc_get_realtime_mode(void* data, calldata_t* cd)
{
	struct cso_data* cso = data;

	pthread_mutex_lock(&cso->frame_request_mutex);
	bool value = cso->realtime_mode;
	pthread_mutex_unlock(&cso->frame_request_mutex);

	calldata_set_bool(cd, "value", value);
}

static void proc_request_frames(void* data, calldata_t* cd)
{
	struct cso_data* cso = data;

	int64_t count = calldata_int(cd, "count");
	if (count < 0) count = 0;

	pthread_mutex_lock(&cso->frame_request_mutex);
	cso->requested_frames += count;
	pthread_mutex_unlock(&cso->frame_request_mutex);
}

struct obs_output_info cordyceps_stalk_output = {
	.id = "cordyceps-stalk-output",
	.flags = OBS_OUTPUT_VIDEO,
	.get_name = cso_get_name,
	.create = cso_create,
	.destroy = cso_destroy,
	.start = cso_start,
	.stop = cso_stop,
	.raw_video = cso_get_frame,
	.update = cso_update,
	.get_total_bytes = cso_get_total_bytes
};