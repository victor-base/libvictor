# Detect OS and Architecture
OS := $(shell uname -s 2>/dev/null || echo Windows_NT)
ARCH := $(shell uname -m)

# Version info
VERSION = 1.0.0
SOVERSION = 1

# Configs based on OS
ifeq ($(OS), Linux)
    TARGET = testdb
    DUTIL  = dutil
    MUTATE = mutate
    LIBNAME_SHARED = libvictor.so.$(VERSION)
    LIBNAME_STATIC = libvictor.a
    LIBNAME_LINK = libvictor.so
    CC = gcc
    CFLAGS = -g -Wall -Wextra -O3 -march=native -fPIC
    SHARED_FLAGS = -shared -Wl,-soname,libvictor.so.$(SOVERSION)
    INSTALL_LIB = cp $(LIBNAME_SHARED) $(LIBNAME_STATIC) /usr/local/lib/ && \
                  ln -sf /usr/local/lib/$(LIBNAME_SHARED) /usr/local/lib/$(LIBNAME_LINK) && \
                  ln -sf /usr/local/lib/$(LIBNAME_SHARED) /usr/local/lib/libvictor.so.$(SOVERSION) && \
                  ldconfig
else ifeq ($(OS), Darwin)
    TARGET = testdb.macos
    DUTIL  = dutil.macos
    MUTATE = mutate.macos
    LIBNAME_SHARED = libvictor.$(VERSION).dylib
    LIBNAME_STATIC = libvictor.a
    LIBNAME_LINK = libvictor.dylib
    CC = gcc
    
    # Handle different architectures
    ifeq ($(ARCH), arm64)
        CFLAGS = -g -std=c11 -Wall -Wextra -O3 -mcpu=apple-m1 -fPIC
    else
        CFLAGS = -g -std=c11 -Wall -Wextra -O3 -march=native -fPIC
    endif
    
    SHARED_FLAGS = -dynamiclib -install_name /usr/local/lib/$(LIBNAME_SHARED) \
                   -current_version $(VERSION) -compatibility_version $(SOVERSION)
    INSTALL_LIB = cp $(LIBNAME_SHARED) $(LIBNAME_STATIC) /usr/local/lib/ && \
                  ln -sf /usr/local/lib/$(LIBNAME_SHARED) /usr/local/lib/$(LIBNAME_LINK) && \
                  ln -sf /usr/local/lib/$(LIBNAME_SHARED) /usr/local/lib/libvictor.$(SOVERSION).dylib
endif

# Sources and objects
SRCS = index.c index_flat.c vmath.c mem.c method.c vector.c iflat_utils.c heap.c \
       vtime.c map.c store.c file.c asort.c graph.c index_hnsw.c error.c kmeans.c kvtable.c version.c
OBJS = $(SRCS:.c=.o)

LDFLAGS = -lm -lpthread

# Generate version header
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

# Default target
all: version.h $(TARGET) $(LIBNAME_SHARED) $(LIBNAME_STATIC)

# Optional targets
optional: $(DUTIL) $(MUTATE)

# Executable
$(TARGET): $(OBJS) test.c
	$(CC) $(CFLAGS) $(OBJS) test.c -o $(TARGET) $(LDFLAGS)

# Dutil executable
$(DUTIL): $(OBJS) dutil.c
	$(CC) $(CFLAGS) $(OBJS) dutil.c -o $(DUTIL) $(LDFLAGS)

# Mutate executable
$(MUTATE): $(OBJS) mutate.c
	$(CC) $(CFLAGS) $(OBJS) mutate.c -o $(MUTATE) $(LDFLAGS)

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

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET) $(LIBNAME_SHARED) $(LIBNAME_STATIC) $(LIBNAME_LINK) $(DUTIL) $(MUTATE)
	rm -f *.dylib *.so *.so.* *.a
	rm -f testdb testdb.macos dutil.macos mutate.macos
	rm -f version.h

# Install to system directories
install: $(LIBNAME_SHARED) $(LIBNAME_STATIC)
	mkdir -p /usr/local/include/victor
	cp victor.h victor.hpp victorkv.h /usr/local/include/victor/
	$(INSTALL_LIB)

# Uninstall
uninstall:
	rm -rf /usr/local/include/victor
	rm -f /usr/local/lib/libvictor.*

# Test installation
test-install: install
	$(CC) $(CFLAGS) -I/usr/local/include/victor -L/usr/local/lib -lvictor test.c -o test_installed $(LDFLAGS)
	./test_installed
	rm -f test_installed

# Debug info
debug:
	@echo "OS: $(OS)"
	@echo "ARCH: $(ARCH)"
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "SHARED_FLAGS: $(SHARED_FLAGS)"
	@echo "TARGET: $(TARGET)"
	@echo "LIBNAME_SHARED: $(LIBNAME_SHARED)"
	@echo "LIBNAME_STATIC: $(LIBNAME_STATIC)"

# Show version
version: version.h
	@echo "libvictor version $(VERSION)"
	@echo "Built for $(OS)/$(ARCH)"

.PHONY: all optional clean install uninstall test-install debug version
