/* driver.c - the Java VM on a thread of its own, run a frame at a time.
 * Shared by the sandbox core and the native runner.
 *
 * The VM is not created on the thread the host calls in on: HotSpot treats
 * that one as the process's first thread and goes looking for its stack in
 * /proc, which the java launcher avoids by starting a new thread too. The
 * caller asks for something (start the game, run a frame) and waits; the VM
 * thread does it through JNI, during which the game's own threads get their
 * turns, and answers.
 *
 * Only C calls Java here, never the other way: the front end
 * (java/chimera/Frontend.java) has no native methods.
 * SPDX-License-Identifier: MIT
 */
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver.h"

/* A text a person reads is a key of languages/en.json, each of its
 * arguments after a TEXT_ARG (chimera docs/translations.md). TEXT_ARG is a
 * string of its own so the byte after it is never read as more of its hex. */
#define TEXT_ARG "\x1f"

enum { REQ_NONE, REQ_START, REQ_FRAME };

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_req = REQ_NONE, g_busy, g_dead, g_result;
static char g_error[1024];

static const char *g_classpath;
static char *g_options[16];
static int g_noptions;

/* the start request */
static const char *g_game;
static int g_width, g_height;
static const char *const *g_settings;
static int g_nsettings;
/* the frame request */
static int g_keys, g_touch, g_x, g_y;

/* what the last frame left: the picture (0xAARRGGBB) and the sound */
static uint32_t *g_video;
static int g_videoCap, g_videoW, g_videoH;
static int16_t g_audio[DRIVER_AUDIO_FRAMES * 2];
static int g_newPicture;

static JNIEnv *g_env;
static jclass g_frontend;
static jmethodID g_start, g_frame;
static jfieldID g_lcd, g_w, g_h, g_audioField, g_errorField, g_newPictureField;

void driver_add_option(const char *opt)
{
	if (g_noptions < (int)(sizeof g_options / sizeof g_options[0]))
		g_options[g_noptions++] = strdup(opt);
}

void driver_set_video_buffer(uint32_t *buf, int pixels)
{
	g_video = buf;
	g_videoCap = pixels;
}

const char *driver_error(void) { return g_error; }

/* the exception's text, into g_error, and the exception cleared */
static void describe_exception(const char *what, const char *what_because)
{
	JNIEnv *env = g_env;
	jthrowable t = (*env)->ExceptionOccurred(env);
	(*env)->ExceptionDescribe(env);
	snprintf(g_error, sizeof g_error, "%s", what);
	if (!t)
		return;
	(*env)->ExceptionClear(env);
	jclass c = (*env)->FindClass(env, "java/lang/Object");
	jmethodID ts = c ? (*env)->GetMethodID(env, c, "toString", "()Ljava/lang/String;") : NULL;
	jstring s = ts ? (jstring)(*env)->CallObjectMethod(env, t, ts) : NULL;
	if (s) {
		const char *u = (*env)->GetStringUTFChars(env, s, NULL);
		snprintf(g_error, sizeof g_error, "%s" TEXT_ARG "%s", what_because, u);
		(*env)->ReleaseStringUTFChars(env, s, u);
	}
	(*env)->ExceptionClear(env);
}

static int find_frontend(void)
{
	JNIEnv *env = g_env;
	jclass c = (*env)->FindClass(env, "chimera/Frontend");
	if (!c) {
		describe_exception("error.frontend_missing", "error.frontend_missing_because");
		return 0;
	}
	g_frontend = (jclass)(*env)->NewGlobalRef(env, c);
	g_start = (*env)->GetStaticMethodID(env, c, "start", "(Ljava/lang/String;II[Ljava/lang/String;)Z");
	g_frame = (*env)->GetStaticMethodID(env, c, "frame", "(IZII)V");
	g_lcd = (*env)->GetStaticFieldID(env, c, "lcd", "[I");
	g_w = (*env)->GetStaticFieldID(env, c, "width", "I");
	g_h = (*env)->GetStaticFieldID(env, c, "height", "I");
	g_audioField = (*env)->GetStaticFieldID(env, c, "audio", "[S");
	g_errorField = (*env)->GetStaticFieldID(env, c, "error", "Ljava/lang/String;");
	g_newPictureField = (*env)->GetStaticFieldID(env, c, "newPicture", "Z");
	if (!g_start || !g_frame || !g_lcd || !g_w || !g_h || !g_audioField || !g_errorField || !g_newPictureField) {
		describe_exception("error.frontend_other", "error.frontend_other_because");
		return 0;
	}
	return 1;
}

