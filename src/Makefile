# This section is responsible for detecting the operating system.
# It can be used to set OS-specific variables or perform OS-specific tasks.

OS := $(shell uname -s 2>/dev/null || echo Windows_NT)

# Definir opciones según el SO

ifeq ($(OS), Linux)
    TARGET = testdb
	DUTIL  = dutil
    LIBNAME = libvictor.so
    CC = gcc
    CFLAGS = -g -Wall -Wextra -O3 -march=native -fPIC
else ifeq ($(OS), Darwin)
	TARGET = testdb.macos
	DUTIL  = dutil
	LIBNAME = libvictor.dylib
	CC = gcc  
	CFLAGS = -g -std=c11 -Wall -Wextra -O3 -march=native -fPIC
endif

# Source files & .obj instructiosm

SRCS = index.c index_flat.c math.c mem.c method.c vector.c iflat_utils.c heap.c time.c map.c index_nsw.c store.c file.c asort.c
OBJS = $(SRCS:.c=.o)

LDFLAGS = -lm -lpthread

all: $(TARGET) $(LIBNAME) $(DUTIL)

$(DUTIL): $(OBJS)
	$(CC) $(FLAGS) $(OBJS) dutil.c -o $(DUTIL) $(LDFLAGS)


$(TARGET): $(OBJS) # Me genera el ejecutable (testdb.exe en Win) usando esta regla:
	$(CC) $(CFLAGS) $(OBJS) test.c  -o $(TARGET) $(LDFLAGS)

%.o: %.c # le digo al compilador como generar sus .obj a partir de los fuente
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBNAME): $(OBJS) # Creará la biblioteca compartida (libvictor.dll en Windows) usando la regla:
	$(CC) -shared -o $(LIBNAME) $(OBJS) $(LDFLAGS)

# Borro los archivos generados como los .obj y todos los generados en las variables
clean:
	rm -f $(OBJS) $(TARGET) $(LIBNAME) $(DUTIL)

install:
	cp libvictor.dylib /usr/local/lib/
	mkdir -p /usr/local/include/victor && cp victor.h /usr/local/include/victor/

.PHONY: all clean
