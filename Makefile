# c68k --- host build of the cross-compiler.
#
# P0 baseline: the imported chibicc front end and (temporary) x86-64 back end
# are UNMODIFIED, so this builds, tests, and self-hosts exactly like upstream
# chibicc -- only relocated into the c68k repository layout:
#
#   src/       compiler sources (chibicc front end + back end)
#   include/   chibicc's freestanding builtin headers (found via <argv0dir>/include)
#   tests/     the chibicc conformance suite + golden donors
#
# Requires a POSIX host with GCC or Clang plus GNU binutils (as/ld), i.e. a
# Linux/macOS x86-64 box. This is validated in CI on ubuntu-latest; it does not
# build with MSVC and does not run on Windows natively (see docs). The real
# 68000 code generator and OS backends arrive in later phases (P2+).

CFLAGS=-std=c11 -g -fno-common -Wall -Wno-switch
INCDIR=include
TESTDIR=tests

SRCS=$(wildcard src/*.c)
OBJS=$(SRCS:.c=.o)

TEST_SRCS=$(wildcard $(TESTDIR)/*.c)
TESTS=$(TEST_SRCS:.c=.exe)

# ---- Stage 1: build c68k with the host compiler ----

c68k: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJS): src/chibicc.h

# ---- Tests (compiled by stage-1 c68k, run natively) ----

$(TESTDIR)/%.exe: c68k $(TESTDIR)/%.c
	./c68k -I$(INCDIR) -I$(TESTDIR) -c -o $(TESTDIR)/$*.o $(TESTDIR)/$*.c
	$(CC) -pthread -o $@ $(TESTDIR)/$*.o -xc $(TESTDIR)/common

test: $(TESTS)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	$(TESTDIR)/driver.sh ./c68k

test-all: test test-stage2 selfhost

# ---- Stage 2: c68k compiles its own source ----

stage2/c68k: $(OBJS:src/%=stage2/src/%)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

stage2/src/%.o: c68k src/%.c
	mkdir -p stage2/src
	./c68k -I$(INCDIR) -c -o $@ src/$*.c

stage2/$(TESTDIR)/%.exe: stage2/c68k $(TESTDIR)/%.c
	mkdir -p stage2/$(TESTDIR)
	./stage2/c68k -I$(INCDIR) -I$(TESTDIR) -c -o stage2/$(TESTDIR)/$*.o $(TESTDIR)/$*.c
	$(CC) -pthread -o $@ stage2/$(TESTDIR)/$*.o -xc $(TESTDIR)/common

test-stage2: $(TESTS:$(TESTDIR)/%=stage2/$(TESTDIR)/%)
	for i in $^; do echo $$i; ./$$i || exit 1; echo; done
	$(TESTDIR)/driver.sh ./stage2/c68k

# ---- Stage 3 + byte-identical self-host check ----

stage3/c68k: $(OBJS:src/%=stage3/src/%)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

stage3/src/%.o: stage2/c68k src/%.c
	mkdir -p stage3/src
	./stage2/c68k -I$(INCDIR) -c -o $@ src/$*.c

# The self-host baseline: stage2 (built by stage1) and stage3 (built by stage2)
# must be byte-identical, object for object.
selfhost: stage2/c68k stage3/c68k
	@ok=1; for f in $(OBJS:src/%=%); do \
	  if cmp -s stage2/src/$$f stage3/src/$$f; then \
	    echo "identical: $$f"; \
	  else \
	    echo "DIFFERS:   $$f"; ok=0; \
	  fi; \
	done; \
	if [ $$ok -eq 1 ]; then \
	  echo "self-host OK: stage2 == stage3"; \
	else \
	  echo "self-host FAILED: stage2 != stage3"; exit 1; \
	fi

# ---- Misc ----

# Front-end-only smoke check: needs no assembler/linker, so it runs on any host.
# Used on macOS/Windows, where chibicc's interim x86-64 back end can't assemble
# or link (real execution testing happens on Linux and, from P2, under sim68k).
smoke: c68k
	@printf 'int main(){return 41+1;}\n' | ./c68k -S -o- -xc - | grep -q 'main:'
	@printf 'int x=1+2;\n' | ./c68k -E -xc - | grep -q 'int x'
	@./c68k --help 2>&1 | grep -q chibicc
	@echo "front-end smoke OK"

clean:
	rm -rf c68k stage2 stage3 $(TESTS) $(TESTDIR)/*.s $(TESTDIR)/*.exe
	find . -type f '(' -name '*~' -o -name '*.o' ')' -exec rm -f {} ';'

.PHONY: test test-all test-stage2 selfhost smoke clean
