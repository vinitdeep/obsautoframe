/* macOS plugin load test: boots the REAL libobs from an OBS.app, loads the built plugin bundle
 * exactly the way OBS does, and checks it registers its filter, defaults and settings UI.
 *
 * The binary must sit in <OBS.app>/Contents/MacOS/ so "@executable_path/../Frameworks" resolves
 * like it does for OBS itself (package-macos.sh copies it there).
 *
 *   macos-load-check <plugin.plugin>
 *
 * No OBS headers needed: the handful of libobs entry points are declared here. */
#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef bool (*startup_fn)(const char *, const char *, void *);
typedef void (*shutdown_fn)(void);
typedef const char *(*version_fn)(void);
typedef int (*open_module_fn)(void **, const char *, const char *);
typedef bool (*init_module_fn)(void *);
typedef bool (*enum_types_fn)(size_t, const char **);
typedef void *(*get_defaults_fn)(const char *);
typedef void (*data_release_fn)(void *);
typedef double (*data_get_double_fn)(void *, const char *);
typedef void *(*get_props_fn)(const char *);
typedef void *(*props_get_fn)(void *, const char *);
typedef void (*props_destroy_fn)(void *);
typedef long (*bnum_allocs_fn)(void);

static void *sym(void *lib, const char *name, int *fail)
{
	void *p = dlsym(lib, name);
	if (!p) {
		printf("FAIL: libobs has no %s\n", name);
		++*fail;
	}
	return p;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <vmedia-ai-autoframe.plugin>\n", argv[0]);
		return 2;
	}
	char bin[4096], data[4096];
	snprintf(bin, sizeof(bin), "%s/Contents/MacOS/vmedia-ai-autoframe", argv[1]);
	snprintf(data, sizeof(data), "%s/Contents/Resources", argv[1]);

	void *obs = dlopen("@executable_path/../Frameworks/libobs.framework/libobs", RTLD_NOW | RTLD_GLOBAL);
	if (!obs) {
		printf("FAIL: cannot load libobs: %s\n", dlerror());
		return 1;
	}
	int fail = 0;
	startup_fn obs_startup = (startup_fn)sym(obs, "obs_startup", &fail);
	shutdown_fn obs_shutdown = (shutdown_fn)sym(obs, "obs_shutdown", &fail);
	version_fn obs_get_version_string = (version_fn)sym(obs, "obs_get_version_string", &fail);
	open_module_fn obs_open_module = (open_module_fn)sym(obs, "obs_open_module", &fail);
	init_module_fn obs_init_module = (init_module_fn)sym(obs, "obs_init_module", &fail);
	enum_types_fn obs_enum_filter_types = (enum_types_fn)sym(obs, "obs_enum_filter_types", &fail);
	get_defaults_fn obs_get_source_defaults = (get_defaults_fn)sym(obs, "obs_get_source_defaults", &fail);
	data_release_fn obs_data_release = (data_release_fn)sym(obs, "obs_data_release", &fail);
	data_get_double_fn obs_data_get_double = (data_get_double_fn)sym(obs, "obs_data_get_double", &fail);
	get_props_fn obs_get_source_properties = (get_props_fn)sym(obs, "obs_get_source_properties", &fail);
	props_get_fn obs_properties_get = (props_get_fn)sym(obs, "obs_properties_get", &fail);
	props_destroy_fn obs_properties_destroy = (props_destroy_fn)sym(obs, "obs_properties_destroy", &fail);
	bnum_allocs_fn bnum_allocs = (bnum_allocs_fn)sym(obs, "bnum_allocs", &fail);
	if (fail)
		return 1;

	printf("libobs %s\n", obs_get_version_string());
	if (!obs_startup("en-US", NULL, NULL)) {
		printf("FAIL: obs_startup\n");
		return 1;
	}

	void *module = NULL;
	const int rc = obs_open_module(&module, bin, data);
	if (rc != 0 || !module) {
		printf("FAIL: obs_open_module(%s) returned %d\n", bin, rc);
		obs_shutdown();
		return 1;
	}
	if (!obs_init_module(module)) {
		printf("FAIL: obs_init_module (obs_module_load returned false or API version rejected)\n");
		obs_shutdown();
		return 1;
	}
	printf("PASS: plugin opened and initialised by libobs\n");

	const char *id = NULL;
	bool found = false;
	for (size_t i = 0; obs_enum_filter_types(i, &id); ++i)
		if (id && strcmp(id, "vmedia_ai_autoframe_filter") == 0)
			found = true;
	if (found)
		printf("PASS: filter 'vmedia_ai_autoframe_filter' registered\n");
	else {
		printf("FAIL: filter not registered\n");
		++fail;
	}

	void *defaults = obs_get_source_defaults("vmedia_ai_autoframe_filter");
	if (defaults) {
		printf("PASS: defaults (max_zoom = %.2f)\n", obs_data_get_double(defaults, "max_zoom"));
		obs_data_release(defaults);
	} else {
		printf("FAIL: no defaults\n");
		++fail;
	}

	void *props = obs_get_source_properties("vmedia_ai_autoframe_filter");
	if (props && obs_properties_get(props, "status_info")) {
		printf("PASS: settings UI builds\n");
	} else {
		printf("FAIL: settings UI missing\n");
		++fail;
	}
	if (props)
		obs_properties_destroy(props);

	obs_shutdown();
	const long leaks = bnum_allocs();
	printf("%s: %ld memory leak(s) reported by libobs\n", leaks ? "FAIL" : "PASS", leaks);
	if (leaks)
		++fail;

	printf("\n%s\n", fail ? "MACOS LOAD CHECK FAILED" : "MACOS LOAD CHECK PASSED");
	return fail ? 1 : 0;
}
