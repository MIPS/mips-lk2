#
# Copyright (c) 2013-2015, Google, Inc. All rights reserved
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS := \
	$(LOCAL_DIR)/trusty.c \
	$(LOCAL_DIR)/trusty_app.c \
	$(LOCAL_DIR)/syscall.c \
	$(LOCAL_DIR)/handle.c \
	$(LOCAL_DIR)/uctx.c \
	$(LOCAL_DIR)/ipc.c \
	$(LOCAL_DIR)/ipc_msg.c \
	$(LOCAL_DIR)/iovec.c \
	$(LOCAL_DIR)/uuid.c

ifeq (true,$(call TOBOOL,$(WITH_TRUSTY_IPC)))
GLOBAL_DEFINES += WITH_TRUSTY_IPC=1
endif # WITH_TRUSTY_IPC

ifeq (true,$(call TOBOOL,$(WITH_TRUSTY_TIPC_DEV)))
GLOBAL_DEFINES += WITH_TRUSTY_TIPC_DEV=1
MODULE_SRCS += \
	$(LOCAL_DIR)/vqueue.c \
	$(LOCAL_DIR)/tipc_dev.c \
	$(LOCAL_DIR)/tipc_config.c
endif # WITH_TRUSTY_TIPC_DEV

GLOBAL_INCLUDES += \
	$(LOCAL_DIR)/include \

MODULE_DEPS += \
	lib/uthread \
	lib/syscall \
	lib/version \

GLOBAL_DEFINES += \
	WITH_SYSCALL_TABLE=1 \

ifeq (true,$(call TOBOOL,$(WITH_UPSTREAM_LK)))

ARCH_PAGE_SIZE ?= 0x1000

# rules for generating the linker
$(BUILDDIR)/trusty_apps.ld: $(LOCAL_DIR)/trusty_apps.ld linkerscript.phony
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)sed "s/%PAGE_SIZE%/$(ARCH_PAGE_SIZE)/" < $< > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)

linkerscript.phony:
.PHONY: linkerscript.phony

GENERATED += \
	$(BUILDDIR)/trusty_apps.ld \

EXTRA_LINKER_SCRIPTS += $(BUILDDIR)/trusty_apps.ld

endif

include make/module.mk
