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
- [ ] **Bindings** — Python / Go / Java — In progress 🚧 
- [ ] **Persistence** — `dump()` and `load()` — In progress 🚧 
- [ ] **Benchmarks** — In progress 🚧  
- [x] **Docs** — Public API complete 📄  
- [ ] **Package** - Package Distribution 

---

## 🚀 Public Interface (C API)

All functions operate through the abstract `Index` type:

| Function             | Description                                             |
|----------------------|---------------------------------------------------------|
| `alloc_index()`      | Allocates a new index in memory                         |
| `load_index()`       | Loads an index from disk                                |
| `destroy_index()`    | Frees memory and resources associated with an index     |
| `insert()`           | Inserts a new vector with its associated ID             |
| `delete()`           | Deletes a vector by ID                                  |
| `search()`           | Searches for the top-N closest vectors to a query       |
| `search_n()`         | Searches with a specific result count                   |
| `contains()`         | Checks if an ID exists in the index                     |
| `stats()`            | Returns statistics (nodes, edges, memory, etc.)         |
| `size()`             | Returns the number of vectors in the index              |
| `update_context()`   | Updates internal state (used post-optimization)         |
| `dump()`             | Serializes the full index to disk                       |

---

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
