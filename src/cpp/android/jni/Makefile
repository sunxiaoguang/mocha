PROJECT_HOME = ../../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

CPPSRCS += com_moca_rpc_RPC.cc RPC.cc

INCLUDE_DIRS += ../../rpc

SOTARGET = mocarpc_jni

DEPLIBS += uuid

include $(BUILD_DIR)/make.rules
