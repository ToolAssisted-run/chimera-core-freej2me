/* core.c - FreeJ2ME-Plus as a Chimera core: the functions Chimera calls.
 *
 * FreeJ2ME-Plus is written in Java. The core is a Java VM (OpenJDK's Zero,
 * the interpreter-only HotSpot, linked in whole) running FreeJ2ME and a
 * small front end of ours (java/chimera/Frontend.java), driven a frame at a
 * time by driver.c. This file reads the project (the game, the settings),
 * starts the VM with the options the machine needs, turns Chimera's
 * controls into FreeJ2ME's keys and touches, and hands back the picture and
 * the sound.
 *
 * The same file is compiled for the sandbox (CHIMERA_GUEST) and for the
 * native runner, which links the host's own Zero VM.
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "driver.h"

#ifdef CHIMERA_GUEST
#include "memfs.h"
/* spec 3: the machine's own clock and timed waits (miniBox MACHINE-SPEC) */
__attribute__((visibility("default"))) __attribute__((used)) const uint32_t __wbx_machine_spec = 3;
/* the pack of the runtime's files, built in by pack.S */
extern const unsigned char chimera_pack[], chimera_pack_end[];
#define CORE_HOME "/jdk"
#define CORE_CLASSPATH "/chimera/freej2me.jar:/chimera/frontend.jar"
#endif
/* the native runner names its own (run-native.c's build) */
#ifndef CORE_HOME
#error CORE_HOME: where the Java runtime is
#endif

static char g_loadError[1024];

/* ---- the controls -------------------------------------------------------
 * A phone's keys, in the order Chimera shows them, each with the index
 * FreeJ2ME gives it (Mobile.getMobileKey; the Phone Keys setting turns
 * those into each maker's key codes), and the touch screen. */
static const struct { const char *name; int fj; } kKeys[] = {
	{ "Up", 0 }, { "Down", 1 }, { "Left", 2 }, { "Right", 3 }, { "Fire", 7 },
	{ "Left Soft", 9 }, { "Right Soft", 8 },
	{ "Num 1", 10 }, { "Num 2", 14 }, { "Num 3", 11 }, { "Num 4", 15 }, { "Num 5", 18 },
	{ "Num 6", 16 }, { "Num 7", 5 }, { "Num 8", 17 }, { "Num 9", 4 },
	{ "Star", 12 }, { "Num 0", 6 }, { "Pound", 13 }, { "Clear", 19 },
};
#define KEY_COUNT ((int)(sizeof kKeys / sizeof kKeys[0]))
#define BUTTON_TOUCH KEY_COUNT
#define BUTTON_COUNT (KEY_COUNT + 1)
static int g_held[BUTTON_COUNT];

/* where the screen is touched, a fraction of the picture from 0 to 65535 */
enum { kTouchX, kTouchY, kAxes };
static const char *const kAxisNames[kAxes] = { "Touch X", "Touch Y" };
static int g_axis[kAxes] = { 32768, 32768 };

/* ---- the picture and the sound ---- */
#define MAX_PIXELS (1024 * 1024)
static uint32_t *g_picture;
static int g_render = 1;
static int g_running;
static int16_t g_silence[DRIVER_AUDIO_FRAMES * 2];

/* the memory domain (below) */
static void find_heap(void);

/* ---- the settings ------------------------------------------------------- */
static void setting(const char *name, const char *otherwise, char *out, int size)
{
	if (wbx_setting_str(name, out, size) <= 0)
		snprintf(out, (size_t)size, "%s", otherwise);
}

static int ends_with(const char *s, const char *tail)
{
	const size_t n = strlen(s), t = strlen(tail);
	return n >= t && strcasecmp(s + n - t, tail) == 0;
}

