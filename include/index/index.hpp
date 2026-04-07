#pragma once

#include <string>
#include <vector>
#include "storage/storage.hpp"

// Index types (outside namespace because macros don't respect namespaces)
#define INDEX_TYPE_INT 0
#define INDEX_TYPE_DECIMAL 1
#define INDEX_TYPE_STRING 2

namespace flexql {
namespace index {

struct Index {
    int key_type;
    int index_type;
    void* data;
    std::mutex index_mutex;
};

Index* create_index(int type);
void index_insert_int(Index* idx, int64_t key, uint64_t offset);
void index_insert_decimal(Index* idx, double key, uint64_t offset);
void index_insert_string(Index* idx, const char* key, uint64_t offset);

uint64_t index_lookup_int(Index* idx, int64_t key);
uint64_t index_lookup_decimal(Index* idx, double key);
uint64_t index_lookup_string(Index* idx, const char* key);

} // namespace index
} // namespace flexql
