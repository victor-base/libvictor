# Detect OS
OS := $(shell uname -s 2>/dev/null || echo Windows_NT)

# Configs based on OS
ifeq ($(OS), Linux)
    TARGET = testdb
    DUTIL  = dutil
    LIBNAME_SHARED = libvictor.so
    LIBNAME_STATIC = libvictor.a
    CC = gcc
    CFLAGS = -g -Wall -Wextra -O3 -march=native -fPIC
    INSTALL_LIB = cp libvictor.so libvictor.a /usr/local/lib/
else ifeq ($(OS), Darwin)
    TARGET = testdb.macos
    DUTIL  = dutil
	MUTATE = mutate
    LIBNAME_SHARED = libvictor.dylib
    LIBNAME_STATIC = libvictor.a
    CC = gcc
    CFLAGS = -g -std=c11 -Wall -Wextra -O3 -ffast-math -march=native -fPIC
    INSTALL_LIB = cp libvictor.dylib libvictor.a /usr/local/lib/
endif

# Sources and objects
SRCS = index.c index_flat.c math.c mem.c method.c vector.c iflat_utils.c heap.c time.c map.c index_nsw.c store.c file.c asort.c graph.c index_hnsw.c
OBJS = $(SRCS:.c=.o)

LDFLAGS = -lm -lpthread

# Default target
all: $(TARGET) $(LIBNAME_SHARED) $(LIBNAME_STATIC) #$(DUTIL) $(MUTATE)

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
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Shared Library
$(LIBNAME_SHARED): $(OBJS)
	$(CC) -shared -o $(LIBNAME_SHARED) $(OBJS) $(LDFLAGS)

# Static Library
$(LIBNAME_STATIC): $(OBJS)
	ar rcs $(LIBNAME_STATIC) $(OBJS)

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET) $(LIBNAME_SHARED) $(LIBNAME_STATIC) $(DUTIL)

# Install to system directories
install:
	mkdir -p /usr/local/include/victor
	cp victor.h victor.hpp /usr/local/include/victor/
	$(INSTALL_LIB)

.PHONY: all clean install
