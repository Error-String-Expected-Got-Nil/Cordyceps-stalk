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
// csvc = "cordyceps stalk vendor callback"

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

void csvc_record_start_success(void* data, calldata_t* cd);
void csvc_record_start_fail(void* data, calldata_t* cd);
void csvr_status(obs_data_t* request, obs_data_t* response, void* priv);
void csvr_update_settings(obs_data_t* request, obs_data_t* response,
			  void* priv);
void csvr_start_recording(obs_data_t* request, obs_data_t* response,
			  void* priv);
void csvr_stop_recording(obs_data_t* request, obs_data_t* response,
			 void* priv);
void csvr_set_realtime_mode(obs_data_t* request, obs_data_t* response,
			    void* priv);
void csvr_request_frames(obs_data_t* request, obs_data_t* response,
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

	signal_handler_t* sh = obs_output_get_signal_handler(cso);
	signal_handler_connect(sh, "activate", csvc_record_start_success, &csv);
	signal_handler_connect(sh, "stop", csvc_record_start_fail, &csv);

	obs_websocket_vendor_register_request(csv, "update_settings",
					      csvr_update_settings, cso);
	obs_websocket_vendor_register_request(csv, "start_recording",
					      csvr_start_recording, cso);
	obs_websocket_vendor_register_request(csv, "stop_recording",
					      csvr_stop_recording, cso);
	obs_websocket_vendor_register_request(csv, "set_realtime_mode",
					      csvr_set_realtime_mode, cso);
	obs_websocket_vendor_register_request(csv, "request_frames",
					      csvr_request_frames, cso);

	obs_websocket_vendor_register_request(csv, "status", csvr_status, NULL);
}

void csvc_record_start_success(void* data, calldata_t* cd)
{
	UNUSED_PARAMETER(cd);

	obs_websocket_vendor* vendor = data;

	obs_websocket_vendor_emit_event(*vendor, "record_start_success", NULL);
}

void csvc_record_start_fail(void* data, calldata_t* cd)
{
	obs_websocket_vendor* vendor = data;

	int64_t code = calldata_int(cd, "code");

	// OBS_OUTPUT_CONNECT_FAILED should only happen if the output causes it
	// explicitly itself, so it's being used here to indicate a failure to
	// start the encoder specifically
	if (code == OBS_OUTPUT_CONNECT_FAILED) {
		obs_websocket_vendor_emit_event(*vendor, "record_start_fail",
						NULL);
	}
}

void csvr_status(obs_data_t* request, obs_data_t* response, void* priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(priv);

	obs_log(LOG_INFO, "Cordyceps-stalk status requested");

	obs_data_set_bool(response, "active", true);
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

// Note that a 'true' return from obs_output_start() does NOT actually mean
// that the output started successfully in this case, since the Cordyceps-stalk
// output initializes in a thread. A 'true' return just means the thread was
// made successfully. Signals are used to send a vendor event when success
// is *actually* confirmed by the output.
void csvr_start_recording(obs_data_t* request, obs_data_t* response,
			  void* priv)
{
	UNUSED_PARAMETER(request);

	obs_output_t* output = priv;

	obs_log(LOG_INFO, "Cordyceps-stalk received record start request");

	bool success = obs_output_start(output);

	obs_data_set_bool(response, "success", success);
}

void csvr_stop_recording(obs_data_t* request, obs_data_t* response,
			 void* priv)
{
	UNUSED_PARAMETER(request);
	UNUSED_PARAMETER(response);

	obs_output_t* output = priv;

	obs_output_stop(output);
}

void csvr_set_realtime_mode(obs_data_t* request, obs_data_t* response,
			    void* priv)
{
	UNUSED_PARAMETER(response);

	obs_output_t* output = priv;
	proc_handler_t* ph = obs_output_get_proc_handler(output);
	calldata_t* cd = calldata_create();
	calldata_set_bool(cd, "value", obs_data_get_bool(request, "value"));
	proc_handler_call(ph, "set_realtime_mode", cd);
	calldata_destroy(cd);
}

void csvr_request_frames(obs_data_t* request, obs_data_t* response,
			 void* priv)
{
	UNUSED_PARAMETER(response);

	obs_output_t* output = priv;
	proc_handler_t* ph = obs_output_get_proc_handler(output);
	calldata_t* cd = calldata_create();
	calldata_set_int(cd, "count", obs_data_get_int(request, "count"));
	proc_handler_call(ph, "request_frames", cd);
	calldata_destroy(cd);
}

void obs_module_unload()
{
	obs_output_release(cso);
}