static int do_start(void)
{
	JNIEnv *env = g_env;
	jclass str = (*env)->FindClass(env, "java/lang/String");
	jobjectArray settings = (*env)->NewObjectArray(env, g_nsettings, str, NULL);
	for (int i = 0; i < g_nsettings; i++) {
		jstring s = (*env)->NewStringUTF(env, g_settings[i]);
		(*env)->SetObjectArrayElement(env, settings, i, s);
		(*env)->DeleteLocalRef(env, s);
	}
	jstring game = (*env)->NewStringUTF(env, g_game);
	const jboolean ok = (*env)->CallStaticBooleanMethod(env, g_frontend, g_start, game, (jint)g_width, (jint)g_height, settings);
	(*env)->DeleteLocalRef(env, game);
	(*env)->DeleteLocalRef(env, settings);
	if ((*env)->ExceptionCheck(env)) {
		describe_exception("error.game_did_not_start", "error.game_did_not_start_because");
		return 0;
	}
	if (!ok) {
		jstring e = (jstring)(*env)->GetStaticObjectField(env, g_frontend, g_errorField);
		const char *u = e ? (*env)->GetStringUTFChars(env, e, NULL) : NULL;
		snprintf(g_error, sizeof g_error, "%s", u ? u : "error.game_did_not_start");
		if (u)
			(*env)->ReleaseStringUTFChars(env, e, u);
		return 0;
	}
	return 1;
}

static void copy_outputs(void)
{
	JNIEnv *env = g_env;
	const jint w = (*env)->GetStaticIntField(env, g_frontend, g_w);
	const jint h = (*env)->GetStaticIntField(env, g_frontend, g_h);
	jintArray lcd = (jintArray)(*env)->GetStaticObjectField(env, g_frontend, g_lcd);
	if (lcd && w > 0 && h > 0 && w * h <= g_videoCap && (*env)->GetArrayLength(env, lcd) >= w * h) {
		(*env)->GetIntArrayRegion(env, lcd, 0, w * h, (jint *)g_video);
		/* the picture has no see-through parts */
		for (int i = 0; i < w * h; i++)
			g_video[i] |= 0xff000000u;
		g_videoW = w;
		g_videoH = h;
	}
	(*env)->DeleteLocalRef(env, lcd);
	jshortArray a = (jshortArray)(*env)->GetStaticObjectField(env, g_frontend, g_audioField);
	if (a)
		(*env)->GetShortArrayRegion(env, a, 0, DRIVER_AUDIO_FRAMES * 2, (jshort *)g_audio);
	(*env)->DeleteLocalRef(env, a);
	g_newPicture = (*env)->GetStaticBooleanField(env, g_frontend, g_newPictureField) ? 1 : 0;
}

static int do_frame(void)
{
	JNIEnv *env = g_env;
	(*env)->CallStaticVoidMethod(env, g_frontend, g_frame, (jint)g_keys, (jboolean)(g_touch != 0), (jint)g_x, (jint)g_y);
	if ((*env)->ExceptionCheck(env)) {
		describe_exception("error.frame_did_not_finish", "error.frame_did_not_finish_because");
		return 0;
	}
	copy_outputs();
	return 1;
}

static void answer(int result)
{
	pthread_mutex_lock(&g_mu);
	g_result = result;
	g_busy = 0;
	g_req = REQ_NONE;
	if (!result)
		g_dead = 1;
	pthread_cond_broadcast(&g_cv);
	pthread_mutex_unlock(&g_mu);
}

