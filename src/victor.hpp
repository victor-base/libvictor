#ifndef VICTOR_INDEX_HPP
#define VICTOR_INDEX_HPP

extern "C" {
#include <victor/victor.h>
}

#include <vector>
#include <stdexcept>
#include <cstdint>
#include <string>

/**
 * Macro to throw a runtime error if a Victor C API call fails.
 *
 * Usage:
 *   THROW_IF_FAIL(insert(...), "Insert failed");
 */
#define THROW_IF_FAIL(expr, msg)                                  \
    do {                                                          \
        ErrorCode _err = (ErrorCode) (expr);                      \
        if (_err != SUCCESS) {                                    \
            throw std::runtime_error(                             \
                std::string(msg) + ": " + victor_strerror(_err)); \
        }                                                         \
    } while (0)

class VictorIndex {
private:
    Index* index_;

    explicit VictorIndex(Index* idx) : index_(idx) {
        if (!index_) {
            throw std::runtime_error("Failed to load Index");
        }
    }

public:
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

    ~VictorIndex() {
        if (index_) {
            ::destroy_index(&index_);
            index_ = nullptr;
        }
    }

    static VictorIndex load(const std::string& filename) {
        Index* idx = ::load_index(filename.c_str());
        if (!idx) {
            throw std::runtime_error("Failed to load Index from file");
        }
        return VictorIndex(idx);
    }

    void insert(uint64_t id, const std::vector<float>& vec) {
        if (!index_) throw std::runtime_error("Invalid Index");
        THROW_IF_FAIL(::insert(index_, id, const_cast<float*>(vec.data()), static_cast<uint16_t>(vec.size())),
                      "Insert operation failed");
    }

    
    std::pair<uint64_t, float> search(const std::vector<float>& vec) {
        if (!index_) throw std::runtime_error("Invalid Index");
        MatchResult result;
        THROW_IF_FAIL(::search(index_, const_cast<float*>(vec.data()), static_cast<uint16_t>(vec.size()), &result),
                      "Search operation failed");
        return { result.id, result.distance };
    }

    std::vector<std::pair<uint64_t, float>> search_n(const std::vector<float>& vec, int n) {
        if (!index_) throw std::runtime_error("Invalid Index");
        std::vector<MatchResult> results(n);
        THROW_IF_FAIL(::search_n(index_, const_cast<float*>(vec.data()), static_cast<uint16_t>(vec.size()), results.data(), n),
                      "Search_n operation failed");

        std::vector<std::pair<uint64_t, float>> output;
        output.reserve(n);
        for (const auto& r : results) {
            output.emplace_back(r.id, r.distance);
        }
        return output;
    }

    void remove(uint64_t id) {
        if (!index_) throw std::runtime_error("Invalid Index");
        THROW_IF_FAIL(::cpp_delete(index_, id), "Delete operation failed");
    }

    bool contains(uint64_t id) {
        if (!index_) throw std::runtime_error("Invalid Index");
        return ::contains(index_, id) != 0;
    }

    uint64_t size() {
        if (!index_) throw std::runtime_error("Invalid Index");
        uint64_t sz = 0;
        THROW_IF_FAIL(::size(index_, &sz), "Size operation failed");
        return sz;
    }

    void dump(const std::string& filename) {
        if (!index_) throw std::runtime_error("Invalid Index");
        THROW_IF_FAIL(::dump(index_, filename.c_str()), "Dump operation failed");
    }

    IndexStats stats() {
        if (!index_) throw std::runtime_error("Invalid Index");
        IndexStats s;
        THROW_IF_FAIL(::stats(index_, &s), "Stats operation failed");
        return s;
    }
};

#endif // VICTOR_INDEX_HPP
