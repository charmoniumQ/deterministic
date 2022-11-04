#!/usr/bin/env sh

set -e

gcc -Wall -Werror -fPIC -shared -o deterministic_random_preload.so deterministic_random_preload.c

determinism=LD_PRELOAD=$PWD/deterministic_random_preload.so

head -c 1000 /dev/random | strings -s ''; echo
head -c 1000 /dev/random | strings -s ''; echo
env $determinism head -c 1000 /dev/random | strings -s ''; echo
env $determinism head -c 1000 /dev/random | strings -s ''; echo

strings -s '' /dev/random | head -c 50; echo
strings -s '' /dev/random | head -c 50; echo
env $determinism strings -s '' /dev/random | head -c 50; echo
env $determinism strings -s '' /dev/random | head -c 50; echo

# See also: -Wl,--wrap=malloc