/* ---- Chimera's calls ---------------------------------------------------- */
ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }
ECL_EXPORT const char *SuggestSettings(void) { return "{\"values\":{},\"note\":\"\",\"firmware\":[]}"; }
ECL_EXPORT const char *DescribeDips(void) { return ""; }

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';
#ifdef CHIMERA_GUEST
	if (!memfs_mount_image(chimera_pack, (size_t)(chimera_pack_end - chimera_pack))) {
		snprintf(g_loadError, sizeof g_loadError, "the core's own files are damaged");
		return 0;
	}
	/* the settings and the slot map, where the readers below look for them */
	memfs_import("settings", "/settings");
	memfs_import("slots", "/slots");
#endif

	/* the game: every file of the slot, under its own name. A jar is what
	 * runs; a descriptor beside it (.jad) is found by FreeJ2ME itself. */
	static char names[8][256];
	int count = wbx_slot_count("game");
	if (count > 8)
		count = 8;
	for (int i = 0; i < count; i++)
		wbx_slot_name("game", i, names[i], sizeof names[i]);
	if (count == 0) {
		/* a game opened with no project is mounted under its own name */
		FILE *f = fopen("rom.name", "rb");
#ifdef CHIMERA_GUEST
		if (!f && memfs_import("rom.name", "/rom.name"))
			f = fopen("rom.name", "rb");
#endif
		if (f) {
			const size_t got = fread(names[0], 1, sizeof names[0] - 1, f);
			names[0][got] = '\0';
			fclose(f);
			count = names[0][0] ? 1 : 0;
		}
	}
	const char *game = NULL;
	for (int pass = 0; pass < 3 && !game; pass++)
		for (int i = 0; i < count && !game; i++)
			if ((pass == 0 && ends_with(names[i], ".jar")) || (pass == 1 && ends_with(names[i], ".kjx"))
				|| (pass == 2 && ends_with(names[i], ".jad")))
				game = names[i];
	if (!game) {
		snprintf(g_loadError, sizeof g_loadError, count ? "the game slot holds no .jar, .jad or .kjx file" : "no game was given");
		return 0;
	}
#ifdef CHIMERA_GUEST
	for (int i = 0; i < count; i++) {
		char at[300];
		snprintf(at, sizeof at, "/%.256s", names[i]);
		if (!memfs_import(names[i], at)) {
			snprintf(g_loadError, sizeof g_loadError, "the file %.256s could not be read", names[i]);
			return 0;
		}
	}
