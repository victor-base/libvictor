# -- MAKEFILE EXCLUSIVO DE WINDOWS --

# Nombre del ejecutable final
EXEC = mi_programa

# Archivos fuente
SRCS = test.c index.c index_flat.c math.c mem.c method.c vector.c iflat_utils.c index_flat_mp.c

# Archivos objeto correspondientes
OBJS = $(SRCS:.c=.o)

# Compilador y opciones
CC = gcc
CFLAGS = -Wall -g

# Regla para compilar el ejecutable
$(EXEC): $(OBJS)
	$(CC) $(OBJS) -o $(EXEC)  # Enlazar los archivos objeto para crear el ejecutable

# Regla para compilar cada archivo fuente .c a archivo objeto .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@  # Compilar cada archivo fuente a objeto

# Regla para limpiar archivos generados
.PHONY: clean
clean:
	rm -f $(OBJS) $(EXEC)  # Eliminar los archivos objeto y el ejecutable
