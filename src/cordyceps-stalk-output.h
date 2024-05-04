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
};

void deactivate(struct cordyceps_stalk_output*, bool);
bool write_packet(struct cordyceps_stalk_output*, struct encoder_packet*);