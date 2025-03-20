CC = gcc

TARGET = testdb

LIBNAME = libvictor.so

SRCS = test.c index.c index_flat.c math.c mem.c method.c vector.c iflat_utils.c index_flat_mp.c
OBJS = $(SRCS:.c=.o)

# Compiler flags (Optimized for ARMv8-A / AArch64)
CFLAGS = -g -Wall -Wextra -O3 -march=armv8-a+simd -fPIC

LDFLAGS = -lm -lpthread

all: $(TARGET) $(LIBNAME)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBNAME): index.o index_flat.o math.o mem.o method.o vector.o iflat_utils.o index_flat_mp.o
	$(CC) -shared -o $(LIBNAME) index.o index_flat.o math.o mem.o iflat_utils.o index_flat_mp.o method.o vector.o $(LDFLAGS)


clean:
	rm -f $(OBJS) $(TARGET) $(LIBNAME)

check-neon:
	@echo "Checking for NEON support..."
	@$(CC) -dM -E - < /dev/null | grep -q "__ARM_NEON" && echo "NEON supported" || echo "NEON not supported"

.PHONY: all clean check-neon


