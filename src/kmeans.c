#include "kmeans.h"

/**
 * @brief Generates a random floating-point number between a and b.
 *
 * @param a Lower bound.
 * @param b Upper bound.
 * @return A random float between a and b.
 */
static float random_float_between(float a, float b);

/**
 * @brief Generates a random integer between a and b.
 *
 * @param a Lower bound (inclusive).
 * @param b Upper bound (exclusive).
 * @return A random integer in [a, b).
 */
static int random_int_between(int a, int b);

/**
 * @brief Clones an array of centroids.
 *
 * Allocates a new array of centroids and copies the contents of the input centroids.
 *
 * @param centroids The centroids to clone.
 * @param k Number of centroids.
 * @param dims Number of dimensions.
 * @return A newly allocated copy of the centroids, or NULL on failure.
 */
static float32_t **kmeans_clone_centroids(float32_t **centroids, int k, int dims);

/**
 * @brief Allocates memory for centroids.
 *
 * Creates an array of `k` centroid vectors, each of size `dims`.
 *
 * @param k Number of centroids.
 * @param dims Number of dimensions.
 * @return A newly allocated array of centroids, or NULL on failure.
 */
static float32_t **kmeans_alloc_centroids(int k, int dims);

/**
 * @brief Frees memory associated with centroids.
 *
 * Releases memory for each centroid vector and the array itself.
 *
 * @param centroids Array of centroid vectors to free.
 * @param k Number of centroids.
 */
static void kmeans_free_centroids(float32_t **centroids, int k);

/**
 * @brief Assigns a vector to the closest centroid.
 *
 * Finds the centroid with the minimum distance to the given vector.
 *
 * @param centroids Array of centroid vectors.
 * @param cn Number of centroids.
 * @param vector The vector to assign.
 * @param dims_aligned Number of aligned dimensions.
 * @return The index of the closest centroid.
 */
static int kmeans_assign(float32_t **centroids, int cn, float32_t *vector, int dims_aligned);

/**
 * @brief Computes the minimum squared distance from a vector to all centroids.
 *
 * @param centroids Array of centroid vectors.
 * @param cn Number of centroids.
 * @param vector The vector to evaluate.
 * @param dims_aligned Number of aligned dimensions.
 * @return The minimum squared distance.
 */
static int kmeans_min_squared_distance_to_centroids(float32_t **centroids, int cn, float32_t *vector, int dims_aligned);

/**
 * @brief Updates the centroid vector as the mean of its assigned points.
 *
 * @param centroid The centroid vector to update.
 * @param set The map of indices assigned to the centroid.
 * @param vectors Dataset of all vectors.
 * @param vn Number of vectors in the dataset.
 * @param dims_aligned Number of aligned dimensions.
 */
static void kmeans_train(float32_t *centroid, Map *set, float32_t **vectors, int vn, int dims_aligned);

/**
 * @brief Checks if the centroids have converged by comparing old and new centroids.
 *
 * @param old_centroids Previous centroids.
 * @param new_centroids Newly computed centroids.
 * @param cn Number of centroids.
 * @param dims_aligned Number of aligned dimensions.
 * @param epsilon Convergence threshold.
 * @return 1 if converged, 0 otherwise.
 */
__attribute__((unused)) static int kmeans_converged(float32_t **old_centroids, float32_t **new_centroids, int cn, int dims_aligned, float32_t epsilon);

/**
 * @brief Checks global convergence by calculating the total centroid shift.
 *
 * @param old_centroids Previous centroids.
 * @param new_centroids Newly computed centroids.
 * @param cn Number of centroids.
 * @param dims Number of dimensions.
 * @param epsilon Convergence threshold.
 * @return 1 if converged, 0 otherwise.
 */
static int kmeans_converged_global(float32_t **old_centroids, float32_t **new_centroids, int cn, int dims, float32_t epsilon);

/**
 * @brief Chooses the next centroid in the K-Means++ initialization.
 *
 * Selects a new centroid with probability proportional to the squared distance
 * from existing centroids.
 *
 * @param centroids Array of current centroids.
 * @param cn Number of current centroids.
 * @param dataset The dataset of vectors.
 * @param distances Temporary array of distances.
 * @param n Number of dataset vectors.
 * @param dims Number of dimensions.
 * @return The index of the chosen centroid in the dataset, or -1 on failure.
 */
static int kmeans_choose_next_center(float32_t **centroids, int cn, float32_t **dataset, float32_t *distances, int n, int dims);




static float random_float_between(float a, float b) {
    return ((float)rand() / RAND_MAX) * (b - a) + a;
}

