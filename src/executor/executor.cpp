#include "executor/executor.hpp"
#include "storage/storage.hpp"
#include "index/index.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>

namespace flexql {
namespace executor {

using flexql::storage::Table;
using flexql::storage::RowBuffer;
using flexql::storage::Value;
using flexql::storage::insert_row;
using flexql::storage::read_row;
using flexql::storage::scan_table;
using flexql::storage::scan_table_with_offset;
using flexql::index::index_lookup_int;
using flexql::index::index_lookup_decimal;
using flexql::index::index_lookup_string;

static bool format_datetime_value(double value, char* out_buffer, size_t out_size) {
    if (!out_buffer || out_size == 0 || !std::isfinite(value)) {
        return false;
    }

    time_t epoch = (time_t)value;
    if (std::fabs(value - (double)epoch) > 1e-9) {
        return false;
    }

    std::tm tm_value = {};
    if (!localtime_r(&epoch, &tm_value)) {
        return false;
    }

    size_t written = strftime(out_buffer, out_size, "%Y-%m-%d %H:%M:%S", &tm_value);
    return written > 0;
}

// Helper to decode rows without per-row allocations
DecodedRow decode_row_internal(Table* table, RowBuffer* buffer) {
    DecodedRow decoded;

    if (buffer->data.size() < 4) {
        return decoded;
    }

    uint32_t bitmap_size = (table->column_count + 7) / 8;
    uint8_t* bitmap = buffer->data.data() + 4;
    uint8_t* data_ptr = buffer->data.data() + 4 + bitmap_size;

    decoded.int_vals.resize(table->column_count);
    decoded.decimal_vals.resize(table->column_count);
    decoded.string_ptrs.resize(table->column_count);
    decoded.string_lengths.resize(table->column_count);

    for (int i = 0; i < table->column_count; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        bool is_null = (bitmap[byte_idx] & (1 << bit_idx)) != 0;

        if (is_null) {
            decoded.string_ptrs[i] = nullptr;
            decoded.string_lengths[i] = 0;
        } else {
            if (table->columns[i].type_id == TYPE_INT) {
                decoded.int_vals[i] = *(int64_t*)data_ptr;
                data_ptr += 8;
            } else if (table->columns[i].type_id == TYPE_DECIMAL) {
                decoded.decimal_vals[i] = *(double*)data_ptr;
                data_ptr += 8;
            } else if (table->columns[i].type_id == TYPE_STRING) {
                uint32_t str_len = *(uint32_t*)data_ptr;
                data_ptr += 4;
                decoded.string_ptrs[i] = (const char*)data_ptr;
                decoded.string_lengths[i] = str_len;
                data_ptr += str_len;
            }
        }
    }

    return decoded;
}

struct ScanContext {
    SelectQuery* query;
    const std::vector<std::string>* selected_cols;
    std::vector<int> selected_col_indices;
    std::vector<int> selected_col_types;
    RowCallback callback;
    void* user_context;
    std::vector<std::array<char, 32>> numeric_buffers;
    std::vector<std::string> string_buffers;
    std::vector<char*> output_ptrs;
    std::vector<const char*> col_names;
    int where_col_idx;
    int limit_remaining;
};

extern bool scan_callback(RowBuffer* buffer, void* arg) {
    ScanContext* ctx = (ScanContext*)arg;

    if (ctx->limit_remaining <= 0) {
        return false;  // Stop scan
    }

    DecodedRow decoded = decode_row_internal(ctx->query->table, buffer);

    // Decode bitmap for NULL checking
    uint8_t* bitmap = buffer->data.data() + 4;

    // Check if row matches WHERE clause
    if (ctx->query->where.active) {
        int where_col_idx = ctx->where_col_idx;

        if (where_col_idx >= 0) {
            // Check if WHERE column is NULL using bitmap
            int byte_idx = where_col_idx / 8;
            int bit_idx = where_col_idx % 8;
            bool is_null = (bitmap[byte_idx] & (1 << bit_idx)) != 0;

            if (is_null) {
                return true;  // NULL doesn't match anything, continue scan
            }

            bool matches = false;
            int col_type = ctx->query->table->columns[where_col_idx].type_id;

            // Support comparison operators: =, <, >, <=, >=
            if (col_type == TYPE_INT) {
                int64_t col_val = decoded.int_vals[where_col_idx];
                int64_t cmp_val = ctx->query->where.value.int_val;
                if (ctx->query->where.op == "=") {
                    matches = (col_val == cmp_val);
                } else if (ctx->query->where.op == "!=") {
                    matches = (col_val != cmp_val);
                } else if (ctx->query->where.op == "<") {
                    matches = (col_val < cmp_val);
                } else if (ctx->query->where.op == ">") {
                    matches = (col_val > cmp_val);
                } else if (ctx->query->where.op == "<=") {
                    matches = (col_val <= cmp_val);
                } else if (ctx->query->where.op == ">=") {
                    matches = (col_val >= cmp_val);
                }
            } else if (col_type == TYPE_DECIMAL) {
                double col_val = decoded.decimal_vals[where_col_idx];
                double cmp_val = ctx->query->where.value.decimal_val;
                if (ctx->query->where.op == "=") {
                    matches = (col_val == cmp_val);
                } else if (ctx->query->where.op == "!=") {
                    matches = (col_val != cmp_val);
                } else if (ctx->query->where.op == "<") {
                    matches = (col_val < cmp_val);
                } else if (ctx->query->where.op == ">") {
                    matches = (col_val > cmp_val);
                } else if (ctx->query->where.op == "<=") {
                    matches = (col_val <= cmp_val);
                } else if (ctx->query->where.op == ">=") {
                    matches = (col_val >= cmp_val);
                }
            } else if (col_type == TYPE_STRING) {
                std::string col_val(decoded.string_ptrs[where_col_idx], decoded.string_lengths[where_col_idx]);
                const std::string& cmp_val = ctx->query->where.value.string_val;
                if (ctx->query->where.op == "=") {
                    matches = (col_val == cmp_val);
                } else if (ctx->query->where.op == "!=") {
                    matches = (col_val != cmp_val);
                } else if (ctx->query->where.op == "<") {
                    matches = (col_val < cmp_val);
                } else if (ctx->query->where.op == ">") {
                    matches = (col_val > cmp_val);
                } else if (ctx->query->where.op == "<=") {
                    matches = (col_val <= cmp_val);
                } else if (ctx->query->where.op == ">=") {
                    matches = (col_val >= cmp_val);
                }
            }

            if (!matches) {
                return true;  // Skip rows that don't match WHERE, continue scan
            }
        }
    }

    // Build output for callback using pre-allocated buffers
    for (size_t i = 0; i < ctx->selected_cols->size(); i++) {
        int col_idx = ctx->selected_col_indices[i];

        if (col_idx >= 0) {
            // Check if column is NULL
            int byte_idx = col_idx / 8;
            int bit_idx = col_idx % 8;
            bool is_null = (bitmap[byte_idx] & (1 << bit_idx)) != 0;

            if (is_null) {
                ctx->output_ptrs[i] = nullptr;
            } else if (ctx->selected_col_types[i] == TYPE_INT) {
                snprintf(ctx->numeric_buffers[i].data(), 32, "%lld", (long long)decoded.int_vals[col_idx]);
                ctx->output_ptrs[i] = ctx->numeric_buffers[i].data();
            } else if (ctx->selected_col_types[i] == TYPE_DECIMAL) {
                double val = decoded.decimal_vals[col_idx];
                if (strcmp(ctx->query->table->columns[col_idx].type, "DATETIME") == 0 &&
                    format_datetime_value(val, ctx->numeric_buffers[i].data(), 32)) {
                    ctx->output_ptrs[i] = ctx->numeric_buffers[i].data();
                } else {
                    // Check if value is a whole number to avoid unnecessary decimals
                    if (val == (long long)val && val >= -1e15 && val <= 1e15) {
                        snprintf(ctx->numeric_buffers[i].data(), 32, "%lld", (long long)val);
                    } else {
                        snprintf(ctx->numeric_buffers[i].data(), 32, "%g", val);
                    }
                    ctx->output_ptrs[i] = ctx->numeric_buffers[i].data();
                }
            } else {
                // STRING: ensure null-terminated output for callback consumers
                ctx->string_buffers[i].assign(decoded.string_ptrs[col_idx], decoded.string_lengths[col_idx]);
                ctx->output_ptrs[i] = (char*)ctx->string_buffers[i].c_str();
            }
        } else {
            ctx->output_ptrs[i] = nullptr;
        }
    }

    ctx->callback((char**)ctx->output_ptrs.data(), ctx->selected_cols->size(),
                  ctx->col_names.data(), ctx->user_context);

    ctx->limit_remaining--;

    // Return true if we should continue, false if limit reached
    return ctx->limit_remaining > 0;
}

int execute_select(SelectQuery* query, const std::vector<std::string>& selected_cols,
                   RowCallback callback, void* arg) {
    if (!query || !query->table) {
        return -1;
    }

    // Flush table before SELECT to ensure we read current data
    flush_table(query->table);

    ScanContext ctx = {};
    ctx.query = query;
    ctx.selected_cols = &selected_cols;
    ctx.callback = callback;
    ctx.user_context = arg;
    ctx.numeric_buffers.resize(selected_cols.size());
    ctx.string_buffers.resize(selected_cols.size());
    ctx.output_ptrs.resize(selected_cols.size());
    ctx.col_names.resize(selected_cols.size());
    ctx.selected_col_indices.resize(selected_cols.size(), -1);
    ctx.selected_col_types.resize(selected_cols.size(), -1);
    ctx.where_col_idx = -1;

    // Pre-populate column names and resolve selected column indices once
    for (size_t i = 0; i < selected_cols.size(); i++) {
        ctx.col_names[i] = selected_cols[i].c_str();
        for (int j = 0; j < query->table->column_count; j++) {
            if (query->table->columns[j].name == selected_cols[i]) {
                ctx.selected_col_indices[i] = j;
                ctx.selected_col_types[i] = query->table->columns[j].type_id;
                break;
            }
        }
    }

    if (query->where.active) {
        for (int j = 0; j < query->table->column_count; j++) {
            if (query->table->columns[j].name == query->where.column) {
                ctx.where_col_idx = j;
                break;
            }
        }
    }

    ctx.limit_remaining = (query->limit > 0) ? query->limit : 0x7FFFFFFF;

    // Use index if available for WHERE clause
    if (query->where.active && query->index) {
        uint64_t offset = 0;
        if (query->where.value.type == TYPE_INT) {
            offset = index_lookup_int(query->index, query->where.value.int_val);
        } else if (query->where.value.type == TYPE_DECIMAL) {
            offset = index_lookup_decimal(query->index, query->where.value.decimal_val);
        } else if (query->where.value.type == TYPE_STRING) {
            offset = index_lookup_string(query->index, query->where.value.string_val.c_str());
        }

        if (offset > 0) {
            scan_table_with_offset(query->table, offset, scan_callback, &ctx);
        }
    } else {
        scan_table(query->table, scan_callback, &ctx);
    }

    return 0;
}

static int find_column_index(Table* table, const std::string& column_name) {
    for (int i = 0; i < table->column_count; i++) {
        if (table->columns[i].name == column_name) {
            return i;
        }
    }
    return -1;
}

static bool build_key(Table* table, RowBuffer* buffer, int col_idx, std::string& out_key,
                      const DecodedRow& decoded) {
    if (!table || !buffer || col_idx < 0 || col_idx >= table->column_count) {
        return false;
    }

    uint8_t* bitmap = buffer->data.data() + 4;
    int byte_idx = col_idx / 8;
    int bit_idx = col_idx % 8;
    bool is_null = (bitmap[byte_idx] & (1 << bit_idx)) != 0;
    if (is_null) {
        return false;
    }

    int col_type = table->columns[col_idx].type_id;
    if (col_type == TYPE_INT) {
        out_key = std::to_string(decoded.int_vals[col_idx]);
        return true;
    }
    if (col_type == TYPE_DECIMAL) {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss << std::setprecision(17) << decoded.decimal_vals[col_idx];
        out_key = oss.str();
        return true;
    }
    if (col_type == TYPE_STRING) {
        out_key.assign(decoded.string_ptrs[col_idx], decoded.string_lengths[col_idx]);
        return true;
    }

    return false;
}

static bool where_matches(Table* table, RowBuffer* buffer, const DecodedRow& decoded,
                          int where_col_idx, const WhereClause& where) {
    if (!where.active || where_col_idx < 0) {
        return true;
    }

    uint8_t* bitmap = buffer->data.data() + 4;
    int byte_idx = where_col_idx / 8;
    int bit_idx = where_col_idx % 8;
    bool is_null = (bitmap[byte_idx] & (1 << bit_idx)) != 0;
    if (is_null) {
        return false;
    }

    int col_type = table->columns[where_col_idx].type_id;
    if (col_type == TYPE_INT) {
        int64_t col_val = decoded.int_vals[where_col_idx];
        int64_t cmp_val = where.value.int_val;
        if (where.op == "=") return col_val == cmp_val;
        if (where.op == "!=") return col_val != cmp_val;
        if (where.op == "<") return col_val < cmp_val;
        if (where.op == ">") return col_val > cmp_val;
        if (where.op == "<=") return col_val <= cmp_val;
        if (where.op == ">=") return col_val >= cmp_val;
        return false;
    }

    if (col_type == TYPE_DECIMAL) {
        double col_val = decoded.decimal_vals[where_col_idx];
        double cmp_val = where.value.decimal_val;
        if (where.op == "=") return col_val == cmp_val;
        if (where.op == "!=") return col_val != cmp_val;
        if (where.op == "<") return col_val < cmp_val;
        if (where.op == ">") return col_val > cmp_val;
        if (where.op == "<=") return col_val <= cmp_val;
        if (where.op == ">=") return col_val >= cmp_val;
        return false;
    }

    if (col_type == TYPE_STRING) {
        std::string col_val(decoded.string_ptrs[where_col_idx], decoded.string_lengths[where_col_idx]);
        const std::string& cmp_val = where.value.string_val;
        if (where.op == "=") return col_val == cmp_val;
        if (where.op == "!=") return col_val != cmp_val;
        if (where.op == "<") return col_val < cmp_val;
        if (where.op == ">") return col_val > cmp_val;
        if (where.op == "<=") return col_val <= cmp_val;
        if (where.op == ">=") return col_val >= cmp_val;
        return false;
    }

    return false;
}

int execute_insert(InsertQuery* query) {
    if (!query || !query->table) {
        return -1;
    }

    if (query->rows.empty()) {
        return 0;
    }

    int total_inserted = 0;

    for (const auto& row : query->rows) {
        if (row.empty()) {
            continue;
        }

        insert_row(query->table, const_cast<std::vector<Value>&>(row));
        total_inserted++;
    }

    // Flush after INSERT to ensure data persists
    flush_table(query->table);

    return total_inserted;
}

int execute_delete(DeleteQuery* query) {
    if (!query || !query->table) {
        return -1;
    }

    return 0;  // Simplified
}

int execute_update(UpdateQuery* query) {
    if (!query || !query->table) {
        return -1;
    }

    return 0;  // Simplified
}

int execute_join(JoinQuery* query, RowCallback callback, void* arg) {
    if (!query || !query->left_table || !query->right_table) {
        return -1;
    }

    if (query->columns.empty()) {
        return -1;
    }

    flush_table(query->left_table);
    flush_table(query->right_table);

    int left_join_idx = find_column_index(query->left_table, query->left_join_column);
    int right_join_idx = find_column_index(query->right_table, query->right_join_column);
    if (left_join_idx < 0 || right_join_idx < 0) {
        return -1;
    }

    int where_left_idx = -1;
    int where_right_idx = -1;
    if (query->where.active) {
        if (!query->where_table_name.empty()) {
            if (query->where_table_name == query->left_table_name) {
                where_left_idx = find_column_index(query->left_table, query->where.column);
            } else if (query->where_table_name == query->right_table_name) {
                where_right_idx = find_column_index(query->right_table, query->where.column);
            } else {
                return -1;
            }
        } else {
            where_left_idx = find_column_index(query->left_table, query->where.column);
            if (where_left_idx < 0) {
                where_right_idx = find_column_index(query->right_table, query->where.column);
            }
        }

        if (where_left_idx < 0 && where_right_idx < 0) {
            return -1;
        }
    }

    struct SelectedColumn {
        Table* table;
        int index;
        std::string name;
    };

    std::vector<SelectedColumn> selected;
    selected.reserve(query->columns.size());

    for (const auto& raw_col : query->columns) {
        std::string col = raw_col;
        size_t dot_pos = col.find_last_of('.');
        std::string table_prefix;
        if (dot_pos != std::string::npos && dot_pos + 1 < col.size()) {
            table_prefix = col.substr(0, dot_pos);
            col = col.substr(dot_pos + 1);
        }

        if (!table_prefix.empty()) {
            if (table_prefix == query->left_table_name) {
                int idx = find_column_index(query->left_table, col);
                if (idx < 0) return -1;
                selected.push_back({query->left_table, idx, col});
                continue;
            }
            if (table_prefix == query->right_table_name) {
                int idx = find_column_index(query->right_table, col);
                if (idx < 0) return -1;
                selected.push_back({query->right_table, idx, col});
                continue;
            }
            return -1;
        }

        int left_idx = find_column_index(query->left_table, col);
        int right_idx = find_column_index(query->right_table, col);
        if (left_idx >= 0 && right_idx >= 0) {
            return -1;
        }
        if (left_idx >= 0) {
            selected.push_back({query->left_table, left_idx, col});
        } else if (right_idx >= 0) {
            selected.push_back({query->right_table, right_idx, col});
        } else {
            return -1;
        }
    }

    std::unordered_map<std::string, std::vector<uint64_t>> right_index;
    uint64_t right_offset = 0;
    while (right_offset < query->right_table->file_size) {
        RowBuffer* right_buffer = read_row(query->right_table, right_offset);
        if (!right_buffer) break;

        DecodedRow right_decoded = decode_row_internal(query->right_table, right_buffer);
        std::string key;
        if (build_key(query->right_table, right_buffer, right_join_idx, key, right_decoded)) {
            right_index[key].push_back(right_offset);
        }

        right_offset += *(uint32_t*)right_buffer->data.data();
        delete right_buffer;
    }

    std::vector<std::array<char, 32>> numeric_buffers(selected.size());
    std::vector<char*> output_ptrs(selected.size());
    std::vector<const char*> col_names(selected.size());
    for (size_t i = 0; i < selected.size(); i++) {
        col_names[i] = selected[i].name.c_str();
    }

    int limit_remaining = (query->limit > 0) ? query->limit : 0x7FFFFFFF;
    uint64_t left_offset = 0;
    while (left_offset < query->left_table->file_size && limit_remaining > 0) {
        RowBuffer* left_buffer = read_row(query->left_table, left_offset);
        if (!left_buffer) break;

        DecodedRow left_decoded = decode_row_internal(query->left_table, left_buffer);
        std::string left_key;
        if (!build_key(query->left_table, left_buffer, left_join_idx, left_key, left_decoded)) {
            left_offset += *(uint32_t*)left_buffer->data.data();
            delete left_buffer;
            continue;
        }

        auto it = right_index.find(left_key);
        if (it != right_index.end()) {
            for (uint64_t match_offset : it->second) {
                if (limit_remaining <= 0) break;
                RowBuffer* right_buffer = read_row(query->right_table, match_offset);
                if (!right_buffer) continue;

                DecodedRow right_decoded = decode_row_internal(query->right_table, right_buffer);

                if (query->where.active) {
                    bool matches = true;
                    if (where_left_idx >= 0) {
                        matches = where_matches(query->left_table, left_buffer, left_decoded,
                                                where_left_idx, query->where);
                    } else if (where_right_idx >= 0) {
                        matches = where_matches(query->right_table, right_buffer, right_decoded,
                                                where_right_idx, query->where);
                    }
                    if (!matches) {
                        delete right_buffer;
                        continue;
                    }
                }

                for (size_t i = 0; i < selected.size(); i++) {
                    Table* table = selected[i].table;
                    int col_idx = selected[i].index;
                    RowBuffer* buffer = (table == query->left_table) ? left_buffer : right_buffer;
                    DecodedRow& decoded = (table == query->left_table) ? left_decoded : right_decoded;

                    uint8_t* bitmap = buffer->data.data() + 4;
                    int byte_idx = col_idx / 8;
                    int bit_idx = col_idx % 8;
                    bool is_null = (bitmap[byte_idx] & (1 << bit_idx)) != 0;
                    if (is_null) {
                        output_ptrs[i] = nullptr;
                    } else if (table->columns[col_idx].type_id == TYPE_INT) {
                        snprintf(numeric_buffers[i].data(), 32, "%lld", (long long)decoded.int_vals[col_idx]);
                        output_ptrs[i] = numeric_buffers[i].data();
                    } else if (table->columns[col_idx].type_id == TYPE_DECIMAL) {
                        double val = decoded.decimal_vals[col_idx];
                        if (val == (long long)val && val >= -1e15 && val <= 1e15) {
                            snprintf(numeric_buffers[i].data(), 32, "%lld", (long long)val);
                        } else {
                            snprintf(numeric_buffers[i].data(), 32, "%g", val);
                        }
                        output_ptrs[i] = numeric_buffers[i].data();
                    } else {
                        output_ptrs[i] = (char*)decoded.string_ptrs[col_idx];
                    }
                }

                callback((char**)output_ptrs.data(), selected.size(), col_names.data(), arg);
                limit_remaining--;
                delete right_buffer;
            }
        }

        left_offset += *(uint32_t*)left_buffer->data.data();
        delete left_buffer;
    }

    return 0;
}

} // namespace executor
} // namespace flexql
