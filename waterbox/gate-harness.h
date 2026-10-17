/* gate-harness.h - the run both harnesses share: run-native (the exports
 * compiled for the host) and run-wbx (core.wbx in the sandbox) parse the same
 * options, press the same controls on the same frames and print the same
 * digests, so their outputs diff directly. A difference between the flavors
 * is then the sandbox's doing, never the harness's.
 *
 * options: --frames N          run length (default 60)
 *          --report N          a digest line every N frames, and the last
 *          --press I:FIRST:N   hold panel control I for N frames from FIRST
 *          --axis I:V:FIRST:N  hold analog axis I at V for N frames from FIRST
 *          --exercise          a deterministic wander over the active
 *                              player controls (never Reset/Test/Service)
 *          --list-panel        print the panel, and which controls are live
 *          --list-dips         print the game's dip switches and their values
 *          --dump-domain NAME FILE   write a memory domain after the run;
 *                              NAME "bus:ADDRESS:LENGTH" is a part of the bus
 *          --poke ADDRESS:LENGTH:VALUE:FIRST:N  write VALUE to LENGTH bytes
 *                              of the bus before each of N frames from FIRST
 *          --vid-out FILE      the last picture: "W H\n" then BGRA rows
 *          --aud-out FILE      all the sound, 16-bit stereo samples
 *          --savedata-out DIR  the game's save data after the run, a file each
 * SPDX-License-Identifier: MIT
 */
#ifndef GATE_HARNESS_H
#define GATE_HARNESS_H

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct gate_core
{
	int (*init)(void);
	const char *(*load_error)(void);
	void (*set_button)(int32_t index, int32_t state);
	int (*button_count)(void);
	int (*button_active)(int32_t index);
	const char *(*button_name)(int32_t index);
	const char *(*describe)(void);   /* the game's dip switches, one per line */
	void (*frame)(void);
	int (*input_was_read)(void);
	int (*axis_count)(void);
	int (*axis_active)(int32_t index);
	void (*set_axis)(int32_t index, int32_t value);
	const uint32_t *(*video)(int *w, int *h);
	const int16_t *(*audio)(int *n);
	int (*domain_count)(void);
	const char *(*domain_name)(int i);
	const uint8_t *(*domain_ptr)(int i);
	int64_t (*domain_size)(int i);
	void (*pre_frame)(void); /* run-wbx's rerecord / session hook, or NULL */
	/* the first bus, for a machine whose memory is read through one: its live
	 * ranges as (address, length) pairs ended by a length of 0, and a read of
	 * up to 65536 bytes. Either may answer NULL: no bus. */
	const int64_t *(*bus_ranges)(void);
	const uint8_t *(*bus_read)(int64_t addr, int32_t len);
	void (*bus_poke)(int32_t addr, int32_t value);
	int (*save_count)(void);
	const char *(*save_name)(int i);
	int64_t (*save_size)(int i);
	const uint8_t *(*save_data)(int i);
};

#define GATE_MAX_PRESS 32
struct gate_opts
{
	long frames;
	long report;
	int presses;
	struct { int index; long first, count; } press[GATE_MAX_PRESS];
	int axes;
	struct { int index, value; long first, count; } axis[GATE_MAX_PRESS];
	int pokes;
	struct { long address, length; int value; long first, count; } poke[GATE_MAX_PRESS];
	int exercise;
	int listPanel;
	int listDips;
	const char *dumpDomain, *dumpPath;
	const char *vidOut;
	const char *audOut;
	const char *savedataOut;
};

static uint64_t gate_fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	if (!h) h = 1469598103934665603ULL;
	for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	return h;
}

