# ============================================================
# Makefile  --  PC simulator + unit tests for TMS320C5515 port
#
# Targets:
#   make          -> build tms320_sim
#   make tests    -> build all unit tests under build/ and run them
#   make e2e      -> build sim+gen, synthesize, run sim, run compare.py
#   make clean    -> remove build artifacts
# ============================================================

CC       := gcc
CFLAGS   := -std=c99 -Wall -Wextra -O2 -Isrc
LDLIBS   := -lm

BUILD    := build

# ---- main simulator sources ----------------------------------
# Host-only entry point: main_pc.c.  The on-chip counterpart
# (src/app_main.c) is built only when TMS320_C5515 is defined.
SIM_SRCS := \
    src/main_pc.c \
    src/algo/pipeline.c \
    src/algo/stft.c \
    src/algo/wiener.c \
    src/algo/vad_decision.c \
    src/algo/transpose.c \
    src/hal/fft_hwa.c \
    src/hal/cycle_counter.c \
    src/util/wav_io.c \
    src/util/window_table.c \
    src/util/twiddle_table.c

SIM_BIN  := tms320_sim

# ---- unit-test binaries --------------------------------------
TEST_BINS := \
    $(BUILD)/test_fft \
    $(BUILD)/test_wav \
    $(BUILD)/test_stft \
    $(BUILD)/test_wiener \
    $(BUILD)/test_vad \
    $(BUILD)/test_transpose

GEN_BIN  := $(BUILD)/gen_test_signal

.PHONY: all tests e2e clean

all: $(SIM_BIN)

# ---- main simulator binary -----------------------------------
$(SIM_BIN): $(SIM_SRCS) | $(BUILD)
	$(CC) $(CFLAGS) $(SIM_SRCS) -o $@ $(LDLIBS)

# ---- build dir guard -----------------------------------------
$(BUILD):
	mkdir -p $(BUILD)

# ---- individual test binaries --------------------------------
$(BUILD)/test_fft: test/test_fft.c src/hal/fft_hwa.c src/hal/cycle_counter.c src/util/twiddle_table.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/test_wav: test/test_wav.c src/util/wav_io.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/test_stft: test/test_stft.c src/algo/stft.c src/util/window_table.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/test_wiener: test/test_wiener.c src/algo/wiener.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/test_vad: test/test_vad.c src/algo/vad_decision.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/test_transpose: test/test_transpose.c src/algo/transpose.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(GEN_BIN): test/gen_test_signal.c | $(BUILD)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

# ---- run tests -----------------------------------------------
tests: $(TEST_BINS)
	@set -e; \
	pass=0; fail=0; \
	for t in $(TEST_BINS); do \
	    echo "==== running $$t ===="; \
	    if $$t; then \
	        echo "---- $$t: PASS"; \
	        pass=$$((pass+1)); \
	    else \
	        echo "---- $$t: FAIL"; \
	        fail=$$((fail+1)); \
	    fi; \
	done; \
	echo ""; \
	echo "==== test summary: $$pass passed, $$fail failed ===="; \
	if [ $$fail -ne 0 ]; then exit 1; fi

# ---- end-to-end ----------------------------------------------
e2e: $(SIM_BIN) $(GEN_BIN)
	$(GEN_BIN) $(BUILD)/synth.wav
	./$(SIM_BIN) $(BUILD)/synth.wav $(BUILD)/out.wav
	python3 test/compare.py $(BUILD)/synth.wav $(BUILD)/out.wav $(BUILD)/compare.png

clean:
	rm -rf $(BUILD) $(SIM_BIN)
