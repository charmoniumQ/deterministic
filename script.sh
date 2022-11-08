#!/usr/bin/env sh

env FAKETIME='2022-01-01 00:00:00' LD_PRELOAD=/usr/lib/x86_64-linux-gnu/faketime/libfaketime.so.1:$PWD/deterministic_random_preload.so setarch $(arch) --addr-no-randomize $@