#endif

	/* the project's save data: FreeJ2ME's record stores, the .zip Export
	 * Save Data writes; the front end puts it in place */
	static char savedata[256];
	savedata[0] = '\0';
	if (wbx_slot_count("savedata") > 0) {
		wbx_slot_name("savedata", 0, savedata, sizeof savedata);
#ifdef CHIMERA_GUEST
		char at[300];
		snprintf(at, sizeof at, "/%.256s", savedata);
		if (!memfs_import(savedata, at)) {
			snprintf(g_loadError, sizeof g_loadError, "the save data %.256s could not be read", savedata);
			return 0;
		}
#endif
	}

	/* the settings, as FreeJ2ME's own */
	char size[32], phone[32], fps[16], sound[8], synths[8], encoding[16];
	setting("screenSize", "240x320", size, sizeof size);
	setting("phone", "Standard", phone, sizeof phone);
	setting("fpsLimit", "60", fps, sizeof fps);
	setting("sound", "on", sound, sizeof sound);
	setting("midiSynths", "4", synths, sizeof synths);
	setting("textEncoding", "auto", encoding, sizeof encoding);
	int width = 240, height = 320;
	if (sscanf(size, "%dx%d", &width, &height) != 2 || width < 16 || height < 16 || width * height > MAX_PIXELS) {
		snprintf(g_loadError, sizeof g_loadError, "the screen size %s is not one this core has", size);
		return 0;
	}
	if (!strcmp(encoding, "auto")) {
		/* what FreeJ2ME itself switches to: Korean SKT games (.msd), and
		 * Japanese ones (DoJa's .jam, KDDI's .kjx) */
		snprintf(encoding, sizeof encoding, "ISO_8859_1");
		for (int i = 0; i < count; i++) {
			if (ends_with(names[i], ".msd"))
				snprintf(encoding, sizeof encoding, "EUC_KR");
			if (ends_with(names[i], ".jam") || ends_with(names[i], ".kjx"))
				snprintf(encoding, sizeof encoding, "Shift_JIS");
		}
	}
	static char opt[8][256];
	snprintf(opt[0], sizeof opt[0], "-Dsun.awt.fontconfig=%s/lib/fontconfig.properties", CORE_HOME);
	snprintf(opt[1], sizeof opt[1], "-Dfile.encoding=%s", encoding);
	for (int i = 0; i < 2; i++)
		driver_add_option(opt[i]);
	/* everything the program writes stays among the machine's files */
	driver_add_option("-Duser.home=home");
	driver_add_option("-Djava.io.tmpdir=tmp");
	driver_add_option("-Djava.util.prefs.userRoot=prefs");
	driver_add_option("-Djava.util.prefs.systemRoot=prefs");
	mkdir("home", 0755);
	mkdir("tmp", 0755);

	static char fj[8][300];
	snprintf(fj[0], sizeof fj[0], "scrwidth=%d", width);
	snprintf(fj[1], sizeof fj[1], "scrheight=%d", height);
	snprintf(fj[2], sizeof fj[2], "phone=%s", phone);
	snprintf(fj[3], sizeof fj[3], "fps=%s", strcmp(fps, "off") ? fps : "0");
	snprintf(fj[4], sizeof fj[4], "sys.sound=%s", sound);
	snprintf(fj[5], sizeof fj[5], "sys.numsynths=%s", synths);
	snprintf(fj[6], sizeof fj[6], "chimera.savedata=%.256s", savedata);
	const char *fjs[7] = { fj[0], fj[1], fj[2], fj[3], fj[4], fj[5], fj[6] };

	g_picture = alloc_invisible(MAX_PIXELS * sizeof(uint32_t));
	driver_set_video_buffer(g_picture, MAX_PIXELS);
	if (!driver_start(CORE_CLASSPATH, game, width, height, fjs, savedata[0] ? 7 : 6)) {
		snprintf(g_loadError, sizeof g_loadError, "%s", driver_error());
		return 0;
	}
	g_running = 1;
	find_heap();
	return 1;
}

ECL_EXPORT int GetButtonCount(void) { return BUTTON_COUNT; }
ECL_EXPORT const char *GetButtonName(int32_t i)
{
	return i >= 0 && i < KEY_COUNT ? kKeys[i].name : i == BUTTON_TOUCH ? "Touch" : "";
}
ECL_EXPORT int IsButtonActive(int32_t i) { return i >= 0 && i < BUTTON_COUNT; }
ECL_EXPORT void SetButton(int32_t i, int32_t state)
{
	if (i >= 0 && i < BUTTON_COUNT)
		g_held[i] = state != 0;
}

ECL_EXPORT int GetAxisCount(void) { return kAxes; }
ECL_EXPORT const char *GetAxisName(int32_t i) { return i >= 0 && i < kAxes ? kAxisNames[i] : ""; }
ECL_EXPORT int IsAxisActive(int32_t i) { return i >= 0 && i < kAxes; }
ECL_EXPORT void SetAxis(int32_t i, int32_t value)
{
	if (i >= 0 && i < kAxes)
		g_axis[i] = value;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	(void)packed;
	if (!g_running)
		return;
	int keys = 0;
	for (int i = 0; i < KEY_COUNT; i++)
		if (g_held[i])
			keys |= 1 << kKeys[i].fj;
	int w, h;
	driver_video(&w, &h);
	if (w <= 0 || h <= 0) {
		w = 240;
		h = 320;
	}
	const int x = (int)((int64_t)clampi(g_axis[kTouchX], 0, 65535) * w / 65536);
	const int y = (int)((int64_t)clampi(g_axis[kTouchY], 0, 65535) * h / 65536);
	if (!driver_frame(keys, g_held[BUTTON_TOUCH], x, y)) {
		/* the program died: the machine stops where it is */
		fprintf(stderr, "freej2me: %s\n", driver_error());
		g_running = 0;
	}
}

