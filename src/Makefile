# =======================
#  libvictor Makefile
# =======================

# Detect OS and Architecture
OS   := $(shell uname -s 2>/dev/null || echo Windows_NT)
ARCH := $(shell uname -m)

# Version info
VERSION   = 1.0.17
SOVERSION = 1

# ----- Install prefix / dirs (multiarch-friendly) -----
PREFIX ?= /usr/local
INCLUDEDIR_REL := include
# On Debian/Ubuntu use lib/<multiarch>; otherwise just lib
DEB_HOST_MULTIARCH := $(shell dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null)
LIBDIR_REL  := $(if $(DEB_HOST_MULTIARCH),lib/$(DEB_HOST_MULTIARCH),lib)
PKGCONFIGDIR_REL := $(LIBDIR_REL)/pkgconfig

# ----- Targets per OS -----
ifeq ($(OS), Linux)
    LIBNAME_SHARED = libvictor.so.$(VERSION)
    LIBNAME_STATIC = libvictor.a
    LIBNAME_LINK   = libvictor.so
    CC = gcc
    CFLAGS ?= -g -Wall -Wno-misleading-indentation -Wextra -O3 -march=native -fPIC
    SHARED_FLAGS = -shared -Wl,-soname,libvictor.so.$(SOVERSION)
    INSTALL_LIB = \
      install -d "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)"; \
      install -m 0755 "$(LIBNAME_SHARED)" "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_SHARED)"; \
      install -m 0644 "$(LIBNAME_STATIC)" "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_STATIC)"; \
      ln -sf "$(LIBNAME_SHARED)" "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_LINK)"; \
      ln -sf "$(LIBNAME_SHARED)" "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/libvictor.so.$(SOVERSION)"; \
      if [ -z "$(DESTDIR)" ]; then ldconfig; fi
else ifeq ($(OS), Darwin)
    LIBNAME_SHARED = libvictor.$(VERSION).dylib
    LIBNAME_STATIC = libvictor.a
    LIBNAME_LINK   = libvictor.dylib
    CC = gcc
    ifeq ($(ARCH), arm64)
        CFLAGS ?= -g -std=c11 -Wall -Wextra -O3 -mcpu=apple-m1 -fPIC
    else
        CFLAGS ?= -g -std=c11 -Wall -Wextra -O3 -march=native -fPIC
    endif
    SHARED_FLAGS = -dynamiclib -install_name $(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_SHARED) \
                   -current_version $(VERSION) -compatibility_version $(SOVERSION)
    INSTALL_LIB = \
      install -d "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)"; \
      install -m 0755 "$(LIBNAME_SHARED)" "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_SHARED)"; \
      install -m 0644 "$(LIBNAME_STATIC)" "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_STATIC)"; \
      ln -sf "$(LIBNAME_SHARED)" "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_LINK)"; \
      ln -sf "$(LIBNAME_SHARED)" "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/libvictor.$(SOVERSION).dylib"
endif

# Sources and objects
SRCS = index.c index_flat.c vmath.c mem.c method.c vector.c iflat_utils.c heap.c \
       vtime.c map.c store.c file.c asort.c graph.c index_hnsw.c error.c kmeans.c kvtable.c version.c
OBJS = $(SRCS:.c=.o)

LDFLAGS ?= -lm -lpthread

# =======================
#  Version header
# =======================
version.h:
	@echo "Generating version.h..."
	@echo "#ifndef _VERSION_H" > version.h
	@echo "#define _VERSION_H" >> version.h
	@echo "" >> version.h
	@echo "#define __LIB_VERSION_MAJOR \"$(word 1,$(subst ., ,$(VERSION)))\"" >> version.h
	@echo "#define __LIB_VERSION_MINOR \"$(word 2,$(subst ., ,$(VERSION)))\"" >> version.h
	@echo "#define __LIB_VERSION_PATCH \"$(word 3,$(subst ., ,$(VERSION)))\"" >> version.h
	@echo "#define __LIB_VERSION_STRING \"$(VERSION)\"" >> version.h
	@echo "" >> version.h
	@echo "/* Build information */" >> version.h
	@echo "#define __BUILD_DATE \"$$(date '+%Y-%m-%d %H:%M:%S')\"" >> version.h
	@echo "#define __BUILD_HOST \"$$(hostname)\"" >> version.h
	@echo "#define __BUILD_USER \"$$(whoami)\"" >> version.h
	@echo "#define __BUILD_OS \"$(OS)\"" >> version.h
	@echo "#define __BUILD_ARCH \"$(ARCH)\"" >> version.h
	@echo "" >> version.h
	@echo "#endif" >> version.h

