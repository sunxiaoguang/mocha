PROJECT_HOME = .
BUILD_DIR ?= build/make
include $(BUILD_DIR)/make.defs

LIBUV_ROOT ?= $(CURDIR)/deps/libuv-$(BUILD_PLATFORM)
export LIBUV_ROOT

ifeq ($(wildcard $(LIBUV_ROOT)/lib/*.o), )
	SUBDIRS += deps
endif

SUBDIRS += src/cpp/rpc src/cpp/android/jni
ifeq ($(THE_OS), darwin)
	SUBDIRS += src/cpp/ios
endif
SUBDIRS +=test/src/cpp

include $(BUILD_DIR)/make.rules