static int gate_parse_opts(int argc, char **argv, int first, struct gate_opts *o)
{
	memset(o, 0, sizeof *o);
	o->frames = 60;
	o->report = 10;
	for (int i = first; i < argc; i++)
	{
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) o->frames = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--report") && i + 1 < argc) o->report = strtol(argv[++i], 0, 0);
		else if (!strcmp(argv[i], "--press") && i + 1 < argc && o->presses < GATE_MAX_PRESS)
		{
			int idx; long f, c;
			if (sscanf(argv[++i], "%d:%ld:%ld", &idx, &f, &c) != 3)
			{
				fprintf(stderr, "--press wants INDEX:FIRST:COUNT\n");
				return 0;
			}
			o->press[o->presses].index = idx;
			o->press[o->presses].first = f;
			o->press[o->presses].count = c;
			o->presses++;
		}
		else if (!strcmp(argv[i], "--axis") && i + 1 < argc && o->axes < GATE_MAX_PRESS)
		{
			int idx, val; long f, c;
			if (sscanf(argv[++i], "%d:%d:%ld:%ld", &idx, &val, &f, &c) != 4)
			{
				fprintf(stderr, "--axis wants INDEX:VALUE:FIRST:COUNT\n");
				return 0;
			}
			o->axis[o->axes].index = idx;
			o->axis[o->axes].value = val;
			o->axis[o->axes].first = f;
			o->axis[o->axes].count = c;
			o->axes++;
		}
		else if (!strcmp(argv[i], "--poke") && i + 1 < argc && o->pokes < GATE_MAX_PRESS)
		{
			long addr, len, f, c; int val;
			if (sscanf(argv[++i], "%li:%li:%i:%ld:%ld", &addr, &len, &val, &f, &c) != 5)
			{
				fprintf(stderr, "--poke wants ADDRESS:LENGTH:VALUE:FIRST:COUNT\n");
				return 0;
			}
			o->poke[o->pokes].address = addr;
			o->poke[o->pokes].length = len;
			o->poke[o->pokes].value = val;
			o->poke[o->pokes].first = f;
			o->poke[o->pokes].count = c;
			o->pokes++;
		}
		else if (!strcmp(argv[i], "--exercise")) o->exercise = 1;
		else if (!strcmp(argv[i], "--list-panel")) o->listPanel = 1;
		else if (!strcmp(argv[i], "--list-dips")) o->listDips = 1;
		else if (!strcmp(argv[i], "--dump-domain") && i + 2 < argc) { o->dumpDomain = argv[++i]; o->dumpPath = argv[++i]; }
		else if (!strcmp(argv[i], "--vid-out") && i + 1 < argc) o->vidOut = argv[++i];
		else if (!strcmp(argv[i], "--aud-out") && i + 1 < argc) o->audOut = argv[++i];
		else if (!strcmp(argv[i], "--savedata-out") && i + 1 < argc) o->savedataOut = argv[++i];
		else if (!strcmp(argv[i], "--rerecord") || !strcmp(argv[i], "--session") || !strcmp(argv[i], "--rewind")) ; /* run-wbx's */
		else
		{
			fprintf(stderr, "unknown option %s\n", argv[i]);
			return 0;
		}
	}
	if (o->report <= 0) o->report = o->frames;
	return 1;
}

/* The widest panel the harness drives (MAME knows about a thousand controls). */
#define GATE_MAX_BUTTONS 4096

/* Is this panel control one the exercise may press: a player's, a coin slot
 * or a start button (not the cabinet's Service, Tilt or volume switches)? */
static int gate_is_player(const struct gate_core *c, int i)
{
	const char *n = c->button_name(i);
	if (!n) return 0;
	const size_t len = strlen(n);
	return (n[0] == 'P' && n[1] >= '1' && n[1] <= '9' && n[2] == ' ')
		|| !strncmp(n, "Coin ", 5) || (len > 6 && !strcmp(n + len - 6, " Start"));
}

/* The exercise: an LCG picks, every 6 frames, one live control and holds it
 * for 3. Coins and Start are among them, so a run that exercises long
 * enough also starts a game. */
static void gate_exercise(const struct gate_core *c, long frame, uint8_t *held)
{
	static uint32_t seed = 12345;
	const int count = c->button_count();
	if (frame % 6 == 0)
	{
		memset(held, 0, GATE_MAX_BUTTONS);
		seed = seed * 1103515245u + 12345u;
		for (int tries = 0; tries < count; tries++)
		{
			const int i = (int)((seed >> 8) % (uint32_t)count);
			seed = seed * 1103515245u + 12345u;
			if (c->button_active(i) && gate_is_player(c, i)) { held[i] = 1; break; }
		}
	}
	else if (frame % 6 == 3)
		memset(held, 0, GATE_MAX_BUTTONS);
}

static uint64_t gate_ram_hash(const struct gate_core *c)
{
	uint64_t h = 0;
	for (int i = 0; i < c->domain_count(); i++)
		h = gate_fnv(h, c->domain_ptr(i), (size_t)c->domain_size(i));
	/* and what the bus's live ranges hold */
	const int64_t *ranges = c->bus_ranges ? c->bus_ranges() : NULL;
	for (; ranges && ranges[1] != 0; ranges += 2)
		for (int64_t at = 0; at < ranges[1]; at += 0x10000)
		{
			int32_t const len = (int32_t)(ranges[1] - at < 0x10000 ? ranges[1] - at : 0x10000);
			const uint8_t *bytes = c->bus_read(ranges[0] + at, len);
			if (bytes) h = gate_fnv(h, bytes, (size_t)len);
		}
	return h;
}

