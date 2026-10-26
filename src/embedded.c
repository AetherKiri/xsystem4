/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reuse the upstream config parser without the standalone command-line loop. */
#define main xsystem4_cli_main
#include "system4.c"
#undef main
#include "embedded.h"
#include <setjmp.h>
#include <pthread.h>

void xsystem4_vm_shutdown(void);
void xsystem4_libraries_destroy(void);
void asset_manager_fini(void);
void audio_fini(void);
void gfx_font_fini(void);
void input_reset(void);

static _Thread_local const struct xsystem4_host *embedded_host;
static _Thread_local jmp_buf embedded_exit;
static _Thread_local int embedded_exit_code;
static _Thread_local int cleanup_stage;
static _Thread_local bool vm_running;

void xsystem4_host_vm_started(void) { vm_running = true; }
void xsystem4_host_vm_closed(void) { vm_running = false; }

void xsystem4_host_game_exit(int code)
{
	if (embedded_host && embedded_host->game_exit)
		embedded_host->game_exit(embedded_host->user, code);
}

_Noreturn void xsystem4_host_exit(int code)
{
	if (!embedded_host)
		pthread_exit(NULL);
	embedded_exit_code = code;
	longjmp(embedded_exit, 1);
}

void xsystem4_host_checkpoint(void)
{
	if (embedded_host && cleanup_stage == 0 &&
			embedded_host->checkpoint(embedded_host->user))
		xsystem4_host_exit(0);
}

int xsystem4_host_poll_event(SDL_Event *event)
{
	xsystem4_host_checkpoint();
	return embedded_host->poll_event(embedded_host->user, event);
}

void xsystem4_host_mouse_position(int *x, int *y, int set)
{
	embedded_host->mouse_position(embedded_host->user, x, y, set);
}

void xsystem4_host_text_input(int active)
{
	if (embedded_host && embedded_host->text_input)
		embedded_host->text_input(embedded_host->user, active);
}

const char *xsystem4_host_shader(const char *path)
{
	return embedded_host->shader(embedded_host->user, path);
}

int xsystem4_host_init_graphics(int width, int height)
{
	return embedded_host->init_graphics(embedded_host->user, width, height);
}

unsigned xsystem4_host_framebuffer(void)
{
	return embedded_host->framebuffer(embedded_host->user);
}

void xsystem4_host_present(int width, int height)
{
	embedded_host->present(embedded_host->user, width, height);
	xsystem4_host_checkpoint();
}

static void embedded_error(const char *message)
{
	if (embedded_host && embedded_host->log)
		embedded_host->log(embedded_host->user, message);
}

static void config_free(void)
{
	free(config.game_name);
	free(config.boot_name);
	free(config.ain_filename);
	free(config.vm_name);
	free(config.game_dir);
	free(config.save_dir);
	free(config.home_dir);
	free(config.bgi_path);
	free(config.wai_path);
	free(config.ex_path);
	free(config.fnl_path);
	for (int i = 0; i < 2; i++) free(config.font_paths[i]);
	for (size_t i = 0; i < config.mixer_nr_channels; i++) free(config.mixer_channels[i]);
	free(config.mixer_channels);
	free(config.mixer_volumes);
	free(ini_save_folder);
	ini_save_folder = NULL;
	config = (struct config){ .view_width = 800, .view_height = 600,
		.default_volume = 100, .text_x_scale = 1.0f, .save_format = SAVE_FORMAT_RSM };
}

static bool embedded_config(const char *game)
{
	const char *names[] = { "System40.ini", "AliceStart.ini" };
	for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++) {
		char *candidate = path_join(game, names[i]);
		char *path = path_get_icase(candidate);
		free(candidate);
		if (!path) continue;
		bool loaded = config_init_with_ini(path);
		free(path);
		return loaded;
	}
	return false;
}

int xsystem4_host_run(const struct xsystem4_host *host, const char *game,
		const char *save_directory, const char *font, const char *encoding)
{
	embedded_host = host;
	embedded_exit_code = 0;
	cleanup_stage = 0;
	vm_running = false;
	sys_error_handler = embedded_error;
	use_gbk = false;
	if (!setjmp(embedded_exit)) {
		if (!embedded_config(game))
			ERROR("No System40.ini or AliceStart.ini in %s", game);
		if (config.view_width < 1 || config.view_height < 1 ||
				config.view_width > 8192 || config.view_height > 8192)
			ERROR("Invalid game resolution");
		free(config.home_dir);
		config.home_dir = xstrdup(save_directory);
		read_user_config();
		if (encoding && *encoding && !set_encoding(encoding))
			ERROR("Unsupported text encoding: %s", encoding);
		free(config.save_dir);
		config.save_dir = xstrdup(save_directory);
		if (font && *font) {
			for (int i = 0; i < 2; i++) {
				if (!config.font_paths[i]) config.font_paths[i] = xstrdup(font);
			}
		}
		int error;
		char *path = gamedir_path_icase(config.ain_filename);
		if (!path) ERROR("AIN script does not exist: %s", config.ain_filename);
		ain = ain_open(path, &error);
		free(path);
		if (!ain) ERROR("%s", ain_strerror(error));
		mkdir_p(config.save_dir);
		apply_game_specific_hacks(ain);
		asset_manager_init();
		embedded_exit_code = vm_execute_ain(ain);
		xsystem4_host_game_exit(embedded_exit_code);
	}
	/* Advance before each cleanup: a fatal module finalizer must not recurse. */
	while (cleanup_stage < 8) {
		switch (cleanup_stage++) {
		case 0:
			if (vm_running) { vm_running = false; xsystem4_vm_shutdown(); }
			break;
		case 1: audio_fini(); break;
		case 2: xsystem4_libraries_destroy(); break;
		case 3: gfx_font_fini(); break;
		case 4: gfx_fini(); break;
		case 5: asset_manager_fini(); break;
		case 6: if (ain) ain_free(ain); ain = NULL; break;
		case 7: config_free(); input_reset(); break;
		}
	}
	embedded_host = NULL;
	sys_error_handler = NULL;
	return embedded_exit_code;
}
