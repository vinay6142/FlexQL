#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <mutex>
#include <cstdio>
#include <string>

// Data types (outside namespace because macros don't respect namespaces)
#define TYPE_INT 0
#define TYPE_DECIMAL 1
#define TYPE_STRING 2

#define MAX_COLUMNS 100
#define MAX_COLUMN_NAME 64
#define MAX_TABLE_NAME 64
#define BATCH_SIZE_BYTES (100 * 1024)

namespace flexql {
namespace storage {

// Column metadata
struct Column {
    char name[MAX_COLUMN_NAME];
    char type[64];
    int type_id;
};

// Value struct
struct Value {
    int type;
    bool is_null;
    int64_t int_val;
    double decimal_val;
    std::string string_val;
};

// Row buffer
struct RowBuffer {
    std::vector<uint8_t> data;
};

// Table
struct Table {
    char name[MAX_TABLE_NAME];
    Column* columns;
    int column_count;
    FILE* file;
    uint64_t file_size;
    std::vector<uint8_t> write_buffer;
    std::vector<const char*> insert_string_ptrs;
    std::vector<uint32_t> insert_string_lengths;
    std::vector<char> string_pool;
    size_t string_pool_offset;
    std::mutex table_mutex;
    bool bulk_insert_mode;
};

// Storage operations
Table* create_table(const std::string& name, const std::vector<Column>& columns);
Table* open_table(const std::string& name, const std::vector<Column>& columns);
uint64_t insert_row(Table* table, const std::vector<Value>& values);
uint64_t insert_rows_batch(Table* table, const std::vector<std::vector<Value>>& rows);
RowBuffer* read_row(Table* table, uint64_t offset);
void flush_table(Table* table);
void scan_table(Table* table, bool (*callback)(RowBuffer*, void*), void* arg);
void scan_table_with_offset(Table* table, uint64_t start_offset,
                            bool (*callback)(RowBuffer*, void*), void* arg);
void delete_row(Table* table, uint64_t offset);
void update_row(Table* table, uint64_t offset, const std::vector<Value>& values);

} // namespace storage
} // namespace flexql
