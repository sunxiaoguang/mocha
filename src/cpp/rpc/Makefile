PROJECT_HOME = ../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

CPPSRCS += RPCClient.cc RPC.cc
OBJS += $(addprefix $(OUTDIR)/,$(CPPSRCS:.cc=$(OBJ_SUFFIX)))
INCLUDE_DIRS += internal

LIB_NAME = MocaRPC

include $(BUILD_DIR)/make.rules

