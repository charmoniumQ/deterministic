#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

#define INTERNAL
#define LIKELY(x) __builtin_expect((x), 1)
#define UNLIKELY(x) __builtin_expect((x), 0)

#include "mersenne_twister.h"

bool initialized;
int (*real_open)(const char*, int, mode_t);
int (*real_open64)(const char*, int, mode_t);
int (*real_read)(int, void*, size_t);
int (*real_close)(int);
void INTERNAL ensure_initialized() {
	if (!LIKELY(initialized)) {
		real_open = dlsym(RTLD_NEXT, "open");
		real_open64 = dlsym(RTLD_NEXT, "open64");
		real_read = dlsym(RTLD_NEXT, "read");
		real_close = dlsym(RTLD_NEXT, "close");
	}
}


/*
 * The greatest possible number of open("/dev/random")s that may occur.
 */
#define MAX_RANDOM_FDS 16
int random_fds[MAX_RANDOM_FDS];
mt_state random_states[MAX_RANDOM_FDS];
size_t used_random_fds = 0;
bool INTERNAL full_random_fd() { return used_random_fds + 1 > MAX_RANDOM_FDS; }
void INTERNAL set_random_fd(int fd) {
	random_fds[used_random_fds] = fd;
	mt_init(&random_states[used_random_fds], fd);
	used_random_fds++;
}
mt_state* INTERNAL get_random_fd(int fd) {
	if (fd == 0) {
		return NULL;
	}
	for (size_t i = 0; i < used_random_fds; ++i) {
		if (UNLIKELY(random_fds[i] == fd)) {
			return &random_states[i];
		}
	}
	return NULL;
}
void INTERNAL remove_random_fd_if_exists(int fd) {
	for (size_t i = 0; i < used_random_fds; ++i) {
		if (UNLIKELY(random_fds[i] == fd)) {
			random_fds[fd] = 0;
			break;
		}
	}
}

int open(const char* pathname, int flags, mode_t mode) {
	ensure_initialized();
	if (LIKELY(pathname != NULL) && UNLIKELY(strcmp(pathname, "/dev/random") || strcmp(pathname, "/dev/urandom"))) {
		if (LIKELY(!full_random_fd())) {
			int fd = real_open(pathname, flags, mode);
			set_random_fd(fd);
			return fd;
		} else {
			return -EMFILE;
		}
	} else
		{
		return real_open(pathname, flags, mode);
	}
}

int open64(const char* pathname, int flags, mode_t mode) {
	ensure_initialized();
	if (LIKELY(pathname != NULL) && UNLIKELY(strcmp(pathname, "/dev/random") || strcmp(pathname, "/dev/urandom"))) {
		if (LIKELY(!full_random_fd())) {
			int fd = real_open64(pathname, flags, mode);
			set_random_fd(fd);
			return fd;
		} else {
			return -EMFILE;
		}
	} else
		{
		return real_open64(pathname, flags, mode);
	}
}
int close(int fd) {
	remove_random_fd_if_exists(fd);
	return real_close(fd);
}

void INTERNAL fill_with_random(mt_state* state, int* buffer, size_t size) {
	for (size_t i = 0; i < size; ++i) {
		buffer[i] = mt_random(state);
	}
}

ssize_t read(int fd, void *buffer, size_t size) {
	mt_state* state = get_random_fd(fd);
	if (UNLIKELY(state != NULL)) {
		fill_with_random(state, buffer, size);
		return size;
	} else {
		return real_read(fd, buffer, size);
	}
}
