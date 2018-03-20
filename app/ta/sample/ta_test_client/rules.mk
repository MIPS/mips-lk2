#
# Copyright (c) 2016-218, MIPS Tech, LLC and/or its affiliated group companies
# (“MIPS”).
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_INCLUDES += \
	$(LOCAL_DIR)/../ta_test_server/include \

MODULE_SRCS += \
	$(LOCAL_DIR)/test_client.c \
	$(LOCAL_DIR)/manifest.c

MODULE_DEPS += \
	app/trusty \
	lib/libc-trusty \
	lib/libutee \

include make/module.mk