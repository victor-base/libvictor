# This section is responsible for detecting the operating system.
# It can be used to set OS-specific variables or perform OS-specific tasks.

OS := $(shell uname -s 2>/dev/null || echo Windows_NT)

# Definir opciones según el SO

ifeq ($(OS), Linux)
    TARGET = testdb
    LIBNAME = libvictor.so
    CC = gcc
    CFLAGS = -g -std=c11 -Wall -Wextra -O3 -march=native -fPIC
    LDFLAGS = -lm -lpthread

else ifeq ($(OS), Windows_NT)
    TARGET = testdb.exe
    LIBNAME = libvictor.dll
    CC = x86_64-w64-mingw32-gcc   # change if youre using any other compiler, Im using this one.
    CFLAGS = -g -std=c11 -Wall -Wextra -O3 -march=native
    LDFLAGS = -lm -pthread

else ifeq ($(OS), Darwin)
	TARGET = testdb.macos
	LIBNAME = libvictor.dylib
	CC = gcc  
	CFLAGS = -g -std=c11 -Wall -Wextra -O3 -march=native -fPIC
	LDFLAGS = -lm -lpthread

endif

# Source files & .obj instructiosm

SRCS = test.c index.c index_flat.c math.c mem.c method.c vector.c iflat_utils.c index_flat_mp.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET) $(LIBNAME)

$(TARGET): $(OBJS) # Me genera el ejecutable (testdb.exe en Win) usando esta regla:
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c # le digo al compilador como generar sus .obj a partir de los fuente
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBNAME): $(OBJS) # Creará la biblioteca compartida (libvictor.dll en Windows) usando la regla:
	$(CC) -shared -o $(LIBNAME) $(OBJS) $(LDFLAGS)

# Borro los archivos generados como los .obj y todos los generados en las variables
clean:
	rm -f $(OBJS) $(TARGET) $(LIBNAME) 

.PHONY: all clean