static void *vm_main(void *arg)
{
	(void)arg;
	char cp[1024];
	snprintf(cp, sizeof cp, "-Djava.class.path=%s", g_classpath);
	JavaVMOption opts[48] = {
		{ cp, NULL },
		{ "-XX:+UseSerialGC", NULL },
		/* the whole heap committed from the start: the memory domain shows it */
		{ "-Xms64m", NULL },
		{ "-Xmx64m", NULL },
		{ "-XX:-UsePerfData", NULL },
		{ "-XX:-UseContainerSupport", NULL },
		{ "-XX:ActiveProcessorCount=1", NULL },
		{ "-Xss512k", NULL },
		{ "-XX:CompressedClassSpaceSize=32m", NULL },
		{ "-XX:MaxMetaspaceSize=64m", NULL },
		{ "-XX:ReservedCodeCacheSize=16m", NULL },
		{ "-Djava.awt.headless=true", NULL },
		/* the machine's sound is the front end's mixer (java/chimera/sound) */
		{ "-Djavax.sound.sampled.SourceDataLine=chimera.sound.ChimeraMixerProvider", NULL },
		{ "-Djavax.sound.sampled.Clip=chimera.sound.ChimeraMixerProvider", NULL },
	};
	int n = 0;
	while (opts[n].optionString)
		n++;
	for (int i = 0; i < g_noptions; i++)
		opts[n++].optionString = g_options[i];
	JavaVMInitArgs args;
	memset(&args, 0, sizeof args);
	args.version = JNI_VERSION_21;
	args.nOptions = n;
	args.options = opts;
	args.ignoreUnrecognized = JNI_FALSE;
	JavaVM *vm;
	if (JNI_CreateJavaVM(&vm, (void **)&g_env, &args) != JNI_OK) {
		snprintf(g_error, sizeof g_error, "error.vm_did_not_start");
		answer(0);
		return NULL;
	}
	if (!find_frontend()) {
		answer(0);
		return NULL;
	}
	for (;;) {
		pthread_mutex_lock(&g_mu);
		while (g_req == REQ_NONE || !g_busy)
			pthread_cond_wait(&g_cv, &g_mu);
		const int req = g_req;
		pthread_mutex_unlock(&g_mu);
		const int ok = req == REQ_START ? do_start() : do_frame();
		answer(ok);
		if (!ok)
			return NULL;
	}
}

static int ask(int req)
{
	pthread_mutex_lock(&g_mu);
	if (g_dead) {
		pthread_mutex_unlock(&g_mu);
		return 0;
	}
	g_req = req;
	g_busy = 1;
	pthread_cond_broadcast(&g_cv);
	while (g_busy && !g_dead)
		pthread_cond_wait(&g_cv, &g_mu);
	const int ok = !g_dead && g_result;
	pthread_mutex_unlock(&g_mu);
	return ok;
}

int driver_start(const char *classpath, const char *game, int width, int height, const char *const *settings, int nsettings)
{
	g_classpath = classpath;
	g_game = game;
	g_width = width;
	g_height = height;
	g_settings = settings;
	g_nsettings = nsettings;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 4u << 20);
	pthread_t t;
	/* the VM thread waits for its first request before it is busy */
	pthread_mutex_lock(&g_mu);
	g_req = REQ_START;
	g_busy = 1;
	pthread_mutex_unlock(&g_mu);
	if (pthread_create(&t, &attr, vm_main, NULL) != 0) {
		snprintf(g_error, sizeof g_error, "error.vm_thread");
		return 0;
	}
	pthread_attr_destroy(&attr);
	pthread_mutex_lock(&g_mu);
	while (g_busy && !g_dead)
		pthread_cond_wait(&g_cv, &g_mu);
	const int ok = !g_dead && g_result;
	pthread_mutex_unlock(&g_mu);
	return ok;
}

int driver_frame(int keys, int touch, int x, int y)
{
	g_keys = keys;
	g_touch = touch;
	g_x = x;
	g_y = y;
	return ask(REQ_FRAME);
}

const uint32_t *driver_video(int *w, int *h)
{
	*w = g_videoW;
	*h = g_videoH;
	return g_video;
}

const int16_t *driver_audio(void) { return g_audio; }
int driver_new_picture(void) { return g_newPicture; }