/* J2ME games are told of a key as it changes, not asked to look: every
 * frame counts as one that read its input. */
ECL_EXPORT int InputWasRead(void) { return 1; }
ECL_EXPORT void SetRenderingEnabled(int on) { g_render = on != 0; }

ECL_EXPORT uint32_t *GetVideoBgra(void)
{
	int w, h;
	return (uint32_t *)driver_video(&w, &h);
}
ECL_EXPORT int GetVideoWidth(void) { int w, h; driver_video(&w, &h); return w; }
ECL_EXPORT int GetVideoHeight(void) { int w, h; driver_video(&w, &h); return h; }
ECL_EXPORT int GetDisplayAspectX(void) { return GetVideoWidth(); }
ECL_EXPORT int GetDisplayAspectY(void) { return GetVideoHeight(); }
ECL_EXPORT int GetVsyncNumerator(void) { return 60; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

ECL_EXPORT int16_t *GetAudio(void) { return g_running ? (int16_t *)driver_audio() : g_silence; }
ECL_EXPORT int GetAudioSampleCount(void) { return DRIVER_AUDIO_FRAMES; }

/* RAM Watch and RAM Search: the Java heap, where the game's objects and its
 * classes' static fields live (heap.cpp finds it). The collector moves
 * objects, so a value found at an address stays there only until the next
 * collection moves it. Guest only: the native runner's heap is the host's. */
static uint8_t *g_heap;
static int64_t g_heapSize;
#ifdef CHIMERA_GUEST
extern int chimera_java_heap(void **base, size_t *bytes);
static void find_heap(void)
{
	void *base;
	size_t bytes;
	if (chimera_java_heap(&base, &bytes)) {
		g_heap = base;
		g_heapSize = (int64_t)bytes;
	}
}
#else
static void find_heap(void) { }
#endif
ECL_EXPORT int GetMemoryDomainCount(void) { return g_heap ? 1 : 0; }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return i == 0 && g_heap ? "Java Heap" : ""; }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return i == 0 ? g_heap : NULL; }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return i == 0 && g_heap ? g_heapSize : 0; }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return i == 0 && g_heap; }

ECL_EXPORT const int64_t *GetBusRanges(int32_t bus) { (void)bus; return NULL; }
ECL_EXPORT const uint8_t *ReadBus(int32_t bus, int64_t addr, int32_t len) { (void)bus; (void)addr; (void)len; return NULL; }
ECL_EXPORT void PokeBus(int32_t bus, int32_t addr, int32_t value) { (void)bus; (void)addr; (void)value; }

/* Export Save Data: the record stores the game wrote - rms/<suite>/, a
 * description (.rms) and a file for each record - the names the save data
 * slot takes back */
#ifdef CHIMERA_GUEST
static int g_saves[256], g_saveCount;
ECL_EXPORT int32_t GetSaveDataFileCount(void)
{
	g_saveCount = memfs_list("/rms/", g_saves, 256);
	return g_saveCount;
}
static int save_ok(int32_t i) { return i >= 0 && i < g_saveCount; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { return save_ok(i) ? memfs_path(g_saves[i]) + 1 : ""; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i)
{
	size_t n = 0;
	if (save_ok(i))
		memfs_data(g_saves[i], &n);
	return (int64_t)n;
}
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i)
{
	size_t n;
	return save_ok(i) ? memfs_data(g_saves[i], &n) : NULL;
}
#else
/* the native runner's files are the host's: it exports nothing */
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return 0; }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { (void)i; return ""; }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i) { (void)i; return 0; }
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i) { (void)i; return NULL; }
#endif
