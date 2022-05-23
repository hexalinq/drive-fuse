ifdef DEBUG
	CFLAGS:=-g3 -Wall -Werror -fsanitize=address -fsanitize=undefined -fsanitize=leak -fno-sanitize-recover
else
	CFLAGS:=-s -Wall -Werror -Ofast -mtune=native -march=native -fstack-protector-strong
endif

SCHEME?=https
ENDPOINT?=drive.hexalinq.com

ALL_SRC:=$(wildcard *.c)
ALL_HDR:=$(wildcard *.h)

mount.hexalinq-drive: $(ALL_SRC) $(ALL_HDR)
	gcc $(CFLAGS) $(ALL_SRC) -o$@ `pkg-config fuse3 --cflags --libs` -lcurl -DSCHEME=\"$(SCHEME)\" -DENDPOINT=\"$(ENDPOINT)\"

install: mount.hexalinq-drive
	install mount.hexalinq-drive /usr/bin/mount.hexalinq-drive

.PHONY: install
