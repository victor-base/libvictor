#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "victor.h"
#include "config.h"
#include "panic.h"
#include "index_nsw.h"

#define DIMS 128
#define TOP_N 5
#define NUM_VECTORS 30000

extern void dump_nsw_graph_csv(FILE *fp, const INodeNSW *head);

void print_index_stats(const IndexStats *stats) {
    if (!stats) {
        printf("index_stats is null\n");
        return;
    }

    const char *labels[] = { "insert", "delete", "search", "search_n", "dump" };
    const TimeStat *all_stats[] = {
        &stats->insert,
        &stats->delete,
        &stats->search,
        &stats->search_n,
        &stats->dump,
    };

    for (int i = 0; i < 5; i++) {
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

void normalize_vector(float *vec, int dims) {
    float norm = 0.0f;
    for (int i = 0; i < dims; i++) norm += vec[i] * vec[i];
    norm = sqrtf(norm);
    if (norm > 0.0f) {
        for (int i = 0; i < dims; i++) vec[i] /= norm;
    }
}

int load_fvecs_file(const char *filename, float **vectors, int max_vectors, int dims) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("fvecs fopen");
        return -1;
    }

    int i = 0;
    while (i < max_vectors) {
        int dim;
        if (fread(&dim, sizeof(int), 1, f) != 1) break;

        if (dim != dims) {
            fprintf(stderr, "Dimensión inesperada: %d (esperado: %d)\n", dim, dims);
            fclose(f);
            return -1;
        }

        vectors[i] = malloc(sizeof(float) * dims);
        if (!vectors[i]) {
            perror("malloc");
            fclose(f);
            return -1;
        }

        if (fread(vectors[i], sizeof(float), dims, f) != dims) {
            fprintf(stderr, "Error leyendo vector %d\n", i);
            fclose(f);
            return -1;
        }

        normalize_vector(vectors[i], dims);
        i++;
    }

    fclose(f);
    return i;
}

void generate_random_vector(float *v, int dims) {
    float norm = 0.0f;
    for (int i = 0; i < dims; i++) {
        v[i] = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        norm += v[i] * v[i];
    }
    norm = sqrtf(norm);
    if (norm > 0.0f) {
        for (int i = 0; i < dims; i++) v[i] /= norm;
    }
}

int main() {
    srand(time(NULL));

    HNSWContext context = {
        .ef_construct = 60,
        .ef_search = 120,
        .M0 = 32
    };

    printf("Lib version: %s\n", __LIB_VERSION());

    Index *flat = alloc_index(FLAT_INDEX, L2NORM, DIMS, NULL);
    Index *hnsw = alloc_index(HNSW_INDEX, L2NORM, DIMS, &context);

    if (!flat || !hnsw) {
        printf("Error creando índices.\n");
        return 1;
    }

    float *dataset[NUM_VECTORS];
    int loaded = load_fvecs_file("dataset.fvecs", dataset, NUM_VECTORS, DIMS);
    if (loaded <= 0) {
        fprintf(stderr, "No se pudieron cargar vectores.\n");
        return 1;
    }

    for (int i = 0; i < loaded; i++) {
        if (insert(flat, i + 1, dataset[i], DIMS) != 0 ||
            insert(hnsw, i + 1, dataset[i], DIMS) != 0) {
            printf("Error insertando vector %d\n", i + 1);
            return 1;
        }
    }

    // Cargar queries reales
    float *queries[1000];  // puedes ajustar el límite
    int qtotal = load_fvecs_file("query.fvecs", queries, 1000, DIMS);
    if (qtotal <= 0) {
        fprintf(stderr, "No se pudieron cargar queries desde query.fvecs\n");
        return 1;
    }

    int K = 10;
    MatchResult *flat_result = calloc(K, sizeof(MatchResult));
    MatchResult *hnsw_result = calloc(K, sizeof(MatchResult));

    if (!flat_result || !hnsw_result) {
        perror("alloc match results");
        return 1;
    }

    double total_recall = 0.0;

    for (int q = 0; q < qtotal; q++) {
        if (search_n(flat, queries[q], DIMS, flat_result, K) != 0 ||
            search_n(hnsw, queries[q], DIMS, hnsw_result, K) != 0) {
            printf("Error en búsqueda para query %d\n", q);
            return 1;
        }

        int matches = 0;
        for (int i = 0; i < K; i++) {
            for (int j = 0; j < K; j++) {
                if (flat_result[i].id == hnsw_result[j].id) {
                    matches++;
                    break;
                }
            }
        }

        double recall = (double)matches / K;
        total_recall += recall;

        printf("Query %d: Recall@%d = %.2f | Flat[0] = %.4f | HNSW[0] = %.4f\n",
               q + 1, K, recall, flat_result[0].distance, hnsw_result[0].distance);
    }

    printf("Recall promedio HNSW vs Flat: %.2f\n", total_recall / qtotal);

    IndexStats st;
    stats(hnsw, &st); printf("HNSW:\n"); print_index_stats(&st);
    stats(flat, &st); printf("Flat:\n"); print_index_stats(&st);

    destroy_index(&flat);
    destroy_index(&hnsw);
    free(flat_result);
    free(hnsw_result);

    for (int i = 0; i < loaded; i++) free(dataset[i]);
    for (int i = 0; i < qtotal; i++) free(queries[i]);

    return 0;
}