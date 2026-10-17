/* The machine's files, served from its own memory.
 *
 * The sandbox's file table (which files are open, where each read position
 * is) lives in the host and is not part of a savestate. A program that keeps
 * files open across frames - the VM keeps the class library open for good,
 * and a jar while it reads it - would find, after a load, the descriptors of
 * a future that never happened still open and its positions wherever that
 * future left them. So every path the VM and its libraries open is answered
 * here, from guest memory, where a state carries it:
 *
 * - The pack (make-pack.py) of read-only files - the runtime image, the
 *   program's jars, fonts - is built into the core's image (pack.S) and
 *   served from there: those bytes are in no state, and a read-only mapping
 *   of one of them (the VM maps the class library whole) is the pack's own
 *   bytes, with nothing copied.
 * - Files the host mounts (the settings, the game) are copied in by Init,
 *   before the seal, so they are part of the sealed image too.
 * - Files created later (the program's settings, logs, a game's save data)
 *   are writable and grow in guest memory, so a state carries them.
 * - Directories are every parent of a file plus those made with mkdir.
 *   Listings come out sorted by name: the same order every run.
 * - Relative paths are taken from "/" (getcwd answers "/").
 *
 * Linked with --wrap for each call: HotSpot's and the JDK's references come
 * here, musl's own internals keep the real ones. Descriptors from FD_BASE
 * up are ours; below it (stdin, stdout, stderr) they are the sandbox's.
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#include "memfs.h"

#define FD_BASE 1000
#define MAX_OPEN 256
#define PATHMAX 1024

typedef struct {
	char *path;           /* normalised, absolute */
	unsigned char *data;
	size_t size, cap;
	int writable, is_dir;
} mfile;

static mfile *g_files;
static int g_nfiles, g_capfiles;

typedef struct {
	int used, file, append, acc;
	size_t pos;
} mhandle;
static mhandle g_open[MAX_OPEN];

int __real_open(const char *path, int flags, ...);
ssize_t __real_read(int fd, void *buf, size_t n);
int __real_close(int fd);
int __real_fstat(int fd, struct stat *st);

/* ---- paths ---- */

/* "/a/./b//../c" and "a/b" -> "/a/c" and "/a/b"; 0 if too long */
static int norm(const char *in, char *out)
{
	char tmp[PATHMAX];
	if (strlen(in) + 2 >= PATHMAX)
		return 0;
	snprintf(tmp, sizeof tmp, "%s%s", in[0] == '/' ? "" : "/", in);
	size_t o = 0;
	out[0] = '\0';
	for (char *seg = strtok(tmp, "/"); seg; seg = strtok(NULL, "/")) {
		if (strcmp(seg, ".") == 0)
			continue;
		if (strcmp(seg, "..") == 0) {
			while (o > 0 && out[o - 1] != '/')
				o--;
			if (o > 0)
				o--;
			out[o] = '\0';
			continue;
		}
		out[o++] = '/';
		strcpy(out + o, seg);
		o += strlen(seg);
	}
	if (o == 0)
		strcpy(out, "/");
	return 1;
}

static int find(const char *npath)
{
	for (int i = 0; i < g_nfiles; i++)
		if (strcmp(g_files[i].path, npath) == 0)
			return i;
	return -1;
}

static int add(const char *npath, int is_dir, int writable, unsigned char *data, size_t size)
{
	if (g_nfiles == g_capfiles) {
		int cap = g_capfiles ? g_capfiles * 2 : 64;
		mfile *f = realloc(g_files, (size_t)cap * sizeof *f);
		if (!f)
			return -1;
		g_files = f;
		g_capfiles = cap;
	}
	mfile *f = &g_files[g_nfiles];
	memset(f, 0, sizeof *f);
	f->path = strdup(npath);
	f->is_dir = is_dir;
	f->writable = writable;
	f->data = data;
	f->size = f->cap = size;
	return g_nfiles++;
}

static void parent_of(const char *npath, char *out)
{
	strcpy(out, npath);
	char *slash = strrchr(out, '/');
	if (slash == out)
		out[1] = '\0';
	else
		*slash = '\0';
}

static int is_dir_path(const char *npath)
{
	if (strcmp(npath, "/") == 0)
		return 1;
	const int i = find(npath);
	return i >= 0 && g_files[i].is_dir;
}

