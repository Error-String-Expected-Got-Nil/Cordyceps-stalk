#pragma once

#include <obs-module.h>
#include <plugin-support.h>
#include <util/pipe.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/dstr.h>

#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>

#include "../include/obs-ffmpeg-formats.h"
#include "../include/ffmpeg-mux.h"

struct cordyceps_stalk_output {
	obs_output_t* output;
	os_process_pipe_t* pipe;

	int64_t stop_ts;
	uint64_t total_bytes;
	bool sent_headers;

	volatile bool active;
	volatile bool capturing;
	volatile bool stopping;

	struct dstr path;

	volatile bool realtime_mode;
	volatile uint64_t requested_frames;
	volatile int32_t written_frames;
	pthread_mutex_t mutex;
};

void deactivate(struct cordyceps_stalk_output*, bool);
bool write_packet(struct cordyceps_stalk_output*, struct encoder_packet*);
void ph_set_path(void* data, calldata_t* cd);
void ph_request_frames(void* data, calldata_t* cd);
void ph_set_realtime_mode(void* data, calldata_t* cd);
void ph_get_written_frame_count(void* data, calldata_t* cd);