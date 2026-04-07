#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "storage/storage.hpp"
#include "index/index.hpp"

namespace flexql {
namespace executor {

typedef void (*RowCallback)(char** values, int col_count, const char** col_names, void* context);

// WHERE clause
struct WhereClause {
    bool active;
    std::string column;
    std::string op;
    storage::Value value;
};

// SELECT query
struct SelectQuery {
    std::string table_name;
    std::vector<std::string> columns;
    WhereClause where;
    int limit;
    storage::Table* table;
    index::Index* index;
};

// INNER JOIN query
struct JoinQuery {
    std::string left_table_name;
    std::string right_table_name;
    std::string left_join_column;
    std::string right_join_column;
    std::vector<std::string> columns;
    WhereClause where;
    std::string where_table_name;
    int limit;
    storage::Table* left_table;
    storage::Table* right_table;
};

// INSERT query - supports multi-row
struct InsertQuery {
    std::string table_name;
    std::vector<std::vector<storage::Value>> rows;  // Multiple rows
    storage::Table* table;
};

// DELETE query
struct DeleteQuery {
    std::string table_name;
    WhereClause where;
    storage::Table* table;
    index::Index* index;
};

// UPDATE query
struct UpdateQuery {
    std::string table_name;
    std::vector<std::pair<std::string, storage::Value>> updates;
    WhereClause where;
    storage::Table* table;
    index::Index* index;
};

// CREATE TABLE query
struct CreateTableQuery {
    std::string table_name;
    std::vector<std::string> column_names;
    std::vector<int> column_types;
    std::vector<std::string> column_type_names;
};

// Decoded row
struct DecodedRow {
    std::vector<int64_t> int_vals;
    std::vector<double> decimal_vals;
    std::vector<const char*> string_ptrs;
    std::vector<uint32_t> string_lengths;
};

// Execution functions
int execute_select(SelectQuery* query, const std::vector<std::string>& selected_cols,
                   RowCallback callback, void* arg);
int execute_insert(InsertQuery* query);
int execute_delete(DeleteQuery* query);
int execute_update(UpdateQuery* query);
int execute_join(JoinQuery* query, RowCallback callback, void* arg);

} // namespace executor
} // namespace flexql
