# Omega CMI headless: builds on the device itself (any CPU) or cross-compiles.
#   make                      native build (needs g++ or clang++ and libasound2-dev / alsa-lib headers)
#   make CXX=aarch64-linux-gnu-g++     ARM64 cross build (with the arm64 alsa-lib)
#   make install PREFIX=/usr/local
CXX ?= g++
CXXFLAGS ?= -O2 -pipe
PREFIX ?= /usr/local
override CXXFLAGS += -std=c++17 -Wall -Wextra -ffast-math -fno-finite-math-only
LDLIBS = -lasound -lpthread

cmi-headless: cmi_headless.cpp $(wildcard ../core/*.h)
	$(CXX) $(CXXFLAGS) -o $@ cmi_headless.cpp $(LDFLAGS) $(LDLIBS)

install: cmi-headless
	install -D -m 755 cmi-headless $(DESTDIR)$(PREFIX)/bin/cmi-headless

clean:
	rm -f cmi-headless

.PHONY: install clean