static int gate_run(const struct gate_core *c, const struct gate_opts *o)
{
	if (c->init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c->load_error());
		return 1;
	}
	const int count = c->button_count();
	if (o->listPanel)
	{
		for (int i = 0; i < count; i++)
			printf("panel %d '%s' %s\n", i, c->button_name(i), c->button_active(i) ? "active" : "-");
		for (int a = 0; a < c->axis_count(); a++)
			printf("axis %d %s\n", a, c->axis_active(a) ? "active" : "-");
	}
	if (o->listDips && c->describe)
		fputs(c->describe(), stdout);
	uint8_t held[GATE_MAX_BUTTONS] = {0};
	long lag = 0;
	FILE *aud = o->audOut ? fopen(o->audOut, "wb") : NULL;
	for (long f = 1; f <= o->frames; f++)
	{
		if (c->pre_frame) c->pre_frame();
		if (o->exercise) gate_exercise(c, f, held);
		/* the exercise moves every live axis too: a slow sweep, the same in
		 * both flavors */
		if (o->exercise)
			for (int a = 0; a < c->axis_count(); a++)
				if (c->axis_active(a))
					c->set_axis(a, (int32_t)((f * 37 + a * 311) % 2048) - 1024);
		for (int i = 0; i < count && i < GATE_MAX_BUTTONS; i++)
		{
			int on = held[i];
			for (int p = 0; p < o->presses; p++)
				if (o->press[p].index == i && f >= o->press[p].first && f < o->press[p].first + o->press[p].count)
					on = 1;
			c->set_button(i, on);
		}
		for (int a = 0; a < o->axes; a++)
		{
			const long first = o->axis[a].first, n = o->axis[a].count;
			if (f == first) c->set_axis(o->axis[a].index, o->axis[a].value);
			if (f == first + n) c->set_axis(o->axis[a].index, 0);
		}
		for (int p = 0; p < o->pokes; p++)
			if (c->bus_poke && f >= o->poke[p].first && f < o->poke[p].first + o->poke[p].count)
				for (long at = 0; at < o->poke[p].length; at++)
					c->bus_poke((int32_t)(o->poke[p].address + at), o->poke[p].value);
		c->frame();
		if (!c->input_was_read()) lag++;
		if (aud)
		{
			int n;
			const int16_t *a = c->audio(&n);
			fwrite(a, 4, (size_t)n, aud);
		}
		if (f % o->report == 0 || f == o->frames)
		{
			int w, h, n;
			const uint32_t *v = c->video(&w, &h);
			const int16_t *a = c->audio(&n);
			printf("frame %5ld ram %016" PRIx64 " vid %dx%d %016" PRIx64 " aud %d %016" PRIx64 " lag %ld\n", f,
			       gate_ram_hash(c), w, h, gate_fnv(0, v, (size_t)w * h * 4), n,
			       gate_fnv(0, a, (size_t)n * 4), lag);
			fflush(stdout);
		}
	}
	if (aud) fclose(aud);
	if (o->dumpDomain)
	{
		int found = 0;
		/* "bus:ADDRESS:LENGTH" is that part of the bus */
		if (!strncmp(o->dumpDomain, "bus:", 4) && c->bus_read)
		{
			char *end;
			int64_t const addr = strtoll(o->dumpDomain + 4, &end, 0);
			int32_t const len = *end == ':' ? (int32_t)strtol(end + 1, 0, 0) : 0;
			const uint8_t *bytes = len > 0 && len <= 0x10000 ? c->bus_read(addr, len) : NULL;
			FILE *f = bytes ? fopen(o->dumpPath, "wb") : NULL;
			if (f) { fwrite(bytes, 1, (size_t)len, f); fclose(f); found = 1; }
		}
		for (int i = 0; i < c->domain_count(); i++)
			if (!strcmp(c->domain_name(i), o->dumpDomain))
			{
				FILE *f = fopen(o->dumpPath, "wb");
				if (f) { fwrite(c->domain_ptr(i), 1, (size_t)c->domain_size(i), f); fclose(f); }
				found = 1;
			}
		if (!found) { fprintf(stderr, "no memory domain '%s'\n", o->dumpDomain); return 1; }
	}
	if (o->savedataOut)
		for (int i = 0; i < c->save_count(); i++)
		{
			char path[1024];
			snprintf(path, sizeof path, "%s/%s", o->savedataOut, c->save_name(i));
			FILE *f = fopen(path, "wb");
			if (f) { fwrite(c->save_data(i), 1, (size_t)c->save_size(i), f); fclose(f); }
		}
	if (o->vidOut)
	{
		int w, h;
		const uint32_t *v = c->video(&w, &h);
		FILE *f = fopen(o->vidOut, "wb");
		if (f) { fprintf(f, "%d %d\n", w, h); fwrite(v, 4, (size_t)w * h, f); fclose(f); }
	}
	return 0;
}

#endif
