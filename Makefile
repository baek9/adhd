# Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
ifndef BOARD
$(error 'BOARD' is not defined.  Unable to build gavd.)
endif

export ADHD_DIR = $(shell pwd)
include $(ADHD_DIR)/defs/definitions.mk

all:	gavd adhdinfo cras

adhdinfo gavd::	lib

gavd::		cras

lib gavd adhdinfo::
	@$(call remake,Building,$@,Makefile,$@)

cras:
	@$(call remake,Building,$@,cras.mk,$@)

cras_install:
	@$(call remake,Building,cras,cras.mk,$@)

$(DESTDIR)/etc/init/adhd.conf:	$(ADHD_DIR)/upstart/adhd.conf
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

$(DESTDIR)/lib/udev/rules.d/99-dev-input-group.rules:	\
	$(ADHD_DIR)/udev/99-dev-input-group.rules
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

$(DESTDIR)/etc/init/cras.conf:	$(ADHD_DIR)/upstart/cras.conf
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

$(DESTDIR)/usr/bin/gavd:	$(ADHD_BUILD_DIR)/gavd/gavd
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) -D $< $@

$(DESTDIR)/etc/asound.state:	$(ADHD_DIR)/factory-default/asound.state.$(BOARD)
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

optional_alsa_conf := $(wildcard $(ADHD_DIR)/alsa-module-config/alsa-$(BOARD).conf)

ifneq ($(strip $(optional_alsa_conf)),)

$(DESTDIR)/etc/modprobe.d/alsa-$(BOARD).conf:	$(optional_alsa_conf)
	$(ECHO) "Installing '$<' to '$@'"
	$(INSTALL) --mode 644 -D $< $@

install:	$(DESTDIR)/etc/modprobe.d/alsa-$(BOARD).conf

endif

install:	$(DESTDIR)/etc/init/adhd.conf				\
		$(DESTDIR)/etc/init/cras.conf				\
		$(DESTDIR)/etc/asound.state				\
		$(DESTDIR)/usr/bin/gavd					\
		$(DESTDIR)/lib/udev/rules.d/99-dev-input-group.rules	\
		cras_install
clean:
	@rm -rf $(ADHD_BUILD_DIR)

.PHONY:	gavd clean adhdinfo lib cras cras_install
