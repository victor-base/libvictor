#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "victor.h"
#include "config.h"

#define DIMS 512  // Número de dimensiones del vector de prueba
#define TOP_N 5   // Número de mejores coincidencias a buscar
#define NUM_VECTORS 2000000

// Cantidad de vectores a insertar


// Función auxiliar para generar vectores aleatorios en el rango [-1,1]
void generate_random_vector(float32_t *vector, uint16_t dims) {
    for (int i = 0; i < dims; i++) {
        vector[i] = ((float) rand() / RAND_MAX) * 2.0f - 1.0f;
    }
}

int main() {
    srand(time(NULL)); // Inicializar la semilla de números aleatorios
    struct timespec start, end;
    // Parámetros del índice
    int index_type = FLAT_INDEX;
    int method = COSINE; // Método de prueba
    uint16_t dims = DIMS;
    MatchResult *result;
    float32_t vector[DIMS];
    
    printf("%s\n", __LIB_VERSION());
    
    // Crear el índice
    Index *index = alloc_index(index_type, method, dims, NULL);
    if (!index) {
        printf("Error: No se pudo asignar el índice.\n");
        return 1;
    }
    
    printf("Índice creado correctamente.\n");

    for (int i = 0; i < NUM_VECTORS; i++) {
        
        generate_random_vector(vector, dims);
        
        if (insert(index, i, vector, dims) != 0) {
            printf("Error: No se pudo insertar el vector %d.\n", i);
            return 1;
        }
    }
    
    printf("Se han insertado %d vectores correctamente.\n", NUM_VECTORS);
	fflush(stdout);
	clock_gettime(CLOCK_MONOTONIC, &start);
	result = calloc(10, sizeof(MatchResult));
	int ret;
    if ((ret = search(index, vector, dims, result)) != 0) {
        printf("Error en la búsqueda. %d\n", ret);
        return 1;
    }
	clock_gettime(CLOCK_MONOTONIC, &end);
// Calcular diferencia en milisegundos
	double elapsed_ms = (end.tv_sec - start.tv_sec) * 1000.0;
	elapsed_ms += (end.tv_nsec - start.tv_nsec) / 1.0e6;

	printf("Búsqueda completada en %.3f ms\n", elapsed_ms);
	fflush(stdout);
    // Mostrar resultados
    printf("Búsqueda completada.\n");

	printf("Vector más cercano encontrado: ID = %d, Distancia = %f\n", result[0].id, result[0].distance);
	printf("Vector más cercano encontrado: ID = %d, Distancia = %f\n", result[1].id, result[1].distance);
	printf("Vector más cercano encontrado: ID = %d, Distancia = %f\n", result[2].id, result[2].distance);
    destroy_index(&index);

    printf("Prueba finalizada correctamente.\n");
	getchar();
    return 0;
}