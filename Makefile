DEBUG = FALSE

GCC = nspire-gcc
LD  = nspire-ld
GENZEHN = genzehn

GCCFLAGS = -Wall -W -marm -MMD -MP
LDFLAGS =
ZEHNFLAGS = --name "nstudio" --uses-lcd-blit true --240x320-support true

ifeq ($(DEBUG),FALSE)
	GCCFLAGS += -Os
else
	GCCFLAGS += -O0 -g
endif

SRCS = nstudio.c gfx.c editor.c editor_ui.c settings.c gapbuf.c syntax.c asmdb.c browser.c optab.c util.c asmdiag.c fileio.c textdiff.c undo.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)
EXE = nstudio

all: $(EXE).tns

%.o: %.c
	$(GCC) $(GCCFLAGS) -c $< -o $@

$(EXE).elf: $(OBJS)
	$(LD) $^ -o $@ $(LDFLAGS)

$(EXE).tns: $(EXE).elf
	$(GENZEHN) --input $^ --output $@.zehn $(ZEHNFLAGS)
	make-prg $@.zehn $@
	rm $@.zehn

-include $(DEPS)

# ----------------------------------------------------------------------
# Host-side unit tests.  These run on the development machine with the
# system compiler (NOT nspire-gcc) and cover only the pure modules that
# have no editor / graphics / Ndless dependency.  Compiled in a single
# invocation to a standalone binary, so no host .o files ever collide
# with the ARM objects above.
# ----------------------------------------------------------------------
HOSTCC ?= cc
HOSTCFLAGS = -Wall -W -O0 -g -I.
TEST_BIN = tests/run_tests
TEST_MODULES = util.c optab.c gapbuf.c asmdiag.c asmdb.c fileio.c textdiff.c undo.c

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/run_tests.c $(TEST_MODULES)
	$(HOSTCC) $(HOSTCFLAGS) -o $@ $^

clean:
	rm -f $(OBJS) $(DEPS) $(EXE).tns $(EXE).elf $(EXE).tns.zehn $(TEST_BIN)

.PHONY: all clean test
