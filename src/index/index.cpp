#include "index/index.hpp"
#include <map>
#include <unordered_map>
#include <cstring>

namespace flexql {
namespace index {

struct IntIndex {
    std::map<int64_t, uint64_t> data;
};

struct DecimalIndex {
    std::map<double, uint64_t> data;
};

struct StringIndex {
    std::unordered_map<std::string, uint64_t> data;
};

Index* create_index(int type) {
    Index* idx = new Index();
    idx->key_type = type;

    if (type == TYPE_INT) {
        idx->data = new IntIndex();
    } else if (type == TYPE_DECIMAL) {
        idx->data = new DecimalIndex();
    } else if (type == TYPE_STRING) {
        idx->data = new StringIndex();
    }

    return idx;
}

void index_insert_int(Index* idx, int64_t key, uint64_t offset) {
    if (idx && idx->data && idx->key_type == INDEX_TYPE_INT) {
        IntIndex* int_idx = (IntIndex*)idx->data;
        int_idx->data[key] = offset;
    }
}

void index_insert_decimal(Index* idx, double key, uint64_t offset) {
    if (idx && idx->data && idx->key_type == INDEX_TYPE_DECIMAL) {
        DecimalIndex* dec_idx = (DecimalIndex*)idx->data;
        dec_idx->data[key] = offset;
    }
}

void index_insert_string(Index* idx, const char* key, uint64_t offset) {
    if (idx && idx->data && idx->key_type == INDEX_TYPE_STRING && key) {
        StringIndex* str_idx = (StringIndex*)idx->data;
        str_idx->data[std::string(key)] = offset;
    }
}

uint64_t index_lookup_int(Index* idx, int64_t key) {
    if (idx && idx->data && idx->key_type == INDEX_TYPE_INT) {
        IntIndex* int_idx = (IntIndex*)idx->data;
        auto it = int_idx->data.find(key);
        if (it != int_idx->data.end()) {
            return it->second;
        }
    }
    return 0;
}

uint64_t index_lookup_decimal(Index* idx, double key) {
    if (idx && idx->data && idx->key_type == INDEX_TYPE_DECIMAL) {
        DecimalIndex* dec_idx = (DecimalIndex*)idx->data;
        auto it = dec_idx->data.find(key);
        if (it != dec_idx->data.end()) {
            return it->second;
        }
    }
    return 0;
}

uint64_t index_lookup_string(Index* idx, const char* key) {
    if (idx && idx->data && idx->key_type == INDEX_TYPE_STRING && key) {
        StringIndex* str_idx = (StringIndex*)idx->data;
        auto it = str_idx->data.find(std::string(key));
        if (it != str_idx->data.end()) {
            return it->second;
        }
    }
    return 0;
}

} // namespace index
} // namespace flexql
