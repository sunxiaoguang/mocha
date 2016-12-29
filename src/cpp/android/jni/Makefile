PROJECT_HOME = ../../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

CPPFLAGS += -DMOCA_RPC_NANO

CPPSRCS += JniChannel.cc RPCChannelNano.cc RPCNano.cc RPCChannelEasy.cc RPCProtocol.cc RPCLogging.cc

INCLUDE_DIRS += ../../include ../../rpc

SOTARGET = mocarpc_nano_jni

ifeq ($(THE_OS), linux)
DEPLIBS += uuid
endif
DEPLIBS += z

include $(BUILD_DIR)/make.rules
