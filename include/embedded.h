/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native embedding hooks. The host owns its window, GL context and input queue. */
#ifndef XSYSTEM4_EMBEDDED_H
#define XSYSTEM4_EMBEDDED_H

#include <stdint.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

struct xsystem4_host {
	void *user;
	int (*init_graphics)(void *user, int width, int height);
	unsigned (*framebuffer)(void *user);
	void (*present)(void *user, int width, int height);
	int (*poll_event)(void *user, SDL_Event *event);
	void (*mouse_position)(void *user, int *x, int *y, int set);
	int (*checkpoint)(void *user);
	const char *(*shader)(void *user, const char *path);
	void (*log)(void *user, const char *message);
	void (*text_input)(void *user, int active);
	// Normal script completion, including nonzero process exit codes.
	void (*game_exit)(void *user, int code);
};

int xsystem4_host_run(const struct xsystem4_host *host, const char *game,
		const char *save_directory, const char *font, const char *encoding);
void xsystem4_host_checkpoint(void);
int xsystem4_host_poll_event(SDL_Event *event);
void xsystem4_host_mouse_position(int *x, int *y, int set);
const char *xsystem4_host_shader(const char *path);
int xsystem4_host_init_graphics(int width, int height);
unsigned xsystem4_host_framebuffer(void);
void xsystem4_host_present(int width, int height);
void xsystem4_host_text_input(int active);
void xsystem4_host_vm_started(void);
void xsystem4_host_vm_closed(void);
void xsystem4_host_game_exit(int code);
#ifdef __cplusplus
[[noreturn]] void xsystem4_host_exit(int code);
#else
_Noreturn void xsystem4_host_exit(int code);
#endif

#ifdef __cplusplus
}
#endif
#endif
