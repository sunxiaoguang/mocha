PROJECT_HOME = ../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

CPPSRCS += TestClient.cc TestSpread.cc
OBJS += $(addprefix $(OUTDIR)/,$(CPPSRCS:.cc=$(OBJ_SUFFIX)))
CXXFLAGS += -DMOCA_RPC_HAS_STL

INCLUDE_DIRS += $(PROJECT_HOME)/src/cpp/rpc

EXETARGET = TestClient TestSpread

DEPLIBS += MocaRPC

include $(BUILD_DIR)/make.rules

