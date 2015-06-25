PROJECT_HOME = .
BUILD_DIR ?= build/make
include $(BUILD_DIR)/make.defs

SUBDIRS += src/cpp/rpc src/cpp/ios test/src/cpp

include $(BUILD_DIR)/make.rules