static int random_int_between(int a, int b) {
	return ((int)rand() / RAND_MAX) * (b - a) + a;
}

static float32_t **kmeans_clone_centroids(float32_t **centroids, int k, int dims) {
    float32_t **c = (float32_t **) calloc_mem(k, sizeof(float32_t *));
	if (!c) 
		return NULL;
	for (int i = 0; i < k; i++) {
        c[i] = (float32_t *) calloc_mem(dims, sizeof(float32_t));
        if (!c[i]) {
			for (i = i -1; i>=0; i--)
				free_mem(c[i]);
			free_mem(c);
			return NULL;
		}
		memcpy(c[i], centroids[i], sizeof(float32_t) * dims);
    }
    return c;
}


static float32_t **kmeans_alloc_centroids(int k, int dims) {
	float32_t **c = (float32_t **) calloc_mem(k, sizeof(float32_t *));
	if (!c) 
		return NULL;
	for (int i = 0; i < k; i++) {
        c[i] = (float32_t *) calloc_mem(dims, sizeof(float32_t));
        if (!c[i]) {
			for (i = i -1; i>=0; i--)
				free_mem(c[i]);
			free_mem(c);
			return NULL;
		}
    }
    return c;
}

static void kmeans_free_centroids(float32_t **centroids, int k) {
    if (!centroids) return;
    
    for (int i = 0; i < k; i++) {
        if (centroids[i])
            free_mem(centroids[i]);
    }

    free_mem(centroids);
}



void kmeans_destroy_context(KMContext **context) {
	KMContext *ctx = *context;
	if (!ctx)
		return;
	if (ctx->centroids) {
		kmeans_free_centroids(ctx->centroids, ctx->c);
		ctx->centroids = NULL;
	}
	if (ctx->sets) {
		for (int i = 0; i < ctx->c; i++) 
			map_destroy(&ctx->sets[i]);
		ctx->sets = NULL;
	}
	ctx->dataset = NULL;
	ctx->c = 0;
	ctx->n = 0;
	ctx->dims = 0;
	ctx->epsilon = 0;
	ctx->miter = 0;
	ctx->citer = 0;
	free_mem(ctx);
	*context = NULL;
}


KMContext *kmeans_create_context(int c, float32_t **dataset, int n, int dims, float32_t epsilon, int max_iter) {
	KMContext *ctx  = NULL;
	float32_t **tmp = NULL;
	float32_t *distances = NULL;

	if (c >= n || c <= 0 || n <= 0 || dataset == NULL) {
		return NULL;
	}

	ctx = (KMContext *) calloc_mem(1, sizeof(KMContext));
	if (!ctx) return NULL;

	ctx->c = c;
	ctx->n = n;
	ctx->dims = dims;
	ctx->dataset = dataset;
	ctx->epsilon = epsilon;
	ctx->miter = max_iter;
	ctx->citer = 0;
	ctx->sets  = NULL;
	ctx->centroids = NULL;

	tmp = (float32_t **) calloc_mem(c, sizeof(float32_t *));
	if (!tmp) {
		free_mem(ctx);
		return NULL;
	}

	distances = (float32_t *) calloc_mem(n, sizeof(float32_t));
	if (!distances) 
		goto return_error;

	for (int i = 0; i < c; i++) {
		int p;
		if ((p = kmeans_choose_next_center(tmp, i, dataset, distances, n, dims)) == -1) 
			goto return_error;
		tmp[i] = dataset[p];
		memset(distances, 0, sizeof(float32_t) * n);
	}
	free_mem(distances);

	ctx->sets = (Map *) calloc_mem(c, sizeof(Map));
	if (!ctx->sets) 
		goto return_error;

	for (int i = 0; i < c; i++) {
		if (init_map(&(ctx->sets[i]), n / c, 15) != MAP_SUCCESS)
			goto return_error;
			
	}

	ctx->centroids = kmeans_clone_centroids(tmp, c, dims);
	free_mem(tmp);

	if (!ctx->centroids) 
		goto return_error;
	return ctx;

return_error:
	if (distances)
		free_mem(distances);
	if (tmp)
		free_mem(tmp);
	kmeans_destroy_context(&ctx);
	return NULL;
}


