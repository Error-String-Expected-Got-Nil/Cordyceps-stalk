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

// Acronym notes:
// csv = "cordyceps stalk vendor"
// cso = "cordyceps stalk output"
// csvr = "cordyceps stalk vendor request"

#include <obs-module.h>
#include <plugin-support.h>
#include "include/obs-websocket-api.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

obs_websocket_vendor csv;
obs_output_t* cso;

extern struct obs_output_info cordyceps_stalk_output;

bool obs_module_load()
{
	obs_register_output(&cordyceps_stalk_output);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)",
		PLUGIN_VERSION);
	return true;
}

void csvr_status(obs_data_t* request, obs_data_t* response, void* priv);
void csvr_get_fps(obs_data_t* request, obs_data_t* response, void* priv);
void csvr_update_settings(obs_data_t* request, obs_data_t* response,
			  void* priv);

void obs_module_post_load()
{
	obs_data_t* cso_settings = obs_data_create();
	obs_data_set_string(cso_settings, "dirpath", "C:/cordyceps/");
	obs_data_set_int(cso_settings, "gop_size", 120);
	obs_data_set_double(cso_settings, "crf", 23.0);
	obs_data_set_string(cso_settings, "preset", "veryfast");

	cso = obs_output_create("cordyceps-stalk-output",
				"cordyceps_stalk_main", cso_settings, NULL);

	csv = obs_websocket_register_vendor("cordyceps_stalk");

	obs_websocket_vendor_register_request(csv, "get_fps", csvr_get_fps,
					      cso);
	obs_websocket_vendor_register_request(csv, "update_settings",
					      csvr_update_settings, cso);

	obs_websocket_vendor_register_request(csv, "status", csvr_status, NULL);
}

void csvr_status(obs_data_t* request, obs_data_t* response, void* priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(priv);

	obs_log(LOG_INFO, "Cordyceps-stalk status requested");

	obs_data_set_bool(response, "active", true);
}

void csvr_get_fps(obs_data_t* request, obs_data_t* response, void* priv)
{
	UNUSED_PARAMETER(request);

	obs_output_t* output = priv;
	const struct video_output_info* voi =
		video_output_get_info(obs_output_video(output));

	obs_data_set_int(response, "fps_num", voi->fps_num);
	obs_data_set_int(response, "fps_den", voi->fps_den);
}

void csvr_update_settings(obs_data_t* request, obs_data_t* response,
			  void* priv)
{
	UNUSED_PARAMETER(response);

	obs_output_t* output = priv;

	const char* dirpath = obs_data_get_string(request, "dirpath");
	int gop_size = (int) obs_data_get_int(request, "gop_size");
	double crf = obs_data_get_double(request, "crf");
	const char* preset = obs_data_get_string(request, "preset");

	obs_log(LOG_INFO, "Got settings update request: dirpath = \"%s\", "
			  "gop_size = %d, crf = %f, preset = \"%s\"",
		dirpath, gop_size, crf, preset);

	obs_output_update(output, request);
}

void obs_module_unload()
{
	obs_log(LOG_INFO, "plugin unloaded");
}
