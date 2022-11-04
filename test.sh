#!/usr/bin/env sh

set -x -e

gcc -Wall -Werror -fPIC -shared -o deterministic_random_preload.so deterministic_random_preload.c
head -c 50 /dev/random; echo
head -c 50 /dev/random; echo
LD_PRELOAD=$PWD/deterministic_random_preload.so head -c 5 /dev/random; echo
LD_PRELOAD=$PWD/deterministic_random_preload.so head -c 5 /dev/random; echo

python -c 'import random; print(random.randint(0, 99))'
python -c 'import random; print(random.randint(0, 99))'
LD_PRELOAD=$PWD/deterministic_random_preload.so python -c 'import random; print(random.randint(0, 99))'
LD_PRELOAD=$PWD/deterministic_random_preload.so python -c 'import random; print(random.randint(0, 99))'

# See also: -Wl,--wrap=malloc
