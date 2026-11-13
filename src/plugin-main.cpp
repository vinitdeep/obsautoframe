// V Media AI AutoFrame - OBS module entry points.
// Copyright (C) 2026 V Media Production.
//
// This program is free software; you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
#include "autoframe-filter.hpp"
#include "person-detector.hpp"

#include <obs-module.h>

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "0.0.0"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("vmedia-ai-autoframe", "en-US")
OBS_MODULE_AUTHOR("V Media Production")

const char *obs_module_name(void)
{
	return "V Media AI AutoFrame";
}

const char *obs_module_description(void)
{
	return "AI auto-framing filter: keeps a preacher, presenter or performer framed with digital pan, tilt "
	       "and zoom. By V Media Production.";
}

bool obs_module_load(void)
{
	vmaf::registerAutoFrameFilter();
	blog(LOG_INFO, "[vmedia-autoframe] V Media AI AutoFrame %s loaded (built for libobs %d.%d.%d)",
	     PLUGIN_VERSION, LIBOBS_API_MAJOR_VER, LIBOBS_API_MINOR_VER, LIBOBS_API_PATCH_VER);
	return true;
}

void obs_module_unload(void)
{
	vmaf::shutdownOrtRuntime();
	blog(LOG_INFO, "[vmedia-autoframe] unloaded");
}
