// Prints every libobs ABI detail the plugin depends on. scripts/abi-check.ps1 compiles it
// against two OBS header versions and diffs the output: identical output means a plugin
// built against the older headers runs correctly on the newer OBS.
#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/base.h>

#include <cstddef>
#include <cstdio>

#define V(x) printf("%-42s %lld\n", #x, (long long)(x))
#define O(s, f) printf("%-42s %zu\n", "offsetof(" #s "," #f ")", offsetof(s, f))

int main()
{
	// struct obs_source_info: every member the filter fills in
	O(struct obs_source_info, id);
	O(struct obs_source_info, type);
	O(struct obs_source_info, output_flags);
	O(struct obs_source_info, get_name);
	O(struct obs_source_info, create);
	O(struct obs_source_info, destroy);
	O(struct obs_source_info, get_width);
	O(struct obs_source_info, get_height);
	O(struct obs_source_info, get_defaults);
	O(struct obs_source_info, get_properties);
	O(struct obs_source_info, update);
	O(struct obs_source_info, video_tick);
	O(struct obs_source_info, video_render);
	O(struct obs_source_info, video_get_color_space);
	V(sizeof(struct vec2));
	V(sizeof(struct vec4));
	// enums / flags
	V(OBS_SOURCE_TYPE_FILTER);
	V(OBS_SOURCE_VIDEO);
	V(OBS_SOURCE_ASYNC);
	V(OBS_SOURCE_CUSTOM_DRAW);
	V(OBS_SOURCE_SRGB);
	V(OBS_EFFECT_DEFAULT);
	V(OBS_EFFECT_SOLID);
	V(OBS_EFFECT_AREA);
	V(GS_RGBA);
	V(GS_BGRA);
	V(GS_RGBA16F);
	V(GS_ZS_NONE);
	V(GS_CS_SRGB);
	V(GS_CS_SRGB_16F);
	V(GS_CS_709_EXTENDED);
	V(GS_CLEAR_COLOR);
	V(GS_TRIS);
	V(GS_BLEND_ONE);
	V(GS_BLEND_SRCALPHA);
	V(GS_BLEND_INVSRCALPHA);
	V(OBS_TEXT_INFO);
	V(OBS_TEXT_INFO_NORMAL);
	V(OBS_TEXT_INFO_WARNING);
	V(OBS_TEXT_INFO_ERROR);
	V(OBS_GROUP_NORMAL);
	V(OBS_COMBO_TYPE_LIST);
	V(OBS_COMBO_FORMAT_INT);
	V(OBS_PATH_FILE);
	V(OBS_INVALID_HOTKEY_ID);
	V(LOG_ERROR);
	V(LOG_WARNING);
	V(LOG_INFO);
	printf("%-42s %d.%d\n", "LIBOBS_API (built against)", LIBOBS_API_MAJOR_VER, LIBOBS_API_MINOR_VER);
	return 0;
}
