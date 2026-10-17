/* run-wbx.c - drives core.wbx through the miniBox host over the same work
 * dir and schedule as run-native, reporting the same digests, so the two
 * builds diff directly. Every regular file in the work dir is mounted into
 * the guest under its basename - exactly what the frontend does with a
 * project's files, slot map and settings.
 *
 * usage: run-wbx <core.wbx> <workdir> [run-native's options] [--rerecord] [--session]
 *
 * --rerecord round-trips the WHOLE guest machine through the host's
 * save/load state around every frame; the digests must be identical.
 * --session saves the machine half way, tears the host down, builds a NEW
 * host from the same core and files, loads the state into it and finishes
 * the run there - the reopened-project case, which is where a host pointer
 * kept in guest state shows.
 * --state-hash prints, after the run, a digest of the whole saved machine:
 * two runs that end in the same machine print the same line.
 * --save-at N FILE writes the machine to FILE before frame N, and carries on;
 * --resume FILE N, in another process, loads it and runs from frame N: the
 * reports from there must be the straight run's. --dump-baseline FILE writes
 * every page of the sealed machine (index, then 4096 bytes), for comparing
 * what two processes sealed.
 * (From chimera-core-tic80.) SPDX-License-Identifier: MIT
 */
#include "minibox.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gate-harness.h"

typedef struct { FILE *f; } freader;
static intptr_t file_read(uintptr_t ud, uint8_t *d, uintptr_t s) { return (intptr_t)fread(d, 1, s, ((freader *)ud)->f); }
typedef struct { uint8_t *b; size_t len, cap, pos; } membuf;
static int32_t mem_write(uintptr_t ud, const uint8_t *d, uintptr_t n)
{
	membuf *m = (membuf *)ud;
	if (m->len + n > m->cap) { m->cap = (m->len + n) * 2 + 64; m->b = realloc(m->b, m->cap); }
	memcpy(m->b + m->len, d, n); m->len += n; return 0;
}
static intptr_t mem_read(uintptr_t ud, uint8_t *d, uintptr_t n)
{
	membuf *m = (membuf *)ud;
	uintptr_t avail = m->len - m->pos; if (n > avail) n = avail;
	memcpy(d, m->b + m->pos, n); m->pos += n; return (intptr_t)n;
}

typedef int (MB_GUEST_ABI *intfn)(void);
typedef void (MB_GUEST_ABI *framefn)(uint64_t);
typedef void (MB_GUEST_ABI *setfn)(int32_t, int32_t);
typedef void (MB_GUEST_ABI *voidfn_i)(int);
typedef uintptr_t (MB_GUEST_ABI *ptrfn)(void);
typedef uintptr_t (MB_GUEST_ABI *ptrfn_i)(int);
typedef int (MB_GUEST_ABI *intfn_i)(int);
typedef int64_t (MB_GUEST_ABI *i64fn_i)(int);
typedef int32_t (MB_GUEST_ABI *i32fn)(void);
typedef int32_t (MB_GUEST_ABI *i32fn_i)(int32_t);
typedef uintptr_t (MB_GUEST_ABI *ptrfn_i32)(int32_t);
typedef int64_t (MB_GUEST_ABI *i64fn_i32)(int32_t);
typedef uint64_t (MB_GUEST_ABI *u64fn)(void);

static mb_host *g_host;
static intfn g_Init;
static ptrfn g_GetLoadError;
static setfn g_SetButton;
static intfn_i g_IsButtonActive;
static ptrfn_i g_GetButtonName;
static intfn g_GetButtonCount;
static intfn g_InputWasRead;
static intfn_i g_IsAxisActive;
static setfn g_SetAxis;
static intfn g_GetAxisCount;
static ptrfn g_DescribeDips;
static i32fn g_SaveCount;
static ptrfn_i32 g_SaveName, g_SaveBuffer;
static i64fn_i32 g_SaveSize;
static framefn g_FrameAdvance;
static ptrfn g_GetVideoBgra;
static intfn g_GetVideoWidth, g_GetVideoHeight;
static ptrfn g_GetAudio;
static intfn g_GetAudioSampleCount;
static intfn g_GetMemoryDomainCount;
static ptrfn_i g_GetMemoryDomainName, g_GetMemoryDomainPtr;
static i64fn_i g_GetMemoryDomainSize;
static int g_rerecord;
static int g_session;
static int g_rewind;
static int g_stateHash;
static long g_saveAt = -1;
static const char *g_saveFile, *g_resumeFile, *g_baselineFile;
static long g_resumeAt;
static long g_sessionAt;
static long g_frameNo;
static membuf g_state;
static const char *g_wbxPath;
static const char *g_workdir;
static void build_host(void);
static void resolve_exports(void);

