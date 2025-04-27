#ifndef VICTOR_INDEX_HPP
#define VICTOR_INDEX_HPP

extern "C" {
#include <victor/victor.h>
}

#include <vector>
#include <stdexcept>
#include <cstdint>
#include <string>

class VictorIndex {
private:
    Index* index_;  

    explicit VictorIndex(Index* idx) : index_(idx) {
        if (!index_) {
            throw std::runtime_error("Failed to load Index");
        }
    }


public:
    /**
     * @brief Constructs a new VictorIndex object.
     * 
     * @param type Index type (e.g., FLAT_INDEX).
     * @param method Distance metric (e.g., L2NORM, COSINE).
     * @param dims Number of dimensions per vector.
     * @throws std::runtime_error if allocation fails.
     */
    VictorIndex(int type, int method, uint16_t dims) {
        NSWContext ctx;
        if (type == NSW_INDEX) {
            ctx.ef_construct = 64;
            ctx.ef_search = 100;
            ctx.odegree = 32;
            index_ = alloc_index(type, method, dims, &ctx);
        } else {
            index_ = alloc_index(type, method, dims, nullptr);
        }
        
        if (!index_) {
            throw std::runtime_error("Failed to allocate Index");
        }
    }

    /**
     * @brief Destroys the VictorIndex object and releases resources.
     */
    ~VictorIndex() {
        if (index_) {
            ::destroy_index(&index_);
            index_ = nullptr;
        }
    }

    /**
     * @brief Static method to load a VictorIndex from a file.
     * 
     * @param filename Path to the dumped index file.
     * @return VictorIndex instance loaded from file.
     * @throws std::runtime_error if loading fails.
     */
    static VictorIndex load(const std::string& filename) {
        Index* idx = ::load_index(filename.c_str());
        if (!idx) {
            throw std::runtime_error("Failed to load Index from file");
        }
        return VictorIndex(idx);
    }


    /**
     * @brief Inserts a vector into the index.
     * 
     * @param id Unique identifier for the vector.
     * @param vec Vector data (must match index dimensions).
     * @throws std::runtime_error on failure.
     */
    void insert(uint64_t id, const std::vector<float>& vec) {
        if (!index_) throw std::runtime_error("Invalid Index");
        if (::insert(index_, id, const_cast<float*>(vec.data()), static_cast<uint16_t>(vec.size())) != SUCCESS) {
            throw std::runtime_error("Insert operation failed");
        }
    }

    /**
     * @brief Searches for the nearest neighbor to a query vector.
     * 
     * @param vec Query vector.
     * @return std::pair of (id, distance).
     * @throws std::runtime_error on failure.
     */
    std::pair<uint64_t, float> search(const std::vector<float>& vec) {
        if (!index_) throw std::runtime_error("Invalid Index");
        MatchResult result;
        if (::search(index_, const_cast<float*>(vec.data()), static_cast<uint16_t>(vec.size()), &result) != SUCCESS) {
            throw std::runtime_error("Search operation failed");
        }
        return { result.id, result.distance };
    }



    /**
     * @brief Searches for the top N nearest neighbors.
     * 
     * @param vec Query vector.
     * @param n Number of nearest neighbors to find.
     * @return Vector of (id, distance) pairs.
     * @throws std::runtime_error on failure.
     */
    std::vector<std::pair<uint64_t, float>> search_n(const std::vector<float>& vec, int n) {
        if (!index_) throw std::runtime_error("Invalid Index");
        std::vector<MatchResult> results(n);
        if (::search_n(index_, const_cast<float*>(vec.data()), static_cast<uint16_t>(vec.size()), results.data(), n) != SUCCESS) {
            throw std::runtime_error("Search_n operation failed");
        }
        std::vector<std::pair<uint64_t, float>> output;
        output.reserve(n);
        for (const auto& r : results) {
            output.emplace_back(r.id, r.distance);
        }
        return output;
    }

    /**
     * @brief Deletes a vector by ID from the index.
     * 
     * @param id Unique identifier of the vector to delete.
     * @throws std::runtime_error on failure.
     */
    void remove(uint64_t id) {
        if (!index_) throw std::runtime_error("Invalid Index");
        if (::cpp_delete(index_, id) != SUCCESS) {
            throw std::runtime_error("Delete operation failed");
        }
    }

    /**
     * @brief Checks if an ID exists in the index.
     * 
     * @param id Vector ID to check.
     * @return true if found, false otherwise.
     */
    bool contains(uint64_t id) {
        if (!index_) throw std::runtime_error("Invalid Index");
        return ::contains(index_, id) != 0;
    }

    
    /**
     * @brief Gets the current number of elements in the index.
     * 
     * @return Number of elements.
     * @throws std::runtime_error on failure.
     */
    uint64_t size() {
        if (!index_) throw std::runtime_error("Invalid Index");
        uint64_t sz = 0;
        if (::size(index_, &sz) != SUCCESS) {
            throw std::runtime_error("Size operation failed");
        }
        return sz;
    }

    /**
     * @brief Dumps the index to a file.
     * 
     * @param filename Path to save the dumped index.
     * @throws std::runtime_error on failure.
     */
    void dump(const std::string& filename) {
        if (!index_) throw std::runtime_error("Invalid Index");
        if (::dump(index_, filename.c_str()) != SUCCESS) {
            throw std::runtime_error("Dump operation failed");
        }
    }

    /**
     * @brief Retrieves performance statistics.
     * 
     * @return IndexStats structure with timing data.
     * @throws std::runtime_error on failure.
     */
    IndexStats stats() {
        if (!index_) throw std::runtime_error("Invalid Index");
        IndexStats s;
        if (::stats(index_, &s) != SUCCESS) {
            throw std::runtime_error("Stats operation failed");
        }
        return s;
    }
};

#endif // VICTOR_INDEX_HPP
