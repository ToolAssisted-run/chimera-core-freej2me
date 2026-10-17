/* Guest-only stand-ins for what HotSpot and the JDK's native libraries ask of
 * the system and the sandbox does not answer. One static link: defining a
 * libc function here shadows musl's; --wrap is used where musl's own answer
 * is still wanted for the other cases.
 *
 * - dlopen/dlsym/dladdr: nothing is loaded at run time. Every library is
 *   linked in, and dlsym answers from a table of the native libraries'
 *   functions generated at build time (core-symbols.c).
 * - The VM thinks it lives at /jdk/lib/zero/libjvm.so, so java.home is
 *   /jdk and the class library is /jdk/lib/modules, in the pack.
 * - A file-backed mmap (jimage maps the modules file) is the file's bytes in
 *   the pack when it can be (memfs_map), else anonymous memory read from the
 *   file: the sandbox maps no files.
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "memfs.h"

struct core_symbol { const char *name; void *addr; };
extern const struct core_symbol core_symbols[];
extern const int core_symbol_count;

#define PROC_HANDLE ((void *)0x1)

/* NULL (the process) and the libraries linked in; nothing else exists, so
 * HotSpot's probes for libnuma or VMware's guest library find nothing. */
static const char *const g_linked[] = { "java", "jimage", "net", "nio", "verify", "zip", "awt", "awt_headless",
	"fontmanager", "freetype", "javajpeg", "lcms", "mlib_image", "prefs" };

void *dlopen(const char *file, int mode)
{
	(void)mode;
	if (!file)
		return PROC_HANDLE;
	const char *base = strrchr(file, '/');
	base = base ? base + 1 : file;
	for (size_t i = 0; i < sizeof g_linked / sizeof g_linked[0]; i++) {
		char name[64];
		snprintf(name, sizeof name, "lib%s.so", g_linked[i]);
		if (strcmp(base, name) == 0)
			return PROC_HANDLE;
	}
	return NULL;
}

/* What HotSpot looks up in libc by name. */
static int shim_sched_getcpu(void)
{
	return 0;
}

static int shim_pthread_setname_np(void *t, const char *name)
{
	(void)t;
	(void)name;
	return 0;
}

int sched_getcpu(void)
{
	return 0;
}

#include <pthread.h>
#include <signal.h>
static const struct core_symbol g_libc_symbols[] = {
	{ "sched_getcpu", (void *)shim_sched_getcpu },
	{ "pthread_setname_np", (void *)shim_pthread_setname_np },
	{ "pthread_getcpuclockid", (void *)pthread_getcpuclockid },
	{ "sigaction", (void *)sigaction },
};

int dlclose(void *h)
{
	(void)h;
	return 0;
}

void *dlsym(void *restrict h, const char *restrict name)
{
	(void)h;
	for (size_t i = 0; i < sizeof g_libc_symbols / sizeof g_libc_symbols[0]; i++)
		if (strcmp(g_libc_symbols[i].name, name) == 0)
			return g_libc_symbols[i].addr;
	for (int i = 0; i < core_symbol_count; i++)
		if (strcmp(core_symbols[i].name, name) == 0)
			return core_symbols[i].addr;
	return NULL;
}

int dladdr(const void *addr, Dl_info *info)
{
	(void)addr;
	memset(info, 0, sizeof *info);
	info->dli_fname = "/jdk/lib/zero/libjvm.so";
	info->dli_fbase = (void *)0x36f00000000ull;
	return 1;
}