/* every parent directory of npath, made if missing */
static void add_parents(const char *npath)
{
	char p[PATHMAX];
	parent_of(npath, p);
	if (strcmp(p, "/") == 0 || find(p) >= 0)
		return;
	add_parents(p);
	add(p, 1, 0, NULL, 0);
}

/* ---- the pack, and files from the host ---- */

static uint64_t le(const unsigned char *p, int n)
{
	uint64_t v = 0;
	for (int i = n - 1; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

/* the pack's bytes, which a read-only mapping of one of its files points into */
static const unsigned char *g_image;
static size_t g_imageSize;

/* Pack (make-pack.py): "CHIMPAK2", u32 count, then count x (u16 name
 * length, name, u64 offset, u64 size); every file's bytes start at an offset
 * that is a multiple of 4096 from the pack's start. Names are absolute. */
int memfs_mount_image(const unsigned char *image, size_t size)
{
	if (size < 12 || memcmp(image, "CHIMPAK2", 8) != 0)
		return 0;
	const uint32_t count = (uint32_t)le(image + 8, 4);
	size_t p = 12;
	for (uint32_t i = 0; i < count; i++) {
		if (p + 2 > size)
			return 0;
		const size_t nl = (size_t)le(image + p, 2);
		p += 2;
		if (p + nl + 16 > size || nl >= PATHMAX)
			return 0;
		char path[PATHMAX], npath[PATHMAX];
		memcpy(path, image + p, nl);
		path[nl] = '\0';
		p += nl;
		const size_t off = (size_t)le(image + p, 8), len = (size_t)le(image + p + 8, 8);
		p += 16;
		if (off > size || len > size - off || !norm(path, npath))
			return 0;
		add_parents(npath);
		add(npath, 0, 0, (unsigned char *)image + off, len);
	}
	g_image = image;
	g_imageSize = size;
	return 1;
}

int memfs_import(const char *host_name, const char *guest_path)
{
	char np[PATHMAX];
	if (!norm(guest_path, np))
		return 0;
	int fd = __real_open(host_name, O_RDONLY);
	if (fd < 0)
		return 0;
	struct stat st;
	if (__real_fstat(fd, &st) != 0) {
		__real_close(fd);
		return 0;
	}
	unsigned char *buf = malloc(st.st_size > 0 ? (size_t)st.st_size : 1);
	size_t got = 0;
	while (buf && got < (size_t)st.st_size) {
		ssize_t n = __real_read(fd, buf + got, (size_t)st.st_size - got);
		if (n <= 0)
			break;
		got += (size_t)n;
	}
	__real_close(fd);
	if (!buf || got != (size_t)st.st_size) {
		free(buf);
		return 0;
	}
	add_parents(np);
	return add(np, 0, 0, buf, got) >= 0;
}

/* ---- descriptors ---- */

static int handle(int fd)
{
	const int h = fd - FD_BASE;
	return (h >= 0 && h < MAX_OPEN && g_open[h].used) ? h : -1;
}

static void fill_stat(int file, struct stat *st)
{
	memset(st, 0, sizeof *st);
	st->st_dev = 1;
	st->st_ino = 100 + (ino_t)file;
	st->st_nlink = 1;
	st->st_mode = g_files[file].is_dir ? (S_IFDIR | 0755) : (S_IFREG | (g_files[file].writable ? 0644 : 0444));
	st->st_size = (off_t)g_files[file].size;
	st->st_blksize = 4096;
	st->st_blocks = (blkcnt_t)((g_files[file].size + 511) / 512);
	st->st_mtim.tv_sec = 1262304000; /* the sandbox's own constant */
	st->st_atim = st->st_ctim = st->st_mtim;
}

static void fill_root_stat(struct stat *st)
{
	memset(st, 0, sizeof *st);
	st->st_dev = 1;
	st->st_ino = 2;
	st->st_nlink = 1;
	st->st_mode = S_IFDIR | 0755;
	st->st_blksize = 4096;
	st->st_mtim.tv_sec = 1262304000;
	st->st_atim = st->st_ctim = st->st_mtim;
}

static int new_handle(int file, int flags)
{
	for (int h = 0; h < MAX_OPEN; h++)
		if (!g_open[h].used) {
			g_open[h].used = 1;
			g_open[h].file = file;
			g_open[h].pos = 0;
			g_open[h].append = !!(flags & O_APPEND);
			g_open[h].acc = flags & O_ACCMODE;
			return FD_BASE + h;
		}
	errno = EMFILE;
	return -1;
}

static int grow(mfile *f, size_t need)
{
	if (need <= f->cap)
		return 1;
	size_t cap = f->cap ? f->cap : 256;
	while (cap < need)
		cap *= 2;
	unsigned char *d = realloc(f->data, cap);
	if (!d)
		return 0;
	memset(d + f->cap, 0, cap - f->cap);
	f->data = d;
	f->cap = cap;
	return 1;
}

int __wrap_open(const char *path, int flags, ...)
{
	char np[PATHMAX];
	if (!norm(path, np)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	const int acc = flags & O_ACCMODE;
	int file = find(np);
	if (file < 0 && strcmp(np, "/") != 0) {
		char parent[PATHMAX];
		parent_of(np, parent);
		if (!(flags & O_CREAT)) {
			errno = ENOENT;
			return -1;
		}
		if (!is_dir_path(parent)) {
			errno = ENOENT;
			return -1;
		}
		file = add(np, 0, 1, NULL, 0);
		if (file < 0) {
			errno = ENOMEM;
			return -1;
		}
	} else if (file >= 0 && (flags & O_CREAT) && (flags & O_EXCL)) {
		errno = EEXIST;
		return -1;
	}
	if (file < 0 || g_files[file].is_dir) {
		/* a directory opens read-only; the root is not a file entry */
		if (acc != O_RDONLY) {
			errno = EISDIR;
			return -1;
		}
		if (file < 0)
			file = add("/", 1, 0, NULL, 0);
	} else {
		if ((flags & O_DIRECTORY)) {
			errno = ENOTDIR;
			return -1;
		}
		if (acc != O_RDONLY && !g_files[file].writable) {
			errno = EACCES;
			return -1;
		}
		if ((flags & O_TRUNC) && acc != O_RDONLY)
			g_files[file].size = 0;
	}
	return new_handle(file, flags);
}

int __wrap_creat(const char *path, mode_t mode)
{
	return __wrap_open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

static ssize_t read_at(int h, void *buf, size_t n, size_t pos)
{
	const mfile *f = &g_files[g_open[h].file];
	if (f->is_dir) {
		errno = EISDIR;
		return -1;
	}
	if (pos >= f->size)
		return 0;
	if (n > f->size - pos)
		n = f->size - pos;
	memcpy(buf, f->data + pos, n);
	return (ssize_t)n;
}

static ssize_t write_at(int h, const void *buf, size_t n, size_t pos)
{
	mfile *f = &g_files[g_open[h].file];
	if (g_open[h].acc == O_RDONLY || !f->writable) {
		errno = EBADF;
		return -1;
	}
	if (!grow(f, pos + n)) {
		errno = ENOSPC;
		return -1;
	}
	memcpy(f->data + pos, buf, n);
	if (pos + n > f->size)
		f->size = pos + n;
	return (ssize_t)n;
}

ssize_t __real_readv(int fd, const struct iovec *iov, int n);
ssize_t __real_write(int fd, const void *buf, size_t n);
ssize_t __real_writev(int fd, const struct iovec *iov, int n);
ssize_t __real_pread(int fd, void *buf, size_t n, off_t off);
ssize_t __real_pwrite(int fd, const void *buf, size_t n, off_t off);
off_t __real_lseek(int fd, off_t off, int whence);
int __real_stat(const char *restrict path, struct stat *restrict st);
int __real_lstat(const char *restrict path, struct stat *restrict st);
int __real_fcntl(int fd, int cmd, ...);
int __real_ftruncate(int fd, off_t len);
int __real_fsync(int fd);
int __real_fdatasync(int fd);
int __real_isatty(int fd);
int __real_fstatvfs(int fd, struct statvfs *st);

ssize_t __wrap_read(int fd, void *buf, size_t n)
{
	const int h = handle(fd);
	if (h < 0)
		return __real_read(fd, buf, n);
	const ssize_t r = read_at(h, buf, n, g_open[h].pos);
	if (r > 0)
		g_open[h].pos += (size_t)r;
	return r;
}

ssize_t __wrap_write(int fd, const void *buf, size_t n)
{
	const int h = handle(fd);
	if (h < 0)
		return __real_write(fd, buf, n);
	if (g_open[h].append)
		g_open[h].pos = g_files[g_open[h].file].size;
	const ssize_t r = write_at(h, buf, n, g_open[h].pos);
	if (r > 0)
		g_open[h].pos += (size_t)r;
	return r;
}

ssize_t __wrap_readv(int fd, const struct iovec *iov, int cnt)
{
	if (handle(fd) < 0)
		return __real_readv(fd, iov, cnt);
	ssize_t total = 0;
	for (int i = 0; i < cnt; i++) {
		const ssize_t r = __wrap_read(fd, iov[i].iov_base, iov[i].iov_len);
		if (r < 0)
			return total ? total : r;
		total += r;
		if ((size_t)r < iov[i].iov_len)
			break;
	}
	return total;
}

ssize_t __wrap_writev(int fd, const struct iovec *iov, int cnt)
{
	if (handle(fd) < 0)
		return __real_writev(fd, iov, cnt);
	ssize_t total = 0;
	for (int i = 0; i < cnt; i++) {
		const ssize_t r = __wrap_write(fd, iov[i].iov_base, iov[i].iov_len);
		if (r < 0)
			return total ? total : r;
		total += r;
	}
	return total;
}

ssize_t __wrap_pread(int fd, void *buf, size_t n, off_t off)
{
	const int h = handle(fd);
	if (h < 0)
		return __real_pread(fd, buf, n, off);
	if (off < 0) {
		errno = EINVAL;
		return -1;
	}
	return read_at(h, buf, n, (size_t)off);
}

ssize_t __wrap_pwrite(int fd, const void *buf, size_t n, off_t off)
{
	const int h = handle(fd);
	if (h < 0)
		return __real_pwrite(fd, buf, n, off);
	if (off < 0) {
		errno = EINVAL;
		return -1;
	}
	return write_at(h, buf, n, (size_t)off);
}

off_t __wrap_lseek(int fd, off_t off, int whence)
{
	const int h = handle(fd);
	if (h < 0)
		return __real_lseek(fd, off, whence);
	const off_t base = whence == SEEK_SET ? 0
	                 : whence == SEEK_CUR ? (off_t)g_open[h].pos
	                 : whence == SEEK_END ? (off_t)g_files[g_open[h].file].size : -1;
	if (base < 0 || base + off < 0) {
		errno = EINVAL;
		return -1;
	}
	g_open[h].pos = (size_t)(base + off);
	return base + off;
}

int __wrap_close(int fd)
{
	const int h = handle(fd);
	if (h < 0)
		return __real_close(fd);
	g_open[h].used = 0;
	return 0;
}

int __wrap_fstat(int fd, struct stat *st)
{
	const int h = handle(fd);
	if (h < 0)
		return __real_fstat(fd, st);
	fill_stat(g_open[h].file, st);
	return 0;
}

static int stat_path(const char *path, struct stat *st)
{
	char np[PATHMAX];
	if (!norm(path, np)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	if (strcmp(np, "/") == 0) {
		fill_root_stat(st);
		return 0;
	}
	const int file = find(np);
	if (file < 0) {
		errno = ENOENT;
		return -1;
	}
	fill_stat(file, st);
	return 0;
}

int __wrap_stat(const char *restrict path, struct stat *restrict st)
{
	return stat_path(path, st);
}

int __wrap_lstat(const char *restrict path, struct stat *restrict st)
{
	return stat_path(path, st);
}

int __wrap_access(const char *path, int mode)
{
	struct stat st;
	if (stat_path(path, &st) != 0)
		return -1;
	if ((mode & W_OK) && !(st.st_mode & 0200)) {
		errno = EACCES;
		return -1;
	}
	return 0;
}

int __wrap_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	va_start(ap, cmd);
	const long arg = va_arg(ap, long);
	va_end(ap);
	const int h = handle(fd);
	if (h < 0)
		return __real_fcntl(fd, cmd, arg);
	switch (cmd) {
	case F_GETFD:
	case F_SETFD:
	case F_SETFL:
	case F_SETLK:
	case F_SETLKW:
		return 0;
	case F_GETFL:
		return g_open[h].acc | (g_open[h].append ? O_APPEND : 0);
	case F_GETLK: {
		struct flock *fl = (struct flock *)arg;
		if (fl)
			fl->l_type = F_UNLCK;
		return 0;
	}
	default:
		errno = EINVAL;
		return -1;
	}
}

int __wrap_ftruncate(int fd, off_t len)
{
	const int h = handle(fd);
	if (h < 0)
		return __real_ftruncate(fd, len);
	mfile *f = &g_files[g_open[h].file];
	if (len < 0 || !f->writable || g_open[h].acc == O_RDONLY) {
		errno = len < 0 ? EINVAL : EBADF;
		return -1;
	}
	if (!grow(f, (size_t)len)) {
		errno = ENOSPC;
		return -1;
	}
	if ((size_t)len > f->size)
		memset(f->data + f->size, 0, (size_t)len - f->size);
	f->size = (size_t)len;
	return 0;
}

int __wrap_fsync(int fd)
{
	return handle(fd) < 0 ? __real_fsync(fd) : 0;
}

int __wrap_fdatasync(int fd)
{
	return handle(fd) < 0 ? __real_fdatasync(fd) : 0;
}

int __wrap_isatty(int fd)
{
	if (handle(fd) >= 0) {
		errno = ENOTTY;
		return 0;
	}
	return __real_isatty(fd);
}

int __wrap_fstatvfs(int fd, struct statvfs *st)
{
	if (handle(fd) < 0)
		return __real_fstatvfs(fd, st);
	memset(st, 0, sizeof *st);
	st->f_bsize = st->f_frsize = 4096;
	st->f_blocks = st->f_bfree = st->f_bavail = 1u << 20;
	st->f_namemax = 255;
	return 0;
}

int __wrap_statvfs(const char *restrict path, struct statvfs *restrict st)
{
	struct stat s;
	if (stat_path(path, &s) != 0)
		return -1;
	memset(st, 0, sizeof *st);
	st->f_bsize = st->f_frsize = 4096;
	st->f_blocks = st->f_bfree = st->f_bavail = 1u << 20;
	st->f_namemax = 255;
	return 0;
}

/* A read-only mapping of a file of the pack: the pack's own bytes, which
 * are part of the core's image and of no savestate. NULL when the mapping
 * cannot be that (another file, a writable mapping, an offset or a file that
 * does not start on a page). */
const void *memfs_map(int fd, size_t len, off_t off, int writable)
{
	const int h = handle(fd);
	if (h < 0 || writable || off < 0 || (off & 4095) != 0)
		return NULL;
	const mfile *f = &g_files[g_open[h].file];
	if (f->is_dir || f->writable || !g_image || f->data < g_image || f->data >= g_image + g_imageSize)
		return NULL;
	if (((uintptr_t)f->data & 4095) != 0 || (size_t)off > f->size || len > f->size - (size_t)off + 4095)
		return NULL;
	return f->data + off;
}

int memfs_is_image(const void *addr)
{
	return g_image && (const unsigned char *)addr >= g_image && (const unsigned char *)addr < g_image + g_imageSize;
}

/* The files under a folder that a program wrote (not the pack's), sorted
 * by path: what Export Save Data lists. Indices stay valid until a file is
 * made or removed, which only the machine does, while it runs. */
static int cmp_files(const void *a, const void *b)
{
	return strcmp(g_files[*(const int *)a].path, g_files[*(const int *)b].path);
}

int memfs_list(const char *prefix, int *out, int max)
{
	const size_t n = strlen(prefix);
	int count = 0;
	for (int i = 0; i < g_nfiles && count < max; i++)
		if (g_files[i].writable && !g_files[i].is_dir && strncmp(g_files[i].path, prefix, n) == 0)
			out[count++] = i;
	qsort(out, (size_t)count, sizeof *out, cmp_files);
	return count;
}

const char *memfs_path(int file) { return g_files[file].path; }

const unsigned char *memfs_data(int file, size_t *size)
{
	*size = g_files[file].size;
	return g_files[file].data;
}

/* ---- names ---- */

int __wrap_mkdir(const char *path, mode_t mode)
{
	(void)mode;
	char np[PATHMAX], parent[PATHMAX];
	if (!norm(path, np)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	if (strcmp(np, "/") == 0 || find(np) >= 0) {
		errno = EEXIST;
		return -1;
	}
	parent_of(np, parent);
	if (!is_dir_path(parent)) {
		errno = ENOENT;
		return -1;
	}
	return add(np, 1, 1, NULL, 0) < 0 ? (errno = ENOMEM, -1) : 0;
}

static int has_children(const char *np)
{
	const size_t n = strlen(np);
	for (int i = 0; i < g_nfiles; i++)
		if (strncmp(g_files[i].path, np, n) == 0 && g_files[i].path[n] == '/')
			return 1;
	return 0;
}

static int is_open(int file)
{
	for (int h = 0; h < MAX_OPEN; h++)
		if (g_open[h].used && g_open[h].file == file)
			return 1;
	return 0;
}

static void drop(int file)
{
	free(g_files[file].path);
	if (g_files[file].writable)
		free(g_files[file].data);
	g_files[file] = g_files[--g_nfiles];
	for (int h = 0; h < MAX_OPEN; h++)
		if (g_open[h].used && g_open[h].file == g_nfiles)
			g_open[h].file = file;
}

static int remove_path(const char *path, int want_dir)
{
	char np[PATHMAX];
	if (!norm(path, np)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	const int file = find(np);
	if (file < 0) {
		errno = ENOENT;
		return -1;
	}
	if (!g_files[file].writable) {
		errno = EROFS;
		return -1;
	}
	if (want_dir == 1 && !g_files[file].is_dir) {
		errno = ENOTDIR;
		return -1;
	}
	if (want_dir == 0 && g_files[file].is_dir) {
		errno = EISDIR;
		return -1;
	}
	if (g_files[file].is_dir && has_children(np)) {
		errno = ENOTEMPTY;
		return -1;
	}
	if (is_open(file)) {
		errno = EBUSY;
		return -1;
	}
	drop(file);
	return 0;
}

int __wrap_unlink(const char *path)
{
	return remove_path(path, 0);
}

int __wrap_rmdir(const char *path)
{
	return remove_path(path, 1);
}

int __wrap_remove(const char *path)
{
	return remove_path(path, -1);
}

int __wrap_rename(const char *from, const char *to)
{
	char nf[PATHMAX], nt[PATHMAX], parent[PATHMAX];
	if (!norm(from, nf) || !norm(to, nt)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	const int file = find(nf);
	if (file < 0) {
		errno = ENOENT;
		return -1;
	}
	if (!g_files[file].writable || g_files[file].is_dir) {
		errno = g_files[file].is_dir ? EXDEV : EROFS;
		return -1;
	}
	parent_of(nt, parent);
	if (!is_dir_path(parent)) {
		errno = ENOENT;
		return -1;
	}
	const int old = find(nt);
	if (old >= 0) {
		if (g_files[old].is_dir || !g_files[old].writable || is_open(old)) {
			errno = g_files[old].is_dir ? EISDIR : !g_files[old].writable ? EROFS : EBUSY;
			return -1;
		}
		drop(old);
	}
	const int f2 = find(nf); /* drop may have moved it */
	char *p = strdup(nt);
	if (!p) {
		errno = ENOMEM;
		return -1;
	}
	free(g_files[f2].path);
	g_files[f2].path = p;
	return 0;
}

int __wrap_chmod(const char *path, mode_t mode)
{
	(void)mode;
	struct stat st;
	return stat_path(path, &st);
}

int __wrap_fchmod(int fd, mode_t mode)
{
	(void)mode;
	if (handle(fd) < 0) {
		errno = EBADF;
		return -1;
	}
	return 0;
}

int __wrap_chown(const char *path, uid_t u, gid_t g)
{
	(void)u;
	(void)g;
	struct stat st;
	return stat_path(path, &st);
}

int __wrap_utimes(const char *path, const struct timeval tv[2])
{
	(void)tv;
	struct stat st;
	return stat_path(path, &st);
}

int __wrap_link(const char *a, const char *b)
{
	(void)a;
	(void)b;
	errno = EPERM;
	return -1;
}

int __wrap_symlink(const char *a, const char *b)
{
	(void)a;
	(void)b;
	errno = EPERM;
	return -1;
}

ssize_t __wrap_readlink(const char *restrict path, char *restrict buf, size_t n)
{
	(void)buf;
	(void)n;
	struct stat st;
	if (stat_path(path, &st) != 0)
		return -1;
	errno = EINVAL;
	return -1;
}

int __wrap_chdir(const char *path)
{
	char np[PATHMAX];
	if (!norm(path, np) || !is_dir_path(np)) {
		errno = ENOENT;
		return -1;
	}
	return strcmp(np, "/") == 0 ? 0 : (errno = EACCES, -1);
}

int __wrap_flock(int fd, int op)
{
	(void)op;
	return handle(fd) < 0 ? (errno = EBADF, -1) : 0;
}

int __wrap_posix_fadvise(int fd, off_t off, off_t len, int advice)
{
	(void)off;
	(void)len;
	(void)advice;
	return handle(fd) < 0 ? EBADF : 0;
}

/* ---- directories ---- */

typedef struct {
	char **names;
	int n, next;
	struct dirent ent;
} mdir;

static int cmp_names(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

DIR *__wrap_opendir(const char *path)
{
	char np[PATHMAX];
	if (!norm(path, np)) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	if (!is_dir_path(np)) {
		errno = find(np) >= 0 ? ENOTDIR : ENOENT;
		return NULL;
	}
	mdir *d = calloc(1, sizeof *d);
	if (!d)
		return NULL;
	const size_t n = strcmp(np, "/") == 0 ? 0 : strlen(np);
	d->names = malloc((size_t)(g_nfiles + 2) * sizeof *d->names);
	d->names[d->n++] = strdup(".");
	d->names[d->n++] = strdup("..");
	for (int i = 0; i < g_nfiles; i++) {
		const char *p = g_files[i].path;
		if (strncmp(p, np, n) == 0 && p[n] == '/' && p[n + 1] && !strchr(p + n + 1, '/'))
			d->names[d->n++] = strdup(p + n + 1);
	}
	qsort(d->names + 2, (size_t)d->n - 2, sizeof *d->names, cmp_names);
	return (DIR *)d;
}

struct dirent *__wrap_readdir(DIR *dir)
{
	mdir *d = (mdir *)dir;
	if (d->next >= d->n)
		return NULL;
	memset(&d->ent, 0, sizeof d->ent);
	snprintf(d->ent.d_name, sizeof d->ent.d_name, "%s", d->names[d->next]);
	d->ent.d_ino = 1000 + (ino_t)d->next;
	d->ent.d_type = DT_UNKNOWN;
	d->next++;
	return &d->ent;
}

int __wrap_closedir(DIR *dir)
{
	mdir *d = (mdir *)dir;
	for (int i = 0; i < d->n; i++)
		free(d->names[i]);
	free(d->names);
	free(d);
	return 0;
}

/* ---- stdio on our files (HotSpot reads small files with fopen) ---- */

static ssize_t ck_read(void *c, char *buf, size_t n)
{
	return __wrap_read((int)(intptr_t)c, buf, n);
}

static ssize_t ck_write(void *c, const char *buf, size_t n)
{
	return __wrap_write((int)(intptr_t)c, buf, n);
}

static int ck_seek(void *c, off_t *off, int whence)
{
	const off_t r = __wrap_lseek((int)(intptr_t)c, *off, whence);
	if (r < 0)
		return -1;
	*off = r;
	return 0;
}

static int ck_close(void *c)
{
	return __wrap_close((int)(intptr_t)c);
}

FILE *__wrap_fopen(const char *restrict path, const char *restrict mode)
{
	int flags;
	switch (mode[0]) {
	case 'r': flags = strchr(mode, '+') ? O_RDWR : O_RDONLY; break;
	case 'w': flags = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
	case 'a': flags = (strchr(mode, '+') ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
	default: errno = EINVAL; return NULL;
	}
	const int fd = __wrap_open(path, flags, 0644);
	if (fd < 0)
		return NULL;
	cookie_io_functions_t io = { ck_read, ck_write, ck_seek, ck_close };
	FILE *f = fopencookie((void *)(intptr_t)fd, mode, io);
	if (!f)
		__wrap_close(fd);
	return f;
}
