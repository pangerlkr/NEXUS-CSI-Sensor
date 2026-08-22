# Host test suite for the NEXUS CSI Sensor
#
# Builds the platform-independent firmware modules with a native compiler and
# runs them. No ESP-IDF, no toolchain, no board. See README.md for what this does
# and does not prove.
#
#   make                 build and run everything
#   make SAN=1           same, with the address and UB sanitizers on
#   ./build/test_logger   run one suite after a build
#   make clean

CC      ?= cc
FW      := ../../firmware/main
BUILD   := build

CFLAGS  := -std=c11 -O1 -g \
           -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align \
           -Wstrict-prototypes -Wwrite-strings -Wno-unused-parameter \
           -Istubs -I$(FW)

# Opt-in sanitizers. Leak checking is off because the restore suite re-runs
# logger_init(), which reallocates the ring on purpose and has no deinit to call.
ifdef SAN
CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer
export ASAN_OPTIONS = detect_leaks=0
endif

# Each suite is its own binary: the stubs keep per-test state, so linking them
# together would let one suite's fault injection leak into another.
SUITES  := test_ringbuffer test_logger test_logger_restore test_json

test_ringbuffer_SRCS     := test_ringbuffer.c     $(FW)/ringbuffer.c
test_logger_SRCS         := test_logger.c         $(FW)/logger.c $(FW)/ringbuffer.c
# The restore suite never calls logger_to_json(), but logger.c references the
# builder, so link the real json.c rather than adding six more no-op fakes.
test_logger_restore_SRCS := test_logger_restore.c $(FW)/logger.c $(FW)/ringbuffer.c $(FW)/json.c
test_json_SRCS           := test_json.c           $(FW)/json.c

# Coarse but correct: any header change rebuilds every suite. Four small
# compiles, so the precision of real dependency tracking is not worth the
# machinery.
HDRS := $(wildcard $(FW)/*.h) $(wildcard stubs/*.h) $(wildcard stubs/freertos/*.h)

BINS := $(addprefix $(BUILD)/,$(SUITES))

.PHONY: all check clean
all: check

check: $(BINS)
	@echo
	@fail=0; for t in $(BINS); do \
	    echo "--- $$t"; \
	    ./$$t || fail=1; \
	    echo; \
	done; \
	if [ $$fail -ne 0 ]; then echo "SOME SUITES FAILED"; exit 1; fi; \
	echo "all suites passed"

$(BUILD):
	@mkdir -p $(BUILD)

.SECONDEXPANSION:
$(BUILD)/%: $$($$*_SRCS) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) $($*_SRCS) -o $@

clean:
	rm -rf $(BUILD)
