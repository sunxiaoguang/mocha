PROJECT_HOME = ../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

OCPPSRCS += MCRPC.mm

LIB_NAME = MCRPC
INCLUDE_DIRS += ../include ../rpc
LDFLAGS += -std=c++11 -stdlib=libc++
CXXFLAGS += -std=c++11 -stdlib=libc++ -fobjc-arc 

include $(BUILD_DIR)/make.rules

