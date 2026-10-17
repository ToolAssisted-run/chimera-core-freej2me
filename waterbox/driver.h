/* driver.h - the Java VM run a frame at a time (driver.c).
 * SPDX-License-Identifier: MIT */
#ifndef DRIVER_H
#define DRIVER_H

#include <stdint.h>

/* 44100 samples a second, 60 frames a second */
#define DRIVER_AUDIO_FRAMES 735

/* One more VM option, before driver_start: -Dname=value and the like. */
void driver_add_option(const char *opt);
/* Where the picture is copied to, and how many pixels it holds. */
void driver_set_video_buffer(uint32_t *buf, int pixels);
/* Starts the VM and the game: FreeJ2ME's settings as "name=value". 1 when
 * the game is running; else driver_error() says why. */
int driver_start(const char *classpath, const char *game, int width, int height, const char *const *settings, int nsettings);
/* Runs one frame: keys is a bit for each of FreeJ2ME's 20 keys, touch says
 * the screen is touched at x, y (pixels). 1 on success. */
int driver_frame(int keys, int touch, int x, int y);
const char *driver_error(void);
/* the last frame's picture, 0xAARRGGBB, and its 735 frames of 16-bit stereo */
const uint32_t *driver_video(int *w, int *h);
const int16_t *driver_audio(void);

#endif
