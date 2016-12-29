PROJECT_HOME = .
BUILD_DIR ?= build/make
include $(BUILD_DIR)/make.defs

SUBDIRS += src/cpp/rpc src/cpp/android/jni
ifeq ($(THE_OS), darwin)
	SUBDIRS += src/cpp/ios
endif
SUBDIRS +=test/src/cpp

include $(BUILD_DIR)/make.rules