static uintptr_t proc(mb_host *h, const char *n)
{
	mb_return r;
	wbx_get_proc_addr(h, n, &r);
	if (r.error_message[0]) { fprintf(stderr, "proc %s: %s\n", n, r.error_message); exit(2); }
	if (!r.data) { fprintf(stderr, "missing required export %s\n", n); exit(2); }
	return r.data;
}

static int core_init(void) { return g_Init(); }
static int core_init_done(void) { return 1; } /* boot happens before Seal, not in gate_run */
static const char *core_load_error(void) { return (const char *)g_GetLoadError(); }
static int core_button_count(void) { return g_GetButtonCount(); }
static int core_input_was_read(void) { return g_InputWasRead(); }
static int core_axis_count(void) { return g_GetAxisCount(); }
static int core_axis_active(int32_t i) { return g_IsAxisActive(i); }
static void core_set_axis(int32_t i, int32_t v) { g_SetAxis(i, v); }
static int core_button_active(int32_t i) { return g_IsButtonActive(i); }
static const char *core_button_name(int32_t i) { return (const char *)g_GetButtonName(i); }
static const char *core_describe(void) { return (const char *)g_DescribeDips(); }
static int core_save_count(void) { return g_SaveCount(); }
static const char *core_save_name(int i) { return (const char *)g_SaveName(i); }
static int64_t core_save_size(int i) { return g_SaveSize(i); }
static const uint8_t *core_save_data(int i) { return (const uint8_t *)g_SaveBuffer(i); }
static void core_set_button(int32_t i, int32_t s) { g_SetButton(i, s); }
static void core_frame(void) { g_FrameAdvance(0); }
static const uint32_t *core_video(int *w, int *h)
{
	*w = g_GetVideoWidth();
	*h = g_GetVideoHeight();
	return (const uint32_t *)g_GetVideoBgra();
}
static const int16_t *core_audio(int *n)
{
	*n = g_GetAudioSampleCount();
	return (const int16_t *)g_GetAudio();
}
static int core_domain_count(void) { return g_GetMemoryDomainCount(); }
static const char *core_domain_name(int i) { return (const char *)g_GetMemoryDomainName(i); }
static const uint8_t *core_domain_ptr(int i) { return (const uint8_t *)g_GetMemoryDomainPtr(i); }
static int64_t core_domain_size(int i) { return g_GetMemoryDomainSize(i); }
typedef uintptr_t (MB_GUEST_ABI *busrangesfn)(int32_t);
typedef uintptr_t (MB_GUEST_ABI *busreadfn)(int32_t, int64_t, int32_t);
static busrangesfn g_GetBusRanges;
static busreadfn g_ReadBus;
static const int64_t *core_bus_ranges(void) { return (const int64_t *)g_GetBusRanges(0); }
static const uint8_t *core_bus_read(int64_t addr, int32_t len) { return (const uint8_t *)g_ReadBus(0, addr, len); }
typedef void (MB_GUEST_ABI *buspokefn)(int32_t, int32_t, int32_t);
static buspokefn g_PokeBus;
static void core_bus_poke(int32_t addr, int32_t value) { g_PokeBus(0, addr, value); }

