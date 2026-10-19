RACK_DIR ?= /c/dev/Rack-SDK
FLAGS += -Icore
SOURCES += $(wildcard src/*.cpp)
DISTRIBUTABLES += res LICENSE
include $(RACK_DIR)/plugin.mk

test:
	$(MAKE) -C tests
.PHONY: test
