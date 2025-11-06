CC = cc
CFLAGS = -std=gnu17 -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wundef -g -O0

BINDIR = bin
SRCDIR = src

TARGETS = $(BINDIR)/mysh $(BINDIR)/proc-clone $(BINDIR)/cpu-calc-crc $(BINDIR)/ema-join-nl

all: $(TARGETS)

$(BINDIR):
	mkdir -p $(BINDIR)

$(BINDIR)/mysh: $(SRCDIR)/mysh.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $@

$(BINDIR)/proc-clone: $(SRCDIR)/proc_clone.c | $(BINDIR)
	$(CC) $(CFLAGS) -D_GNU_SOURCE $< -o $@

$(BINDIR)/cpu-calc-crc: $(SRCDIR)/cpu_calc_crc.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $@

$(BINDIR)/ema-join-nl: $(SRCDIR)/ema_join_nl.c | $(BINDIR)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(BINDIR)

.PHONY: all clean
