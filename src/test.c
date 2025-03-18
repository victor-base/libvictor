#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "victor.h"

#define DIMS 512  // Número de dimensiones del vector de prueba
#define TOP_N 5   // Número de mejores coincidencias a buscar
#define NUM_VECTORS 10000 // Cantidad de vectores a insertar


// Función auxiliar para generar vectores aleatorios en el rango [-1,1]
void generate_random_vector(float32_t *vector, uint16_t dims) {
    for (int i = 0; i < dims; i++) {
        vector[i] = ((float) rand() / RAND_MAX) * 2.0f - 1.0f;
    }
}

int main() {
    srand(time(NULL)); // Inicializar la semilla de números aleatorios

    // Parámetros del índice
    int index_type = FLAT_INDEX;
    int method = COSINE; // Método de prueba
    uint16_t dims = DIMS;
    MatchResult result;
    float32_t vector[DIMS];
    
    
    // Crear el índice
    Index *index = alloc_index(index_type, method, dims);
    if (!index) {
        printf("Error: No se pudo asignar el índice.\n");
        return 1;
    }
    
    printf("Índice creado correctamente.\n");

    for (int i = 0; i < NUM_VECTORS; i++) {
        
        generate_random_vector(vector, dims);
        
        if (index->insert(index->data, i, vector, dims) != 0) {
            printf("Error: No se pudo insertar el vector %d.\n", i);
            return 1;
        }
    }
    
    printf("Se han insertado %d vectores correctamente.\n", NUM_VECTORS);

    if (index->search(index->data, vector, dims, &result) != 0) {
        printf("Error en la búsqueda.\n");
        return 1;
    }

    // Mostrar resultados
    printf("Búsqueda completada.\n");
    printf("Vector más cercano encontrado: ID = %d, Distancia = %f\n", result.id, result.distance);
    destroy_index(&index);

    printf("Prueba finalizada correctamente.\n");
    return 0;
}