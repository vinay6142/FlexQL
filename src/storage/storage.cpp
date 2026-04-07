#include "storage/storage.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>

namespace flexql {
namespace storage {

Table* create_table(const std::string& name, const std::vector<Column>& columns) {
    Table* table = new Table();
    strncpy(table->name, name.c_str(), MAX_TABLE_NAME - 1);
    table->name[MAX_TABLE_NAME - 1] = '\0';

    table->column_count = columns.size();
    table->columns = new Column[columns.size()];
    for (size_t i = 0; i < columns.size(); i++) {
        table->columns[i] = columns[i];
    }

    std::string filename = "tables/" + name + ".db";
    table->file = fopen(filename.c_str(), "w+b");
    if (!table->file) {
        delete[] table->columns;
        delete table;
        return nullptr;
    }

    table->file_size = 0;
    table->string_pool_offset = 0;
    table->bulk_insert_mode = false;
    table->write_buffer.reserve(BATCH_SIZE_BYTES);
    table->insert_string_ptrs.reserve(MAX_COLUMNS);
    table->insert_string_lengths.reserve(MAX_COLUMNS);
    table->string_pool.reserve(10 * 1024 * 1024);

    return table;
}

Table* open_table(const std::string& name, const std::vector<Column>& columns) {
    Table* table = new Table();
    strncpy(table->name, name.c_str(), MAX_TABLE_NAME - 1);
    table->name[MAX_TABLE_NAME - 1] = '\0';

    table->column_count = columns.size();
    table->columns = new Column[columns.size()];
    for (size_t i = 0; i < columns.size(); i++) {
        table->columns[i] = columns[i];
    }

    std::string filename = "tables/" + name + ".db";
    table->file = fopen(filename.c_str(), "r+b");
    if (!table->file) {
        delete[] table->columns;
        delete table;
        return nullptr;
    }

    fseek(table->file, 0, SEEK_END);
    table->file_size = ftell(table->file);
    fseek(table->file, 0, SEEK_SET);

    table->string_pool_offset = 0;
    table->bulk_insert_mode = false;
    table->write_buffer.reserve(BATCH_SIZE_BYTES);
    table->insert_string_ptrs.reserve(MAX_COLUMNS);
    table->insert_string_lengths.reserve(MAX_COLUMNS);
    table->string_pool.reserve(10 * 1024 * 1024);

    return table;
}

bool validate_values(Table* table, const std::vector<Value>& values) {
    if ((int)values.size() != table->column_count) {
        return false;
    }
    for (int i = 0; i < table->column_count; i++) {
        if (!values[i].is_null && values[i].type != table->columns[i].type_id) {
            return false;
        }
    }
    return true;
}

char* allocate_string(Table* table, const std::string& str) {
    size_t needed = table->string_pool_offset + str.length() + 1;
    if (needed > table->string_pool.capacity()) {
        table->string_pool.reserve(needed * 2);
    }
    if (table->string_pool.size() < needed) {
        table->string_pool.resize(needed);
    }

    char* ptr = table->string_pool.data() + table->string_pool_offset;
    memcpy(ptr, str.c_str(), str.length());
    ptr[str.length()] = '\0';
    table->string_pool_offset += str.length() + 1;

    return ptr;
}

uint32_t calculate_row_size_fast(Table* table, const std::vector<Value>& values,
                                  std::vector<uint32_t>* string_lengths) {
    uint32_t bitmap_size = (table->column_count + 7) / 8;
    uint32_t size = 4 + bitmap_size;

    for (int i = 0; i < table->column_count; i++) {
        if (!values[i].is_null) {
            if (values[i].type == TYPE_INT) {
                size += 8;
            } else if (values[i].type == TYPE_DECIMAL) {
                size += 8;
            } else if (values[i].type == TYPE_STRING) {
                if (string_lengths) {
                    size += 4 + (*string_lengths)[i];
                } else {
                    size += 4 + values[i].string_val.length();
                }
            }
        }
    }

    return size;
}

void encode_row_into_buffer(Table* table, const std::vector<Value>& values,
                           std::vector<uint8_t>& buffer, uint32_t row_size,
                           std::vector<const char*>* string_ptrs,
                           std::vector<uint32_t>* string_lengths) {
    size_t offset = buffer.size();
    buffer.insert(buffer.end(), row_size, 0);
    uint8_t* row_data = buffer.data() + offset;

    uint32_t bitmap_size = (table->column_count + 7) / 8;
    uint32_t pos = 4 + bitmap_size;

    memcpy(row_data, &row_size, 4);
    // memset(row_data + 4, 0, bitmap_size); // already 0 due to insert

    for (int i = 0; i < table->column_count; i++) {
        if (values[i].is_null) {
            int byte_idx = i / 8;
            int bit_idx = i % 8;
            row_data[4 + byte_idx] |= (1 << bit_idx);
        } else {
            if (values[i].type == TYPE_INT) {
                memcpy(row_data + pos, &values[i].int_val, 8);
                pos += 8;
            } else if (values[i].type == TYPE_DECIMAL) {
                memcpy(row_data + pos, &values[i].decimal_val, 8);
                pos += 8;
            } else if (values[i].type == TYPE_STRING) {
                uint32_t len = (*string_lengths)[i];
                memcpy(row_data + pos, &len, 4);
                pos += 4;
                memcpy(row_data + pos, (*string_ptrs)[i], len);
                pos += len;
            }
        }
    }
}

void flush_table(Table* table) {
    if (!table || !table->file || table->write_buffer.empty()) {
        return;
    }

    size_t written = fwrite(table->write_buffer.data(), 1, table->write_buffer.size(), table->file);
    if (written != table->write_buffer.size()) {
        fprintf(stderr, "ERROR: Failed to write all data\n");
    }
    fflush(table->file);
    table->write_buffer.clear();
}

uint64_t insert_row(Table* table, const std::vector<Value>& values) {
    if (!table || !table->file) {
        return 0;
    }

    if (!validate_values(table, values)) {
        return 0;
    }

    std::vector<const char*>& string_ptrs = table->insert_string_ptrs;
    std::vector<uint32_t>& string_lengths = table->insert_string_lengths;

    string_ptrs.clear();
    string_lengths.clear();

    for (int i = 0; i < (int)values.size(); i++) {
        if (values[i].type == TYPE_STRING && !values[i].is_null) {
            string_ptrs.push_back(values[i].string_val.c_str());
            string_lengths.push_back((uint32_t)values[i].string_val.length());
        } else {
            string_ptrs.push_back(nullptr);
            string_lengths.push_back(0);
        }
    }

    uint64_t offset = table->file_size;
    uint32_t row_size = calculate_row_size_fast(table, values, &string_lengths);
    encode_row_into_buffer(table, values, table->write_buffer, row_size, &string_ptrs, &string_lengths);
    table->file_size += row_size;

    if (table->write_buffer.size() >= BATCH_SIZE_BYTES) {
        flush_table(table);
    }

    return offset;
}

uint64_t insert_rows_batch(Table* table, const std::vector<std::vector<Value>>& rows) {
    if (!table || !table->file || rows.empty()) {
        return 0;
    }

    // Precompute total batch size using single loop
    uint32_t total_size = 0;
    std::vector<uint32_t> row_sizes(rows.size());
    
    uint32_t bitmap_size = (table->column_count + 7) / 8;
    uint32_t fixed_part = 4 + bitmap_size;
    int cols = table->column_count;

    for (size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        uint32_t size = fixed_part;
        for (int i = 0; i < cols; ++i) {
            if (!row[i].is_null) {
                if (table->columns[i].type_id == TYPE_STRING) {
                    size += 4 + row[i].string_val.length();
                } else {
                    size += 8;
                }
            }
        }
        row_sizes[r] = size;
        total_size += size;
    }

    // Reserve buffer once
    size_t start_offset = table->write_buffer.size();
    table->write_buffer.insert(table->write_buffer.end(), total_size, 0);
    uint8_t* buffer_ptr = table->write_buffer.data();
    
    size_t current_pos = start_offset;

    // Encode all rows
    for (size_t r = 0; r < rows.size(); r++) {
        const auto& row = rows[r];
        uint32_t row_size = row_sizes[r];
        
        uint8_t* row_data = buffer_ptr + current_pos;
        uint32_t data_pos = 4 + bitmap_size;

        memcpy(row_data, &row_size, 4);

        for (int i = 0; i < cols; i++) {
            if (row[i].is_null) {
                row_data[4 + (i / 8)] |= (1 << (i % 8));
            } else {
                uint8_t expected_type = table->columns[i].type_id;
                if (expected_type == TYPE_INT) {
                    int64_t out_val = (row[i].type == TYPE_DECIMAL)
                        ? (int64_t)row[i].decimal_val
                        : row[i].int_val;
                    memcpy(row_data + data_pos, &out_val, 8);
                    data_pos += 8;
                } else if (expected_type == TYPE_DECIMAL) {
                    double out_val = (row[i].type == TYPE_INT)
                        ? (double)row[i].int_val
                        : row[i].decimal_val;
                    memcpy(row_data + data_pos, &out_val, 8);
                    data_pos += 8;
                } else if (expected_type == TYPE_STRING) {
                    uint32_t len = (uint32_t)row[i].string_val.length();
                    memcpy(row_data + data_pos, &len, 4);
                    data_pos += 4;
                    memcpy(row_data + data_pos, row[i].string_val.c_str(), len);
                    data_pos += len;
                }
            }
        }
        current_pos += row_size;
    }

    uint64_t file_start_offset = table->file_size;
    table->file_size += total_size;

    if (table->write_buffer.size() >= BATCH_SIZE_BYTES) {
        flush_table(table);
    }

    return file_start_offset;
}

RowBuffer* read_row(Table* table, uint64_t offset) {
    if (!table || !table->file || offset >= table->file_size) {
        return nullptr;
    }

    if (fseek(table->file, (off_t)offset, SEEK_SET) != 0) {
        return nullptr;
    }

    uint32_t row_size;
    if (fread(&row_size, 4, 1, table->file) != 1) {
        return nullptr;
    }

    if (row_size < 6 || row_size > 100 * 1024 * 1024) {
        return nullptr;
    }

    if (fseek(table->file, (off_t)offset, SEEK_SET) != 0) {
        return nullptr;
    }

    RowBuffer* buffer = new RowBuffer();
    buffer->data.resize(row_size);
    if (fread(buffer->data.data(), 1, row_size, table->file) != row_size) {
        delete buffer;
        return nullptr;
    }

    return buffer;
}

void scan_table(Table* table, bool (*callback)(RowBuffer*, void*), void* arg) {
    if (!table || !table->file) {
        return;
    }

    fseek(table->file, 0, SEEK_SET);
    uint64_t offset = 0;

    while (offset < table->file_size) {
        RowBuffer* buffer = read_row(table, offset);
        if (!buffer) break;

        bool should_continue = callback(buffer, arg);
        offset += *(uint32_t*)buffer->data.data();
        delete buffer;

        if (!should_continue) break;  // Early termination
    }
}

void scan_table_with_offset(Table* table, uint64_t start_offset,
                            bool (*callback)(RowBuffer*, void*), void* arg) {
    if (!table || !table->file) {
        return;
    }

    uint64_t offset = start_offset;

    while (offset < table->file_size) {
        RowBuffer* buffer = read_row(table, offset);
        if (!buffer) break;

        bool should_continue = callback(buffer, arg);
        offset += *(uint32_t*)buffer->data.data();
        delete buffer;

        if (!should_continue) break;  // Early termination
    }
}

void delete_row(Table* table, uint64_t offset) {
    // Mark row as deleted (simplified - full implementation would handle deletion)
}

void update_row(Table* table, uint64_t offset, const std::vector<Value>& values) {
    // Update row at offset (simplified - full implementation would handle updates)
}

} // namespace storage
} // namespace flexql