static void core_pre_frame(void)
{
	const long frame = g_frameNo++;
	mb_return r;
	if (g_saveFile && frame == g_saveAt)
	{
		membuf m = { 0 };
		wbx_save_state(g_host, mem_write, (uintptr_t)&m, &r);
		if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); exit(1); }
		FILE *f = fopen(g_saveFile, "wb");
		if (!f || fwrite(m.b, 1, m.len, f) != m.len) { perror(g_saveFile); exit(1); }
		fclose(f);
		free(m.b);
	}
	if (g_session && frame == g_sessionAt)
	{
		/* the machine leaves in a state and arrives in a new host */
		g_state.len = 0;
		wbx_save_state(g_host, mem_write, (uintptr_t)&g_state, &r);
		if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); exit(1); }
		wbx_deactivate_host(g_host, &r);
		wbx_destroy_host(g_host, &r);
		g_host = NULL;
		build_host();
		resolve_exports();
		if (g_Init() != 1)
		{
			fprintf(stderr, "Init failed in the second host: %s\n", (const char *)g_GetLoadError());
			exit(1);
		}
		wbx_deactivate_host(g_host, &r);
		wbx_seal(g_host, &r);
		if (r.error_message[0]) { fprintf(stderr, "seal: %s\n", r.error_message); exit(1); }
		wbx_activate_host(g_host, &r);
		g_state.pos = 0;
		wbx_load_state(g_host, mem_read, (uintptr_t)&g_state, &r);
		if (r.error_message[0]) { fprintf(stderr, "load_state (session): %s\n", r.error_message); exit(1); }
		return;
	}
	if (g_rewind && frame == g_sessionAt)
	{
		/* a state is kept here, and the machine comes back to it later */
		g_state.len = 0;
		wbx_save_state(g_host, mem_write, (uintptr_t)&g_state, &r);
		if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); exit(1); }
		return;
	}
	if (g_rewind && frame == g_sessionAt + g_sessionAt / 2)
	{
		g_state.pos = 0;
		wbx_load_state(g_host, mem_read, (uintptr_t)&g_state, &r);
		if (r.error_message[0]) { fprintf(stderr, "load_state (rewind): %s\n", r.error_message); exit(1); }
		return;
	}
	if (!g_rerecord)
		return;
	g_state.len = 0;
	wbx_save_state(g_host, mem_write, (uintptr_t)&g_state, &r);
	if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); exit(1); }
	g_state.pos = 0;
	wbx_load_state(g_host, mem_read, (uintptr_t)&g_state, &r);
	if (r.error_message[0]) { fprintf(stderr, "load_state: %s\n", r.error_message); exit(1); }
}

