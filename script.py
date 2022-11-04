env \
	FAKETIME="2022-01-01 00:00:00" \
	LD_PRELOAD=/usr/lib/x86_64-linux-gnu/faketime/libfaketime.so.1 \
	setarch $(arch) --addr-no-randomize \
	seeded_random_chroot \
	$rest_of_command
