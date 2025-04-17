#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "victor.h"
#include "config.h"
#include "panic.h"

#define DIMS 512  // Número de dimensiones del vector de prueba
#define TOP_N 5   // Número de mejores coincidencias a buscar
#define NUM_VECTORS 50000

// Cantidad de vectores a insertar

#include <stdio.h>

void print_index_stats(const IndexStats *stats) {
    if (!stats) {
        printf("index_stats is null\n");
        return;
    }

    const char *labels[] = { "insert", "delete", "search", "search_n" };
    const TimeStat *all_stats[] = {
        &stats->insert,
        &stats->delete,
        &stats->search,
        &stats->search_n
    };



    for (int i = 0; i < 4; i++) {
        const TimeStat *s = all_stats[i];
        double avg = (s->count > 0) ? s->total / s->count : 0.0;

        printf("%-9s count = %-8lu total = %9.3f ms    avg = %7.3f ms    min = %7.3f ms    max = %7.3f ms\n",
               labels[i],
               s->count,
               s->total,
               avg,
               s->min,
               s->max);
    }
}



// Función auxiliar para generar vectores aleatorios en el rango [-1,1]
void generate_random_vector(float32_t *vector, uint16_t dims) {
    for (int i = 0; i < dims; i++) {
        vector[i] = ((float) rand() / RAND_MAX) * 2.0f - 1.0f;
    }
}

int main() {
	NSWContext context;
    srand(time(NULL)); // Inicializar la semilla de números aleatorios
    int index_type = FLAT_INDEX, ret;
    int method = COSINE; // Método de prueba
    uint16_t dims = DIMS;
    MatchResult *result;
	MatchResult r;
    float32_t vector[DIMS];
    
	context.ef_construct = 64;
	context.ef_search = 100;
	context.odegree = 32;
    printf("%s\n", __LIB_VERSION());
    
    // Crear el índice
    Index *index = alloc_index(index_type, method, dims, &context);
    if (!index) {
        printf("Error: No se pudo asignar el índice.\n");
        return 1;
    }

    printf("Índice creado correctamente.\n");

    for (int i = 1; i < NUM_VECTORS; i++) {
        
        generate_random_vector(vector, dims);
        
        if (insert(index, i, vector, dims) != 0) {
            printf("Error: No se pudo insertar el vector %d.\n", i);
            return 1;
        }

    }
    
	for ( int i = 0; i < 50; i ++) {
		result = calloc(10, sizeof(MatchResult));
		if ((ret = search_n(index, vector, dims, result, 10)) != 0) {
			printf("Error en la búsqueda. %d\n", ret);
			return 1;
		}
		printf("Result: %d\n", result[0].id);
		free(result);
	}

	for ( int i = 0; i < 50; i ++) {
		
		if ((ret = search(index, vector, dims, &r)) != 0) {
			printf("Error en la búsqueda. %d\n", ret);
			return 1;
		}
		printf("Result: %d\n", r.id);
		
	}

	for ( int i= 0; i < 100; i ++) {
		delete(index, i+200);
	}

	IndexStats st;
	stats(index, &st);
	print_index_stats(&st);
    destroy_index(&index);

    printf("Prueba finalizada correctamente.\n");
	getchar();
    return 0;
}