/* the host: the core loaded, every file of the work dir mounted, activated */
static void build_host(void)
{
	FILE *wf = fopen(g_wbxPath, "rb");
	if (!wf) { perror(g_wbxPath); exit(1); }

	/* matches waterbox.config memoryLayoutMiB */
	mb_memory_layout_template layout = { 64u << 20, 4u << 20, 16u << 20, 4u << 20, 1024u << 20 };
	freader fr = { wf };
	mb_return r;
	wbx_create_host(&layout, "core.wbx", file_read, (uintptr_t)&fr, &r);
	fclose(wf);
	if (r.error_message[0]) { fprintf(stderr, "create: %s\n", r.error_message); exit(1); }
	g_host = (mb_host *)r.data;

	/* mount the whole work dir: images under their real names + slots +
	 * settings. Harness outputs (.tga, .txt) are the driver's, not the guest's. */
	DIR *d = opendir(g_workdir);
	if (!d) { perror(g_workdir); exit(1); }
	struct dirent *de;
	while ((de = readdir(d)) != NULL)
	{
		char path[4096];
		snprintf(path, sizeof path, "%s/%s", g_workdir, de->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		const char *dot = strrchr(de->d_name, '.');
		if (dot && (!strcmp(dot, ".raw") || !strcmp(dot, ".txt")))
			continue;
		FILE *f = fopen(path, "rb");
		if (!f) { perror(path); exit(1); }
		freader rd = { f };
		wbx_mount_file(g_host, de->d_name, file_read, (uintptr_t)&rd, false, &r);
		fclose(f);
		if (r.error_message[0]) { fprintf(stderr, "mount %s: %s\n", de->d_name, r.error_message); exit(1); }
	}
	closedir(d);

	wbx_activate_host(g_host, &r);
}

static void resolve_exports(void)
{
	g_Init = (intfn)proc(g_host, "Init");
	g_GetLoadError = (ptrfn)proc(g_host, "GetLoadError");
	g_SetButton = (setfn)proc(g_host, "SetButton");
	g_IsButtonActive = (intfn_i)proc(g_host, "IsButtonActive");
	g_GetButtonName = (ptrfn_i)proc(g_host, "GetButtonName");
	g_GetButtonCount = (intfn)proc(g_host, "GetButtonCount");
	g_InputWasRead = (intfn)proc(g_host, "InputWasRead");
	g_IsAxisActive = (intfn_i)proc(g_host, "IsAxisActive");
	g_SetAxis = (setfn)proc(g_host, "SetAxis");
	g_GetAxisCount = (intfn)proc(g_host, "GetAxisCount");
	g_DescribeDips = (ptrfn)proc(g_host, "DescribeDips");
	g_SaveCount = (i32fn)proc(g_host, "GetSaveDataFileCount");
	g_SaveName = (ptrfn_i32)proc(g_host, "GetSaveDataFileName");
	g_SaveSize = (i64fn_i32)proc(g_host, "GetSaveDataFileSize");
	g_SaveBuffer = (ptrfn_i32)proc(g_host, "GetSaveDataFileBuffer");
	g_FrameAdvance = (framefn)proc(g_host, "FrameAdvance");
	g_GetVideoBgra = (ptrfn)proc(g_host, "GetVideoBgra");
	g_GetVideoWidth = (intfn)proc(g_host, "GetVideoWidth");
	g_GetVideoHeight = (intfn)proc(g_host, "GetVideoHeight");
	g_GetAudio = (ptrfn)proc(g_host, "GetAudio");
	g_GetAudioSampleCount = (intfn)proc(g_host, "GetAudioSampleCount");
	g_GetMemoryDomainCount = (intfn)proc(g_host, "GetMemoryDomainCount");
	g_GetMemoryDomainName = (ptrfn_i)proc(g_host, "GetMemoryDomainName");
	g_GetMemoryDomainPtr = (ptrfn_i)proc(g_host, "GetMemoryDomainPtr");
	g_GetMemoryDomainSize = (i64fn_i)proc(g_host, "GetMemoryDomainSize");
	g_GetBusRanges = (busrangesfn)proc(g_host, "GetBusRanges");
	g_ReadBus = (busreadfn)proc(g_host, "ReadBus");
	g_PokeBus = (buspokefn)proc(g_host, "PokeBus");
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		fprintf(stderr, "usage: run-wbx <core.wbx> <workdir> [options] [--rerecord]\n");
		return 2;
	}
	g_wbxPath = argv[1];
	g_workdir = argv[2];
	for (int i = 3; i < argc; i++)
	{
		if (!strcmp(argv[i], "--rerecord")) g_rerecord = 1;
		if (!strcmp(argv[i], "--session")) g_session = 1;
		if (!strcmp(argv[i], "--rewind")) g_rewind = 1;
		if (!strcmp(argv[i], "--state-hash")) { g_stateHash = 1; memmove(argv + i, argv + i + 1, (size_t)(argc - i - 1) * sizeof *argv); argc--; i--; }
		else if (!strcmp(argv[i], "--save-at") && i + 2 < argc) { g_saveAt = strtol(argv[i + 1], 0, 0); g_saveFile = argv[i + 2]; memmove(argv + i, argv + i + 3, (size_t)(argc - i - 3) * sizeof *argv); argc -= 3; i--; }
		else if (!strcmp(argv[i], "--resume") && i + 2 < argc) { g_resumeFile = argv[i + 1]; g_resumeAt = strtol(argv[i + 2], 0, 0); memmove(argv + i, argv + i + 3, (size_t)(argc - i - 3) * sizeof *argv); argc -= 3; i--; }
		else if (!strcmp(argv[i], "--dump-baseline") && i + 1 < argc) { g_baselineFile = argv[i + 1]; memmove(argv + i, argv + i + 2, (size_t)(argc - i - 2) * sizeof *argv); argc -= 2; i--; }
	}
	int suggest = argc > 3 && !strcmp(argv[3], "--suggest");
	if (suggest)
		argc = 3;

	struct gate_opts o;
	if (!gate_parse_opts(argc, argv, 3, &o))
		return 2;
	g_sessionAt = o.frames / 2;

	build_host();
	resolve_exports();

	/* what the New Project window is told about the game, and nothing run */
	if (suggest)
	{
		puts((const char *)((ptrfn)proc(g_host, "SuggestSettings"))());
		return 0;
	}

	struct gate_core c = {
		.init = core_init,
		.load_error = core_load_error,
		.set_button = core_set_button,
		.button_count = core_button_count,
		.button_active = core_button_active,
		.button_name = core_button_name,
		.describe = core_describe,
		.frame = core_frame,
		.input_was_read = core_input_was_read,
		.axis_count = core_axis_count,
		.axis_active = core_axis_active,
		.set_axis = core_set_axis,
		.video = core_video,
		.audio = core_audio,
		.domain_count = core_domain_count,
		.domain_name = core_domain_name,
		.domain_ptr = core_domain_ptr,
		.domain_size = core_domain_size,
		.bus_ranges = core_bus_ranges,
		.bus_read = core_bus_read,
		.bus_poke = core_bus_poke,
		.pre_frame = core_pre_frame,
		.save_count = core_save_count,
		.save_name = core_save_name,
		.save_size = core_save_size,
		.save_data = core_save_data,
	};

	/* Init runs before Seal - the loaded machine is the sealed baseline */
	if (c.init() != 1)
	{
		fprintf(stderr, "Init failed: %s\n", c.load_error());
		return 1;
	}

	mb_return r;
	wbx_deactivate_host(g_host, &r);
	wbx_seal(g_host, &r);
	if (r.error_message[0]) { fprintf(stderr, "seal: %s\n", r.error_message); return 1; }
	wbx_activate_host(g_host, &r);

	if (g_baselineFile)
	{
		FILE *df = fopen(g_baselineFile, "wb");
		mb_return pr;
		wbx_get_page_len(g_host, &pr);
		const size_t np = pr.data;
		for (size_t pg = 0; df && pg < np; pg++)
		{
			wbx_get_page_data(g_host, pg, &pr);
			if (!(pr.data & 1) || (pr.data & 0x40))
				continue;
			uint64_t idx = pg;
			fwrite(&idx, 8, 1, df);
			fwrite((const void *)(uintptr_t)(0x36f00000000ull + pg * 4096), 1, 4096, df);
		}
		if (df) fclose(df);
	}
	if (g_resumeFile)
	{
		FILE *f = fopen(g_resumeFile, "rb");
		if (!f) { perror(g_resumeFile); return 1; }
		membuf m = { 0 };
		uint8_t chunk[65536];
		size_t n;
		while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) mem_write((uintptr_t)&m, chunk, n);
		fclose(f);
		wbx_load_state(g_host, mem_read, (uintptr_t)&m, &r);
		if (r.error_message[0]) { fprintf(stderr, "load_state (resume): %s\n", r.error_message); return 1; }
		free(m.b);
		/* the state was taken before frame N: the run goes on from frame N + 1 */
		g_frameNo = g_resumeAt;
		o.first = g_resumeAt + 1;
	}

	c.init = core_init_done;
	int ret = gate_run(&c, &o);
	if (ret == 0 && g_stateHash)
	{
		membuf all = { 0 };
		wbx_save_state(g_host, mem_write, (uintptr_t)&all, &r);
		if (r.error_message[0]) { fprintf(stderr, "save_state: %s\n", r.error_message); return 1; }
		printf("state %016" PRIx64 " bytes %zu\n", gate_fnv(0, all.b, all.len), all.len);
		free(all.b);
	}
	if (g_rerecord || g_session)
		fprintf(stderr, "stateBytes=%zu\n", g_state.len);

	wbx_deactivate_host(g_host, &r);
	wbx_destroy_host(g_host, &r);
	free(g_state.b);
	return ret;
}
