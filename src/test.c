#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "victor.h"
#include "config.h"

#define DIMS 512  // Número de dimensiones del vector de prueba
#define TOP_N 5   // Número de mejores coincidencias a buscar
#define NUM_VECTORS 1000000

// Cantidad de vectores a insertar


// Función auxiliar para generar vectores aleatorios en el rango [-1,1]
void generate_random_vector(float32_t *vector, uint16_t dims) {
    for (int i = 0; i < dims; i++) {
        vector[i] = ((float) rand() / RAND_MAX) * 2.0f - 1.0f;
    }
}

int main() {
    srand(time(NULL)); // Inicializar la semilla de números aleatorios
    time_t start, end;
    // Parámetros del índice
    int index_type = FLAT_INDEX;
    int method = COSINE; // Método de prueba
    uint16_t dims = DIMS;
    MatchResult result;
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
    start = clock();
    if (search(index, vector, dims, &result) != 0) {
        printf("Error en la búsqueda.\n");
        return 1;
    }
    end = clock();
    printf("Tiempo de búsqueda: %f segundos\n", (double)(end - start) / CLOCKS_PER_SEC);
    // Mostrar resultados
    printf("Búsqueda completada.\n");
    printf("Vector más cercano encontrado: ID = %d, Distancia = %f\n", result.id, result.distance);
    destroy_index(&index);

    printf("Prueba finalizada correctamente.\n");
    return 0;
}