/* No directories and no links in the box: a path is its own real path. */
char *realpath(const char *restrict path, char *restrict resolved)
{
	if (!path) {
		errno = EINVAL;
		return NULL;
	}
	if (strlen(path) >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	if (!resolved) {
		resolved = malloc(PATH_MAX);
		if (!resolved)
			return NULL;
	}
	strcpy(resolved, path);
	return resolved;
}

char *getcwd(char *buf, size_t size)
{
	if (!buf) {
		size = 2;
		buf = malloc(size);
		if (!buf)
			return NULL;
	}
	if (size < 2) {
		errno = ERANGE;
		return NULL;
	}
	strcpy(buf, "/");
	return buf;
}

int uname(struct utsname *u)
{
	memset(u, 0, sizeof *u);
	strcpy(u->sysname, "Linux");
	strcpy(u->nodename, "minibox");
	strcpy(u->release, "6.0.0");
	strcpy(u->version, "#1");
	strcpy(u->machine, "x86_64");
	return 0;
}

int getrlimit(int res, struct rlimit *r)
{
	switch (res) {
	case RLIMIT_STACK:
		r->rlim_cur = r->rlim_max = 8u << 20;
		break;
	case RLIMIT_NOFILE:
		r->rlim_cur = r->rlim_max = 1024;
		break;
	default:
		r->rlim_cur = r->rlim_max = RLIM_INFINITY;
		break;
	}
	return 0;
}

int setrlimit(int res, const struct rlimit *r)
{
	(void)res;
	(void)r;
	return 0;
}

int clock_getres(clockid_t clk, struct timespec *ts)
{
	(void)clk;
	if (ts) {
		ts->tv_sec = 0;
		ts->tv_nsec = 1000;
	}
	return 0;
}

/* One processor; the rest is musl's own answer. */
long __real_sysconf(int name);
long __wrap_sysconf(int name)
{
	switch (name) {
	case _SC_NPROCESSORS_CONF:
	case _SC_NPROCESSORS_ONLN:
		return 1;
	default:
		return __real_sysconf(name);
	}
}

void *__real_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	if (fd < 0 || (flags & MAP_ANONYMOUS))
		return __real_mmap(addr, len, prot, flags, fd, off);
	if (!(flags & MAP_FIXED)) {
		const void *mine = memfs_map(fd, len, off, (prot & PROT_WRITE) && (flags & MAP_SHARED));
		if (mine && !(prot & PROT_WRITE))
			return (void *)mine;
	}
	void *p = __real_mmap(addr, len, PROT_READ | PROT_WRITE, (flags & ~MAP_SHARED) | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return p;
	size_t got = 0;
	while (got < len) {
		ssize_t n = pread(fd, (char *)p + got, len - got, off + (off_t)got);
		if (n < 0) {
			munmap(p, len);
			return MAP_FAILED;
		}
		if (n == 0)
			break;
		got += (size_t)n;
	}
	if (prot != (PROT_READ | PROT_WRITE))
		mprotect(p, len, prot);
	return p;
}

/* a mapping of the pack's bytes is not memory of its own */
int __real_munmap(void *addr, size_t len);
int __wrap_munmap(void *addr, size_t len)
{
	if (memfs_is_image(addr))
		return 0;
	return __real_munmap(addr, len);
}

int mincore(void *addr, size_t len, unsigned char *vec)
{
	(void)addr;
	memset(vec, 1, (len + 4095) / 4096);
	return 0;
}

int sigaltstack(const stack_t *restrict ss, stack_t *restrict old)
{
	(void)ss;
	if (old)
		memset(old, 0, sizeof *old);
	return 0;
}

/* The box delivers no signals, so there is no mask to save: sigsetjmp is
 * setjmp (HotSpot's crash protection, used only while reporting an error). */
__asm__(".text\n.globl sigsetjmp\n.type sigsetjmp,@function\nsigsetjmp:\n\tjmp setjmp\n");

/* Zero's budget (bytecodeInterpreter.cpp, CHIMERA_ZERO_BUDGET): 1000 backward
 * branches or method entries cost 20 us of the machine's time, and the
 * sleep that charges it gives the other threads their turn. */
void chimera_zero_budget(void)
{
	const struct timespec ts = { 0, 20000 };
	nanosleep(&ts, NULL);
}

/* Files live in guest memory with fixed modes; the mask is the usual one. */
mode_t umask(mode_t mask)
{
	static mode_t current = 022;
	const mode_t old = current;
	current = mask & 0777;
	return old;
}
