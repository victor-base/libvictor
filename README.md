# License

This project is licensed under the [GNU Lesser General Public License v3.0](https://www.gnu.org/licenses/lgpl-3.0.html).


# 🧠 libvictor — Release Candidate (RC)

> **Version**: `v0.9.0-rc`  
> **Date**: April 2025  
> **Stage**: Release Candidate (First public-ready milestone)  

---

## 🧩 Overview

This RC of `libvictor` lays the foundation for a fast, embeddable and flexible vector search engine written in C. It supports multiple index types and is built with simplicity and speed in mind. This release includes:

- A stable public API
- Graph-based and flat vector indexing
- Extensible distance metrics
- Language bindings for Python, Go, and Java
- Linux distribution packages (`.deb`, `.tar.gz`)
- Persistence and restore capabilities

---

### ✅ Component Checklist

- [x] **Flat Index** — Stable & Functional
- [x] **Graph Index** — NSW Stable & Functional  
- [x] **Deletion** — Deferred delete / mark inactive  
- [ ] **Bindings** — Python / Go / Java — In progress / C# 🚧 
- [X] **Persistence** — `dump()` and `load()` — In progress 🚧 
- [ ] **Benchmarks** — In progress 🚧  
- [x] **Docs** — Public API complete 📄  
- [ ] **Package** - Package Distribution 

---

## Public C API – `libvictor`

`libvictor` exposes a minimal and high-performance C API for building and interacting with vector indexes such as Flat and NSW. This interface is designed to be embeddable, extensible, and compatible with C/C++ environments and foreign language bindings.

---

### Index Lifecycle

```c
Index *alloc_index(int type, int method, uint16_t dims, void *icontext);
int destroy_index(Index **index);
```

- `alloc_index`: Allocates and initializes a new index of the specified type (`FLAT_INDEX`, `NSW_INDEX`, etc.) and distance method (`L2NORM`, `COSINE`, `DOTP`).
- `destroy_index`: Frees all memory and resources associated with the index.

---

### Insertion & Deletion

```c
int insert(Index *index, uint64_t id, float32_t *vector, uint16_t dims);
int delete(Index *index, uint64_t id);
```

- `insert`: Inserts a vector with a unique ID into the index.
- `delete`: Removes a vector from the index by its ID.

---

### Search Operations

```c
int search(Index *index, float32_t *vector, uint16_t dims, MatchResult *result);
int search_n(Index *index, float32_t *vector, uint16_t dims, MatchResult *results, int n);

typedef struct {
    uint64_t  id;                  // ID of the matched vector
    float32_t distance;      // Distance or similarity score
} MatchResult;


```

- `search`: Finds the nearest neighbor to a given query vector.
- `search_n`: Finds the `n` closest neighbors to the query vector, sorted by distance or similarity.

---

### Index Info & Statistics

```c
int stats(Index *index, IndexStats *stats);

/**
 * Statistics structure for timing measurements.
 */
typedef struct {
    uint64_t count;      // Number of operations
    double   total;      // Total time in seconds
    double   last;		 // Last operation time
    double   min;        // Minimum operation time
    double   max;        // Maximum operation time
} TimeStat;

/**
 * Aggregate statistics for the index.
 */
typedef struct {
    TimeStat insert;     // Insert operations timing
    TimeStat delete;     // Delete operations timing
    TimeStat dump;       // Dump to file operation
    TimeStat search;     // Single search timing
    TimeStat search_n;   // Multi-search timing
} IndexStats;


int size(Index *index, uint64_t *sz);
int contains(Index *index, uint64_t id);
int update_icontext(Index *index, void *icontext);
```

- `stats`: Retrieves timing statistics for operations like search, insert, and dump.
- `size`: Returns the number of stored vectors.
- `contains`: Checks if a vector ID exists in the index.
- `update_icontext`: Updates the internal configuration/context of an index.

---

### Persistence (Save/Load)

```c
int dump(Index *index, const char *filename);
Index *load_index(const char *filename);
```

- `dump`: Serializes the index to a binary file for persistent storage.
- `load_index`: Reconstructs an index from a previously saved file.

---

### Comparison Methods

```c
#define L2NORM 0x00    // Euclidean Distance
#define COSINE 0x01    // Cosine Similarity
#define DOTP   0x02    // Dot Product
```

---

### Index Types

```c
#define FLAT_INDEX     0x00
#define FLAT_INDEX_MP  0x01
#define NSW_INDEX      0x02
#define HNSW_INDEX     0x03
```

---

### Versioning

```c
const char *__LIB_VERSION(void);
const char *__LIB_SHORT_VERSION(void);
```

Returns the full and short version strings of the `libvictor` library.

---

### Error Codes

These are returned by most public API functions to indicate success or failure.

| Code Name           | Description                                 |
|---------------------|---------------------------------------------|
| `SUCCESS`           | Operation completed successfully            |
| `INVALID_INIT`      | Invalid initialization state                |
| `INVALID_INDEX`     | Index pointer is null or invalid            |
| `INVALID_VECTOR`    | Malformed or null vector                    |
| `INVALID_RESULT`    | Output/result buffer is invalid             |
| `INVALID_DIMENSIONS`| Mismatched or out-of-bound dimensions       |
| `INVALID_ARGUMENT`  | General argument validation failure         |
| `INVALID_ID`        | Vector ID is invalid or zero                |
| `INVALID_REF`       | Reference pointer is invalid                |
| `DUPLICATED_ENTRY`  | Duplicate vector ID already exists          |
| `NOT_FOUND_ID`      | No entry found for the specified ID         |
| `INDEX_EMPTY`       | Index is empty and operation is not allowed |
| `THREAD_ERROR`      | Internal threading or locking issue         |
| `SYSTEM_ERROR`      | System-level or memory error                |
| `FILEIO_ERROR`      | File read/write failure                     |
| `NOT_IMPLEMENTED`   | Function not implemented for this index     |
| `INVALID_FILE`      | Malformed or corrupted file format          |


## 🧬 Internal Features

| Feature                  | Description                                                    |
|--------------------------|----------------------------------------------------------------|
| Multi-index architecture | Internal framework to support multiple indexing strategies     |
| Flat index               | Exact vector search                                            |
| Graph index (NSW)        | Navigable Small World approximation-based index                |
| Distance metrics         | `dot_product`, `cosine_similarity`, `L2_norm` (Euclidean)      |

All index types share a common abstract API, and new types can be registered via plugin.

---

## 🧪 Language Bindings

This release includes tested bindings for:

- **Python**: via `ctypes`/`cffi`, ready for NumPy arrays
- **Golang**: via cgo wrappers
- **Java**: via JNI for Android and server-side integrations

Bindings offer full access to the public interface.

---

## 📦 Distribution Packages

Available build artifacts:

### ✅ Debian Package (`.deb`)
- Target: Ubuntu 20.04+, Debian Bullseye+
- Arch: x86_64, ARM64
- Installs to: `/usr/local/lib/libvictor.so`, headers in `/usr/local/include/victor/`

### ✅ Tarball Source Distribution (`.tar.gz`)
- Contains:
  - `src/` (C source files)
  - `include/`
  - `Makefile`
  - `examples/`
  - `bindings/`
- Build with: `make all` or `cmake . && make`

To create:
```bash
make dist-deb
make dist-tar
