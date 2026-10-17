/* run-native.c - the native runner.
 *
 * Links the same exports the guest build has (core.c, driver.c; emulibc
 * degraded to malloc by native-shim/) and drives them directly. The work dir
 * holds the same files the sandbox would see mounted: the game under its
 * name, plus the "slots" and "settings" JSON.
 *
 * Here the exports are core.c compiled for the host, over the host's own
 * Zero VM (build/jdk) and runtime (build/rt). The game's threads run on the
 * host's clock, so a game whose picture depends on timing does not draw the
 * same frames here as in the sandbox: this runner shows that a game works,
 * and the sandbox runs are compared with each other.
 *
 * usage: run-native <workdir> [options - see gate-harness.h]
 * (From chimera-core-tic80.) SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "gate-harness.h"

extern int Init(void);
extern const char *SuggestSettings(void);
extern const char *GetLoadError(void);
extern void SetButton(int32_t index, int32_t state);
extern int IsButtonActive(int32_t index);
extern const char *GetButtonName(int32_t index);
extern int GetButtonCount(void);
extern const char *DescribeDips(void);
extern int32_t GetSaveDataFileCount(void);
extern const char *GetSaveDataFileName(int32_t i);
extern int64_t GetSaveDataFileSize(int32_t i);
extern const uint8_t *GetSaveDataFileBuffer(int32_t i);
static int save_count(void) { return GetSaveDataFileCount(); }
static const char *save_name(int i) { return GetSaveDataFileName(i); }
static int64_t save_size(int i) { return GetSaveDataFileSize(i); }
static const uint8_t *save_data(int i) { return GetSaveDataFileBuffer(i); }
extern void FrameAdvance(uint64_t packed);
extern int InputWasRead(void);
extern int IsAxisActive(int32_t index);
extern void SetAxis(int32_t index, int32_t value);
extern int GetAxisCount(void);
extern uint32_t *GetVideoBgra(void);
extern int GetVideoWidth(void);
extern int GetVideoHeight(void);
extern int16_t *GetAudio(void);
extern int GetAudioSampleCount(void);
extern int GetMemoryDomainCount(void);
extern const char *GetMemoryDomainName(int i);
extern uint8_t *GetMemoryDomainPtr(int i);
extern int64_t GetMemoryDomainSize(int i);

static void frame(void) { FrameAdvance(0); }
static const uint32_t *video(int *w, int *h)
{
	*w = GetVideoWidth();
	*h = GetVideoHeight();
	return GetVideoBgra();
}
static const int16_t *audio(int *n)
{
	*n = GetAudioSampleCount();
	return GetAudio();
}
static const uint8_t *domain_ptr(int i) { return GetMemoryDomainPtr(i); }
extern const int64_t *GetBusRanges(int32_t bus);
extern const uint8_t *ReadBus(int32_t bus, int64_t addr, int32_t len);
static const int64_t *bus_ranges(void) { return GetBusRanges(0); }
static const uint8_t *bus_read(int64_t addr, int32_t len) { return ReadBus(0, addr, len); }
extern void PokeBus(int32_t bus, int32_t addr, int32_t value);
static void bus_poke(int32_t addr, int32_t value) { PokeBus(0, addr, value); }

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: run-native <workdir> [options]\n");
		return 2;
	}
	if (chdir(argv[1]) != 0)
	{
		perror(argv[1]);
		return 1;
	}
	/* what the New Project window is told about the game, and nothing run */
	if (argc > 2 && !strcmp(argv[2], "--suggest"))
	{
		puts(SuggestSettings());
		return 0;
	}
	struct gate_opts o;
	if (!gate_parse_opts(argc, argv, 2, &o))
		return 2;
	struct gate_core c = {
		.init = Init,
		.load_error = GetLoadError,
		.set_button = SetButton,
		.button_count = GetButtonCount,
		.button_active = IsButtonActive,
		.button_name = GetButtonName,
		.describe = DescribeDips,
		.frame = frame,
		.input_was_read = InputWasRead,
		.axis_count = GetAxisCount,
		.axis_active = IsAxisActive,
		.set_axis = SetAxis,
		.video = video,
		.audio = audio,
		.domain_count = GetMemoryDomainCount,
		.domain_name = GetMemoryDomainName,
		.domain_ptr = domain_ptr,
		.domain_size = GetMemoryDomainSize,
		.bus_ranges = bus_ranges,
		.bus_read = bus_read,
		.bus_poke = bus_poke,
		.pre_frame = NULL,
		.save_count = save_count,
		.save_name = save_name,
		.save_size = save_size,
		.save_data = save_data,
	};
	return gate_run(&c, &o);
}
