#define _GNU_SOURCE

/*
gcc -g -Og -Wall -Werror -fPIC -shared -o deterministic_random_preload.so deterministic_random_preload.c
gcc    -O2 -Wall -Werror -fPIC -shared -o deterministic_random_preload.so deterministic_random_preload.c
LD_PRELOAD=./deterministic_random_preload.so python -c 'import random; print(random.randint(0, 99))'
gdb --command=cpython/Misc/gdbinit --args env LD_PRELOAD=./deterministic_random_preload.so cpython/python -c 'import random; print(random.randint(0, 99))'

env \
	FAKETIME="2022-01-01 00:00:00" \
	LD_PRELOAD=/usr/lib/x86_64-linux-gnu/faketime/libfaketime.so.1:$PWD/deterministic_random_preload.so \
	setarch $(arch) --addr-no-randomize \
	$rest_of_command
 */

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

#define ENABLE true
#define PRINT_INTERCEPTION false
#define PRINT_CALL false
// Note that it is traditional to use #ifdef or #if defined(...) for compile-time switches,
// but I will use normal if(...), for cases where both branches will compile.
// This means I can fold them into boolean expressions (e.g., ENABLE && !disable).
// The compiler will produce the same code.

/*
 * The greatest possible number of open("/dev/random")s that may occur.
 */
#define MAX_RANDOM_FDS 8

typedef struct {
	bool initialized;
	int random_fds[MAX_RANDOM_FDS];
	size_t used_random_fds;
	mt_state random_state;
	int (*real_open)(const char*, int, mode_t);
	size_t (*real_read)(int, void*, size_t);
	int (*real_close)(int);
	size_t (*real_getrandom)(void*, size_t, unsigned int);
	int (*real_getentropy)(void*, size_t);
} process_state_t;

process_state_t process_state;

void INTERNAL ensure_initialized() {
	if (!LIKELY(process_state.initialized)) {
		if (PRINT_INTERCEPTION) {
			printf("Intercepting: initializaiton\n");
		}
		process_state.initialized = true;
		process_state.real_open = dlsym(RTLD_NEXT, "open");
		process_state.real_read = dlsym(RTLD_NEXT, "read");
		process_state.real_close = dlsym(RTLD_NEXT, "close");
		process_state.real_getrandom = dlsym(RTLD_NEXT, "getrandom");
		process_state.real_getentropy = dlsym(RTLD_NEXT, "getentropy");
		process_state.used_random_fds = 0;
		mt_init(&process_state.random_state, 12345);
	}
}

bool INTERNAL full_random_fd() { return process_state.used_random_fds + 1 > MAX_RANDOM_FDS; }
void INTERNAL set_random_fd(int fd) {
	process_state.random_fds[process_state.used_random_fds] = fd;
	process_state.used_random_fds++;
}
mt_state* INTERNAL get_random_fd(int fd) {
	if (fd == 0) {
		return NULL;
	}
	for (size_t i = 0; i < process_state.used_random_fds; ++i) {
		if (UNLIKELY(process_state.random_fds[i] == fd)) {
			return &process_state.random_state;
		}
	}
	return NULL;
}

void INTERNAL fill_with_random(mt_state* state, uint32_t* buffer, size_t size) {
	for (size_t i = 0; i < size / sizeof(uint32_t); ++i) {
		buffer[i] = mt_random(state);
		if (PRINT_INTERCEPTION) {
			printf("%08x", buffer[i]);
		}
	}
	if (PRINT_INTERCEPTION) {
		printf("\n");
	}
}

bool INTERNAL remove_random_fd_if_exists(int fd) {
	for (size_t i = 0; i < process_state.used_random_fds; ++i) {
		if (UNLIKELY(process_state.random_fds[i] == fd)) {
			process_state.random_fds[fd] = 0;
			return true;
		}
	}
	return false;
}

/* int open(const char* pathname, int flags, mode_t mode) { */
/* 	ensure_initialized(); */
/* 	if (PRINT_CALL) { */
/* 		printf("Called open(%s, %d, %d)\n", pathname, flags, mode); */
/* 	} */
/* 	if (ENABLE && LIKELY(pathname != NULL) && UNLIKELY(strcmp(pathname, "/dev/random") || strcmp(pathname, "/dev/urandom"))) { */
/* 		if (LIKELY(!full_random_fd())) { */
/* 			int fd = process_state.real_open(pathname, flags, mode); */
/* 			if (PRINT_INTERCEPTION) { */
/* 				printf("Intercepting open(%s, %d, %d) = %d\n", pathname, flags, mode, fd); */
/* 			} */
/* 			set_random_fd(fd); */
/* 			return fd; */
/* 		} else { */
/* 			return -EMFILE; */
/* 		} */
/* 	} else { */
/* 		return process_state.real_open(pathname, flags, mode); */
/* 	} */
/* } */

/* int close(int fd) { */
/* 	ensure_initialized(); */
/* 	if (PRINT_CALL) { */
/* 		printf("Called close(%d)\n", fd); */
/* 	} */
/* 	if (ENABLE && remove_random_fd_if_exists(fd) && PRINT_INTERCEPTION) { */
/* 		printf("Intercepting close(%d)\n", fd); */
/* 	} */
/* 	return process_state.real_close(fd); */
/* } */

/* ssize_t read(int fd, void *buffer, size_t size) { */
/* 	ensure_initialized(); */
/* 	if (PRINT_CALL) { */
/* 		printf("Called read(%d, %p, %ld)\n", fd, buffer, size); */
/* 	} */
/* 	mt_state* state; */
/* 	if (ENABLE && UNLIKELY(NULL != (state = get_random_fd(fd)))) { */
/* 		if (PRINT_INTERCEPTION) { */
/* 			printf("Intercepting read(%d, %p, %ld)\n", fd, buffer, size); */
/* 		} */
/* 		fill_with_random(&process_state.random_state, buffer, size); */
/* 		return size; */
/* 	} else { */
/* 		return process_state.real_read(fd, buffer, size); */
/* 	} */
/* } */

ssize_t getrandom(void *buffer, size_t size, unsigned int flags) {
	ensure_initialized();
	if (ENABLE) {
		if (PRINT_INTERCEPTION) {
			printf("Intercepting getrandom(%p, %ld, %d)\n", buffer, size, flags);
		}
		fill_with_random(&process_state.random_state, buffer, size);
		return size;
	} else {
		return process_state.real_getrandom(buffer, size, flags);
	}
}

int getentropy(void *buffer, size_t size) {
	ensure_initialized();
	if (PRINT_CALL) {
		printf("Called getentropy(%p, %ld)\n", buffer, size);
	}
	if (ENABLE) {
		if (PRINT_INTERCEPTION) {
			printf("Intercepting getentropy(%p, %ld)\n", buffer, size);
		}
		fill_with_random(&process_state.random_state, buffer, size);
		return size;
	} else {
		return process_state.real_getentropy(buffer, size);
	}
}
