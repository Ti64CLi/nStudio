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

SRCS = nstudio.c gfx.c editor.c settings.c gapbuf.c syntax.c asmdb.c browser.c optab.c util.c asmdiag.c
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

clean:
	rm -f $(OBJS) $(DEPS) $(EXE).tns $(EXE).elf $(EXE).tns.zehn

.PHONY: all clean
