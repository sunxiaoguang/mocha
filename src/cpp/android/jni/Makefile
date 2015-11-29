PROJECT_HOME = ../../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

CPPFLAGS += -DMOCA_RPC_NANO

CPPSRCS += com_moca_rpc_nano_RPC.cc RPCClient.cc RPCNano.cc RPCLogging.cc

INCLUDE_DIRS += ../../include ../../rpc

SOTARGET = mocarpc_nano_jni

ifeq ($(THE_OS), linux)
DEPLIBS += uuid
endif
DEPLIBS += z

include $(BUILD_DIR)/make.rules
