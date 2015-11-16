PROJECT_HOME = ../../..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

PRE_BUILD = build 
.PHONY: build

include $(BUILD_DIR)/make.rules

build:
	$(MAKE) -f Makefile.nano
	$(MAKE) -f Makefile.lite
	$(MAKE) -f Makefile.full

clean-all:
	$(MAKE) -f Makefile.nano clean-all
	$(MAKE) -f Makefile.lite clean-all
	$(MAKE) -f Makefile.full clean-all
