/* memfs.h - the machine's files, served from its own memory (memfs.c).
 * SPDX-License-Identifier: MIT */
#ifndef MEMFS_H
#define MEMFS_H

#include <stddef.h>
#include <sys/types.h>

/* the pack built into the core (make-pack.py); 1 on success */
int memfs_mount_image(const unsigned char *image, size_t size);
/* a copy of a file the host mounted, read-only at guest_path; 1 on success */
int memfs_import(const char *host_name, const char *guest_path);
/* see memfs.c */
const void *memfs_map(int fd, size_t len, off_t off, int writable);
int memfs_is_image(const void *addr);
/* the written files under prefix, sorted by path, as indices; and each one's
 * path (absolute) and bytes */
int memfs_list(const char *prefix, int *out, int max);
const char *memfs_path(int file);
const unsigned char *memfs_data(int file, size_t *size);

#endif
