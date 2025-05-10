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
#define TOP_N 10
#define NUM_VECTORS 20000
#define QUERY_COUNT 1000
#define NOISE_LEVEL 0.2f

void print_index_stats(const IndexStats *stats) {
    if (!stats) return;

    const char *labels[] = { "insert", "delete", "search", "search_n", "dump" };
    const TimeStat *all_stats[] = {
        &stats->insert, &stats->delete, &stats->search,
        &stats->search_n, &stats->dump
    };

    for (int i = 0; i < 5; i++) {
        const TimeStat *s = all_stats[i];
        double avg = (s->count > 0) ? s->total / s->count : 0.0;
        printf("%-9s count = %-8lu total = %9.3f ms    avg = %7.3f ms    min = %7.3f ms    max = %7.3f ms\n",
               labels[i], s->count, s->total, avg, s->min, s->max);
    }
}

void normalize_vector(float *vec, int dims) {
    float norm = 0.0f;
    for (int i = 0; i < dims; i++) norm += vec[i] * vec[i];
    norm = sqrtf(norm);
    if (norm > 0.0f) for (int i = 0; i < dims; i++) vec[i] /= norm;
}

void add_noise(float *dst, float *src, int dims, float noise_level) {
    float norm = 0.0f;
    for (int i = 0; i < dims; i++) {
        float noise = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * noise_level;
        dst[i] = src[i] + noise;
        norm += dst[i] * dst[i];
    }
    norm = sqrtf(norm);
    if (norm > 0.0f) for (int i = 0; i < dims; i++) dst[i] /= norm;
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

int main() {
    srand(time(NULL));

    HNSWContext context = {
        .ef_construct = 240,
        .ef_search = 240,
        .M0 = 32
    };

    printf("Lib version: %s\n", __LIB_VERSION());

    Index *flat = alloc_index(FLAT_INDEX, DOTP, DIMS, NULL);
    Index *hnsw = alloc_index(HNSW_INDEX, DOTP, DIMS, &context);
    if (!flat || !hnsw) {
        printf("Error creando índices.\n");
        return 1;
    }

    float *dataset[NUM_VECTORS];
    int loaded = load_fvecs_file("base.fvecs", dataset, NUM_VECTORS, DIMS);
    if (loaded != NUM_VECTORS) {
        fprintf(stderr, "No se pudieron cargar los %d vectores.\n", NUM_VECTORS);
        return 1;
    }

    // Insertar todos los vectores
    for (int i = 0; i < loaded; i++) {
        if (insert(flat, i + 1, dataset[i], DIMS) != 0 ||
            insert(hnsw, i + 1, dataset[i], DIMS) != 0) {
            printf("Error insertando vector %d\n", i + 1);
            return 1;
        }
		if (i % 5000 == 0)
			printf("%10d - inserted\n", i);
    }

    MatchResult *flat_result = calloc(TOP_N, sizeof(MatchResult));
    MatchResult *hnsw_result = calloc(TOP_N, sizeof(MatchResult));
    float noisy_query[DIMS];
    double total_recall = 0.0;

    for (int q = 0; q < QUERY_COUNT; q++) {
        int qid = rand() % NUM_VECTORS;
        add_noise(noisy_query, dataset[qid], DIMS, NOISE_LEVEL);

        if (search_n(flat, noisy_query, DIMS, flat_result, TOP_N) != 0 ||
            search_n(hnsw, noisy_query, DIMS, hnsw_result, TOP_N) != 0) {
            printf("Error en búsqueda para query %d\n", q);
            return 1;
        }

        int matches = 0;
        for (int i = 0; i < TOP_N; i++) {
            for (int j = 0; j < TOP_N; j++) {
                if (flat_result[i].id == hnsw_result[j].id) {
                    matches++;
                    break;
                }
            }
        }

        double recall = (double)matches / TOP_N;
        total_recall += recall;

        printf("Query %d: Recall@%d = %.2f | Flat[0] = %.4f | HNSW[0] = %.4f\n",
               q + 1, TOP_N, recall, flat_result[0].distance, hnsw_result[0].distance);
    }

    printf("Recall promedio HNSW vs Flat (ruido %.2f): %.4f\n", NOISE_LEVEL, total_recall / QUERY_COUNT);

    IndexStats st;
    stats(hnsw, &st); printf("HNSW:\n"); print_index_stats(&st);
    stats(flat, &st); printf("Flat:\n"); print_index_stats(&st);

    destroy_index(&flat);
    destroy_index(&hnsw);
    free(flat_result);
    free(hnsw_result);
    for (int i = 0; i < loaded; i++) free(dataset[i]);

    return 0;
}