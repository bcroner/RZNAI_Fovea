# RZN AI foveal stereo input engine -- reference implementation.
#
#   make            build the demo and the tests
#   make test       build and run the tests
#   make run        build and run the demo on the synthetic scene
#   make DISPARITY=1 ...   include the optional epipolar disparity stage
#   make PROFILE=32 ...    lossless packing; needs the model's in_sz raised

CC      ?= gcc
CSTD    ?= -std=c11
WARN     = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
OPT     ?= -O2
PROFILE ?= 16
CFLAGS   = $(CSTD) $(WARN) $(OPT) -Isrc -DRZN_PACK_PROFILE=$(PROFILE)
LDFLAGS  =

ifeq ($(DISPARITY),1)
CFLAGS += -DRZN_ENABLE_DISPARITY=1
endif

BUILD = build

CORE_SRC = src/rzn_spiral.c src/rzn_pack.c src/rzn_frame.c src/rzn_fovea.c \
           src/rzn_agi_sink.c
ifeq ($(DISPARITY),1)
CORE_SRC += src/rzn_disparity.c
endif

CORE_OBJ = $(patsubst src/%.c,$(BUILD)/%.o,$(CORE_SRC))

DEMO = $(BUILD)/rzn_demo
TEST = $(BUILD)/rzn_test

.PHONY: all test run clean

all: $(DEMO) $(TEST)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/demo_main.o: src/demo_main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test_rzn.o: test/test_rzn.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(DEMO): $(CORE_OBJ) $(BUILD)/demo_main.o
	$(CC) $^ -o $@ $(LDFLAGS)

$(TEST): $(CORE_OBJ) $(BUILD)/test_rzn.o
	$(CC) $^ -o $@ $(LDFLAGS)

test: $(TEST)
	./$(TEST)

run: $(DEMO)
	./$(DEMO)

clean:
	rm -rf $(BUILD)
