// V Media AI AutoFrame - OBS settings binding and the properties (settings) UI.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include "settings.hpp"

#include <obs-module.h>

namespace vmaf {

struct AutoFrameFilter;

/* Default values written to OBS (these are what "Defaults" in the filter dialog restores). */
void writeDefaults(obs_data_t *settings);

/* OBS settings -> plain config. UI units (percent, seconds) are converted here. */
AutoFrameConfig readConfig(obs_data_t *settings, bool &bicubic);

/* Track id chosen in the "Select person" list (-1 = automatic). */
int manualPersonSetting(obs_data_t *settings);

/* Builds the filter's settings dialog. `f` may be null (e.g. when OBS lists defaults). */
obs_properties_t *buildProperties(AutoFrameFilter *f);

} // namespace vmaf