# =======================
#  Default target
# =======================
all: version.h $(LIBNAME_SHARED) $(LIBNAME_STATIC)

# Object files
%.o: %.c version.h
	$(CC) $(CFLAGS) -c $< -o $@

# Shared Library
$(LIBNAME_SHARED): $(OBJS)
	$(CC) $(SHARED_FLAGS) -o $(LIBNAME_SHARED) $(OBJS) $(LDFLAGS)

# Static Library
$(LIBNAME_STATIC): $(OBJS)
	ar rcs $(LIBNAME_STATIC) $(OBJS)
	ranlib $(LIBNAME_STATIC)

# =======================
#  pkg-config file
# =======================
libvictor.pc: packaging/libvictor.pc.in
	sed \
	  -e 's|@PREFIX@|$(PREFIX)|g' \
	  -e 's|@LIBDIR_REL@|$(LIBDIR_REL)|g' \
	  -e 's|@INCLUDEDIR_REL@|$(INCLUDEDIR_REL)|g' \
	  -e 's|@VERSION@|$(VERSION)|g' \
	  $< > $@

# =======================
#  Install / Uninstall
# =======================
install: $(LIBNAME_SHARED) $(LIBNAME_STATIC) libvictor.pc
	install -d "$(DESTDIR)$(PREFIX)/$(INCLUDEDIR_REL)/victor"
	install -m 0644 victor.h victor.hpp victorkv.h "$(DESTDIR)$(PREFIX)/$(INCLUDEDIR_REL)/victor/"
	# pkg-config
	install -d "$(DESTDIR)$(PREFIX)/$(PKGCONFIGDIR_REL)"
	install -m 0644 libvictor.pc "$(DESTDIR)$(PREFIX)/$(PKGCONFIGDIR_REL)/libvictor.pc"
	# libs + symlinks
	$(INSTALL_LIB)
	@echo "Installed into $(DESTDIR)$(PREFIX)."
	@echo "Tip: check with -> pkg-config --modversion libvictor"

uninstall:
	@echo "Uninstalling from $(DESTDIR)$(PREFIX)..."
	-rm -f "$(DESTDIR)$(PREFIX)/$(PKGCONFIGDIR_REL)/libvictor.pc"
	-rm -f "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_LINK)"
	-rm -f "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/libvictor.so.$(SOVERSION)" 2>/dev/null || true
	-rm -f "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_SHARED)" 2>/dev/null || true
	-rm -f "$(DESTDIR)$(PREFIX)/$(LIBDIR_REL)/$(LIBNAME_STATIC)" 2>/dev/null || true
	-rm -rf "$(DESTDIR)$(PREFIX)/$(INCLUDEDIR_REL)/victor"

# =======================
#  Clean / Test / Debug
# =======================
clean:
	rm -f $(OBJS) $(LIBNAME_SHARED) $(LIBNAME_STATIC) $(LIBNAME_LINK)
	rm -f *.dylib *.so *.so.* *.a
	rm -f version.h libvictor.pc

# Test install using pkg-config
test-install:
	$(CC) $(CFLAGS) test.c $(shell pkg-config --cflags --libs libvictor) -o test_installed
	./test_installed
	rm -f test_installed

debug:
	@echo "OS: $(OS)"
	@echo "ARCH: $(ARCH)"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "SHARED_FLAGS: $(SHARED_FLAGS)"
	@echo "LIBNAME_SHARED: $(LIBNAME_SHARED)"
	@echo "LIBNAME_STATIC: $(LIBNAME_STATIC)"
	@echo "PREFIX: $(PREFIX)"
	@echo "LIBDIR_REL: $(LIBDIR_REL)"
	@echo "PKGCONFIGDIR_REL: $(PKGCONFIGDIR_REL)"

version: version.h
	@echo "libvictor version $(VERSION)"
	@echo "Built for $(OS)/$(ARCH)"

.PHONY: all optional clean install uninstall test-install debug version

