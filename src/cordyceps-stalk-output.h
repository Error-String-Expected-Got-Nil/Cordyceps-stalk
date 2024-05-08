// Mostly copied from obs-ffmpeg-output.h

#pragma once

#include <obs-module.h>
#include <plugin-support.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/mastering_display_metadata.h>
#include <util/threading.h>

#include "include/obs-ffmpeg-formats.h"

struct ffmpeg_config {
	const char* filepath;
	int gop_size; // Also known as keyframe interval
	int width;
	int height;

	enum AVPixelFormat pixel_format;
	enum AVColorRange color_range;
	enum AVColorPrimaries color_primaries;
	enum AVColorTransferCharacteristic color_trc;
	enum AVColorSpace colorspace;
};

struct ffmpeg_context {
	AVStream* video_stream;
	AVCodecContext* video_ctx;
	const AVCodec* vcodec;
	AVFormatContext* output_ctx;

	AVFrame* vframe;
	int64_t total_frames;

	struct ffmpeg_config config;

	bool initialized;
};

struct cso_data {
	obs_output_t* output;

	struct ffmpeg_context context;

	bool starting;
	pthread_t start_thread;

	volatile bool active;
	volatile bool stopping;

	uint64_t total_bytes;

	bool write_thread_active;
	pthread_mutex_t write_mutex;
	os_sem_t* write_semaphore;
	os_event_t* stop_event;
	pthread_t write_thread;

	DARRAY(AVPacket*) packets;
};