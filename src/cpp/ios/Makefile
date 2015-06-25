PROJECT_HOME = ../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

OCPPSRCS += MCRPC.mm

LIB_NAME = MCRPC
INCLUDE_DIRS += ../rpc
LDFLAGS += -std=gnu++11 -stdlib=libc++
CXXFLAGS += -std=gnu++11 -stdlib=libc++

include $(BUILD_DIR)/make.rules

