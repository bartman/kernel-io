.PHONY: all build clean install
all: build

KVER ?= $(shell uname -r)
KDIR ?= $(shell readlink /lib/modules/${KVER}/build)

USER = $(shell id -u -n)
SUDO = $(shell which sudo)

Q = $(if $V,,@)
PWD := $(shell pwd)

.PHONY: build
build:
	make -C ${KDIR} M=$(PWD) modules

install:
	make -C ${KDIR} M=$(PWD) modules_install \
		$(if ${DESTDIR},INSTALL_MOD_PATH=${DESTDIR},)
	$(if ${DESTDIR},,depmod -a)

clean:
	make -C ${KDIR} M=$(PWD) clean


# ------------------------------------------------------------------------
# loading and unloading

KIO_MODPROBE_CONF = /etc/modprobe.d/kio.conf
ifneq (,$(wildcard ${KIO_MODPROBE_CONF}))
KIO_OPTIONS = $(shell sed -rne 's/^ *options +kio +(.*)$$/\1/p' ${KIO_MODPROBE_CONF})
endif

.PHONY: load reload unload
reload:
	${MAKE} unload
	${MAKE} load

load: all
	sync
	sync
	${SUDO} insmod kio.ko ${KIO_OPTIONS}

unload:
	if lsmod | grep -q '^kio\>' ; then \
		sync ; sync ; \
		${SUDO} rmmod kio ; \
	fi

KIO_DEBUG_FILES=$(shell sudo cut -d : -f 1 /sys/kernel/debug/dynamic_debug/control \
	| sort -u | sed -rne 's,.*kio.*/,,p')
debug-on:
	for c in ${KIO_DEBUG_FILES} ; do \
	echo 'file '$$c' +p' | ${SUDO} tee /sys/kernel/debug/dynamic_debug/control >/dev/null ; \
	done
	${SUDO} sysctl -w kernel.printk=8 >/dev/null

debug-off:
	for c in ${KIO_DEBUG_FILES} ; do \
	echo 'file '$$c' -p' | ${SUDO} tee /sys/kernel/debug/dynamic_debug/control >/dev/null ; \
	done


