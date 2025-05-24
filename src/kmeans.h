#ifndef _KMEANS_H
#define _KMEANS_H

#include "vector.h"
#include "vmath.h"
#include "map.h"
#include "mem.h"

/**
 * @brief Context for K-Means clustering.
 *
 * This structure holds the state of a K-Means++ clustering operation.
 * It includes the dataset to cluster, the current centroids, and the maps that track
 * point assignments to clusters.
 */
typedef struct {
    float32_t **centroids;  ///< Array of centroid vectors (size: c x dims)
    float32_t **dataset;    ///< Dataset vectors (size: n x dims)
    int c;                  ///< Number of centroids (clusters)
    int n;                  ///< Number of data vectors
    int dims;               ///< Number of dimensions
    int epsilon;            ///< Convergence threshold
    int miter;              ///< Maximum iterations to perform
    int citer;              ///< Iterations actually used
    Map *sets;              ///< Array of maps to track which points belong to each cluster
} KMContext;

/**
 * @brief Returns a table of raw float pointers from an array of Vector pointers.
 *
 * @param vectors Array of Vector pointers.
 * @param size Number of vectors.
 * @return Pointer to an array of float32_t pointers (each entry pointing to a raw vector),
 *         or NULL on allocation failure.
 */
float32_t **raw_vectors(Vector **vectors, int size);

/**
 * @brief Create a K-Means context and initialize it with centroids.
 *
 * This function allocates and initializes a KMContext structure for K-Means++ training.
 * It also performs the centroid initialization phase (K-Means++ seeding).
 *
 * @param c Number of centroids (clusters) to generate.
 * @param dataset Dataset to cluster (array of float32_t pointers).
 * @param n Number of dataset vectors.
 * @param dims Number of dimensions of each vector.
 * @param epsilon Convergence threshold.
 * @param max_iter Maximum iterations to perform in the training phase.
 *
 * @return A pointer to the newly allocated KMContext, or NULL on failure.
 *         Use kmeans_destroy_context() to free it.
 */
KMContext *kmeans_create_context(int c, float32_t **dataset, int n, int dims, float32_t epsilon, int max_iter);

/**
 * @brief Free the resources allocated for a K-Means context.
 *
 * This function releases all memory associated with a KMContext, including centroids
 * and cluster maps. It sets the pointer to NULL to avoid dangling references.
 *
 * @param context Pointer to the KMContext pointer to destroy.
 */
void kmeans_destroy_context(KMContext **context);

/**
 * @brief Perform the K-Means++ training phase on a given context.
 *
 * This function iteratively assigns points to the nearest centroid and recomputes centroids
 * until convergence or until the maximum number of iterations is reached.
 * After completion, the centroids and the cluster assignments are updated in the context.
 *
 * @param ctx Pointer to the KMContext to train. Must be previously initialized with
 *            kmeans_create_context().
 *
 * @return SUCCESS on successful convergence or reaching max iterations,
 *         SYSTEM_ERROR on failure (e.g., memory or data inconsistency).
 */
int kmeans_pp_train(KMContext *ctx);

#endif