int kmeans_pp_train(KMContext *ctx) {
    if (!ctx || !ctx->centroids || !ctx->dataset || !ctx->sets)
        return SYSTEM_ERROR;

    float32_t **tmp = kmeans_alloc_centroids(ctx->c, ctx->dims);
    if (!tmp)
        return SYSTEM_ERROR;

    for (ctx->citer = 0; ctx->citer < ctx->miter; ctx->citer++) {

        for (int i = 0; i < ctx->n; i++) {
            int p = kmeans_assign(ctx->centroids, ctx->c, ctx->dataset[i], ctx->dims);
            if (p < 0 || p >= ctx->c) {
                kmeans_free_centroids(tmp, ctx->c);
                return SYSTEM_ERROR;
            }
            map_insert(&ctx->sets[p], i, 0);
        }

        for (int i = 0; i < ctx->c; i++)
            kmeans_train(tmp[i], &ctx->sets[i], ctx->dataset, ctx->n, ctx->dims);

        if (kmeans_converged_global(ctx->centroids, tmp, ctx->c, ctx->dims, ctx->epsilon))
            break;

        for (int i = 0; i < ctx->c; i++) {
            memcpy(ctx->centroids[i], tmp[i], sizeof(float32_t) * ctx->dims);
			map_purge(&ctx->sets[i]);
		}
    }

    kmeans_free_centroids(tmp, ctx->c);
    return SUCCESS;
}


/**
 * @brief Returns a table of raw float pointers from an array of Vector pointers.
 * @param vectors Array of Vector pointers.
 * @param size Number of vectors.
 * @return Pointer to an array of float32_t pointers, or NULL on allocation failure.
 */
float32_t **raw_vectors(Vector **vectors, int size) {
	float32_t **table = (float32_t **) calloc_mem(size, sizeof(float32_t *));
	if (!table) 
		return NULL;
	for (int i = 0; i < size; i++) {
		if (!vectors[i])
			continue;
		table[i] = vectors[i]->vector;
	}
	return table;
}


static int kmeans_assign(float32_t **centroids, int cn, float32_t *vector, int dims_aligned){
	float32_t distance;
	float32_t best = INFINITY;
	int selected = -1;

	for (int i = 0; i < cn; i++) {
		distance = euclidean_distance_squared(centroids[i], vector, dims_aligned);
		if (distance < best) {
			selected = i;
			best = distance;
		}
	}
	return selected;
}

static int kmeans_min_squared_distance_to_centroids(float32_t **centroids, int cn, float32_t *vector, int dims_aligned){
	float32_t distance;
	float32_t min = INFINITY;

	for (int i = 0; i < cn; i++) {
		distance = euclidean_distance_squared(centroids[i], vector, dims_aligned);
		if (distance < min) 
			min = distance;
	}
	return min;
}



static void kmeans_train(float32_t *centroid, Map *set, float32_t **vectors, int vn, int dims_aligned) {
	for (int i = 0; i < dims_aligned; i++) {
		float32_t a = 0;
		int count = 0;
		for (int j = 0; j < vn; j++) {
			if (map_has(set, j)) {
				a += vectors[j][i];
				count++;
			}
		}
		centroid[i] = (count > 0) ? (a / count) : 0.0f;
	}
}


static int kmeans_converged(float32_t **old_centroids, float32_t **new_centroids, int cn, int dims_aligned, float32_t epsilon) {
    for (int i = 0; i < cn; i++) {
        float32_t dist = euclidean_distance_squared(old_centroids[i], new_centroids[i], dims_aligned);
        if (dist > epsilon) {
            return 0;
        }
    }
    return 1;
}

static int kmeans_converged_global(float32_t **old_centroids, float32_t **new_centroids, int cn, int dims, float32_t epsilon) {
    float32_t total_shift = 0.0f;
	if (old_centroids == NULL)
		return 0;
    for (int i = 0; i < cn; i++) {
        total_shift += euclidean_distance_squared(old_centroids[i], new_centroids[i], dims);
    }

    return (total_shift < epsilon) ? 1 : 0;
}


static int kmeans_choose_next_center(float32_t **centroids, int cn, float32_t **dataset, float32_t *distances, int n, int dims) {
	PANIC_IF(centroids == NULL, "centroid MUST not be NULL");
	PANIC_IF(dataset == NULL, "dataset MUST not be NULL");
    float32_t total = 0.0f;

	if (n == 0) return -1;
	if (cn == 0) {
		return random_int_between(0, n);
	}

    for (int i = 0; i < n; i++) {
        distances[i] = kmeans_min_squared_distance_to_centroids(centroids, cn, dataset[i], dims);
        total += distances[i];
    }

    float32_t r = random_float_between(0.0f, total);
    float32_t accum = 0.0f;

    for (int i = 0; i < n; i++) {
        accum += distances[i];
        if (r <= accum) {
		
            return i;
        }
    }
    return n - 1;  
}
