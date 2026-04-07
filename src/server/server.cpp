#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <mutex>
#include <unordered_map>
#include <list>

#include "storage/storage.hpp"
#include "index/index.hpp"
#include "parser/parser.hpp"
#include "executor/executor.hpp"

using namespace flexql;

static bool parse_datetime_literal(const std::string& input, double& out_epoch_seconds) {
    int year = 0, month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;

    int matched = 0;
    if (input.find('T') != std::string::npos) {
        matched = sscanf(input.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    } else if (input.find(' ') != std::string::npos) {
        matched = sscanf(input.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    } else {
        matched = sscanf(input.c_str(), "%d-%d-%d", &year, &month, &day);
        hour = 0;
        minute = 0;
        second = 0;
    }

    if (!(matched == 3 || matched == 6)) {
        return false;
    }

    if (month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }

    std::tm time_info = {};
    time_info.tm_year = year - 1900;
    time_info.tm_mon = month - 1;
    time_info.tm_mday = day;
    time_info.tm_hour = hour;
    time_info.tm_min = minute;
    time_info.tm_sec = second;
    time_info.tm_isdst = -1;

    time_t epoch = mktime(&time_info);
    if (epoch == (time_t)-1) {
        return false;
    }

    std::tm check_info = {};
    localtime_r(&epoch, &check_info);
    if (check_info.tm_year != time_info.tm_year ||
        check_info.tm_mon != time_info.tm_mon ||
        check_info.tm_mday != time_info.tm_mday ||
        check_info.tm_hour != time_info.tm_hour ||
        check_info.tm_min != time_info.tm_min ||
        check_info.tm_sec != time_info.tm_sec) {
        return false;
    }

    out_epoch_seconds = static_cast<double>(epoch);
    return true;
}

static bool parse_numeric_or_datetime_literal(const std::string& input, double& out_value) {
    char* end_ptr = nullptr;
    double numeric = strtod(input.c_str(), &end_ptr);
    if (end_ptr && *end_ptr == '\0') {
        out_value = numeric;
        return true;
    }

    return parse_datetime_literal(input, out_value);
}

static bool coerce_insert_row_types(storage::Table* table,
                                    std::vector<storage::Value>& row,
                                    std::string& error_message) {
    if (!table) {
        error_message = "ERROR: Table not found";
        return false;
    }

    if ((int)row.size() != table->column_count) {
        error_message = "ERROR: Column count mismatch";
        return false;
    }

    for (int i = 0; i < table->column_count; ++i) {
        storage::Value& value = row[i];
        if (value.is_null) {
            continue;
        }

        int expected_type = table->columns[i].type_id;
        if (expected_type == TYPE_STRING) {
            if (value.type != TYPE_STRING) {
                error_message = "ERROR: Type mismatch for column '" + std::string(table->columns[i].name) + "'";
                return false;
            }
            continue;
        }

        if (expected_type == TYPE_INT) {
            if (value.type == TYPE_DECIMAL) {
                value.int_val = (int64_t)value.decimal_val;
                value.type = TYPE_INT;
                continue;
            }
            if (value.type == TYPE_STRING) {
                double parsed = 0;
                if (!parse_numeric_or_datetime_literal(value.string_val, parsed)) {
                    error_message = "ERROR: Invalid numeric/datetime literal for column '" + std::string(table->columns[i].name) + "'";
                    return false;
                }
                value.decimal_val = parsed;
                value.int_val = (int64_t)parsed;
                value.type = TYPE_INT;
                value.string_val.clear();
                continue;
            }

            error_message = "ERROR: Type mismatch for column '" + std::string(table->columns[i].name) + "'";
            return false;
        }

        if (expected_type == TYPE_DECIMAL) {
            if (value.type == TYPE_DECIMAL) {
                value.int_val = (int64_t)value.decimal_val;
                continue;
            }
            if (value.type == TYPE_INT) {
                value.decimal_val = (double)value.int_val;
                value.type = TYPE_DECIMAL;
                continue;
            }
            if (value.type == TYPE_STRING) {
                double parsed = 0;
                if (!parse_numeric_or_datetime_literal(value.string_val, parsed)) {
                    error_message = "ERROR: Invalid numeric/datetime literal for column '" + std::string(table->columns[i].name) + "'";
                    return false;
                }
                value.decimal_val = parsed;
                value.int_val = (int64_t)parsed;
                value.type = TYPE_DECIMAL;
                value.string_val.clear();
                continue;
            }

            error_message = "ERROR: Type mismatch for column '" + std::string(table->columns[i].name) + "'";
            return false;
        }
    }

    return true;
}

class QueryCache {
private:
    size_t max_size;
    std::list<std::string> lru_list;
    std::unordered_map<std::string, std::pair<std::string, std::list<std::string>::iterator>> cache_map;
    std::mutex mtx;

public:
    QueryCache(size_t size = 100) : max_size(size) {}

    bool get(const std::string& query, std::string& result) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache_map.find(query);
        if (it == cache_map.end()) {
            return false;
        }
        lru_list.erase(it->second.second);
        lru_list.push_front(query);
        it->second.second = lru_list.begin();
        result = it->second.first;
        return true;
    }

    void put(const std::string& query, const std::string& result) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = cache_map.find(query);
        if (it != cache_map.end()) {
            lru_list.erase(it->second.second);
            cache_map.erase(it);
        }

        if (cache_map.size() >= max_size) {
            std::string last = lru_list.back();
            lru_list.pop_back();
            cache_map.erase(last);
        }

        lru_list.push_front(query);
        cache_map[query] = {result, lru_list.begin()};
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        lru_list.clear();
        cache_map.clear();
    }
};

static QueryCache g_query_cache(1000);

// Global server state
static std::vector<storage::Table*> g_tables;
static std::vector<std::string> g_table_names;
static std::vector<index::Index*> g_indices;
static std::vector<std::string> g_index_names;
static std::vector<std::string> g_index_table_names;
static std::vector<std::string> g_index_column_names;
static std::mutex g_tables_mutex;

storage::Table* get_table(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_tables_mutex);
    for (size_t i = 0; i < g_table_names.size(); i++) {
        if (g_table_names[i] == name) {
            return g_tables[i];
        }
    }
    return nullptr;
}

index::Index* get_index(const std::string& table_name, const std::string& column_name) {
    for (size_t i = 0; i < g_index_names.size(); i++) {
        if (g_index_table_names[i] == table_name && g_index_column_names[i] == column_name) {
            return g_indices[i];
        }
    }
    return nullptr;
}

// Stream context for SELECT
struct SelectStreamContext {
    int socket_fd;
    bool first_row;
    std::string* cache_buffer;
};

void select_stream_callback(char** values, int col_count, const char** col_names, void* context) {
    SelectStreamContext* ctx = (SelectStreamContext*)context;

    if (ctx->first_row) {
        std::string header;
        for (int i = 0; i < col_count; i++) {
            if (i > 0) header += "|";
            header += col_names[i];
        }
        header += "\n";
        send(ctx->socket_fd, header.c_str(), header.length(), 0);
        if (ctx->cache_buffer) {
            *(ctx->cache_buffer) += header;
        }
        ctx->first_row = false;
    }

    std::string row;
    for (int i = 0; i < col_count; i++) {
        if (i > 0) row += "|";
        row += (values[i] ? values[i] : "NULL");
    }
    row += "\n";
    send(ctx->socket_fd, row.c_str(), row.length(), 0);
    if (ctx->cache_buffer) {
        *(ctx->cache_buffer) += row;
    }
}

void count_rows_callback(char** values, int col_count, const char** col_names, void* context) {
    (void)values;
    (void)col_count;
    (void)col_names;
    (*(int*)context)++;
}

std::string handle_select_streaming(int client_socket, const std::string& query) {
    std::string cached_result;
    if (g_query_cache.get(query, cached_result)) {
        if (!cached_result.empty()) {
            send(client_socket, cached_result.c_str(), cached_result.length(), 0);
        }
        return "OK";
    }
    
    std::string cache_buffer;
    std::string upper_query = query;
    for (auto& c : upper_query) c = toupper(c);

    if (upper_query.find(" JOIN") != std::string::npos) {
        executor::JoinQuery parsed;
        if (!parser::parse_join_query(query, parsed)) {
            return "ERROR: Failed to parse JOIN";
        }

        storage::Table* left_table = get_table(parsed.left_table_name);
        storage::Table* right_table = get_table(parsed.right_table_name);
        if (!left_table || !right_table) {
            return "ERROR: Table not found";
        }

        auto find_column = [](storage::Table* table, const std::string& name) {
            for (int i = 0; i < table->column_count; i++) {
                if (name == table->columns[i].name) {
                    return true;
                }
            }
            return false;
        };

        if (!find_column(left_table, parsed.left_join_column) ||
            !find_column(right_table, parsed.right_join_column)) {
            return "ERROR: Unknown column";
        }

        if (parsed.columns.size() == 1 && parsed.columns[0] == "*") {
            parsed.columns.clear();
            for (int i = 0; i < left_table->column_count; i++) {
                parsed.columns.push_back(parsed.left_table_name + "." + left_table->columns[i].name);
            }
            for (int i = 0; i < right_table->column_count; i++) {
                parsed.columns.push_back(parsed.right_table_name + "." + right_table->columns[i].name);
            }
        }

        if (parsed.where.active) {
            if (parsed.where_table_name == parsed.left_table_name) {
                if (!find_column(left_table, parsed.where.column)) {
                    return "ERROR: Unknown column";
                }
            } else if (parsed.where_table_name == parsed.right_table_name) {
                if (!find_column(right_table, parsed.where.column)) {
                    return "ERROR: Unknown column";
                }
            } else if (!parsed.where_table_name.empty()) {
                return "ERROR: Unknown column";
            } else if (!find_column(left_table, parsed.where.column) &&
                       !find_column(right_table, parsed.where.column)) {
                return "ERROR: Unknown column";
            }
        }

        for (auto& col : parsed.columns) {
            size_t dot_pos = col.find_last_of('.');
            if (dot_pos != std::string::npos && dot_pos + 1 < col.size()) {
                std::string table_name = col.substr(0, dot_pos);
                std::string col_name = col.substr(dot_pos + 1);
                if (table_name == parsed.left_table_name) {
                    if (!find_column(left_table, col_name)) return "ERROR: Unknown column";
                } else if (table_name == parsed.right_table_name) {
                    if (!find_column(right_table, col_name)) return "ERROR: Unknown column";
                } else {
                    return "ERROR: Unknown column";
                }
            } else {
                bool in_left = find_column(left_table, col);
                bool in_right = find_column(right_table, col);
                if (!in_left && !in_right) {
                    return "ERROR: Unknown column";
                }
                if (in_left && in_right) {
                    return "ERROR: Ambiguous column";
                }
            }
        }

        parsed.left_table = left_table;
        parsed.right_table = right_table;

        std::unique_lock<std::mutex> left_lock(left_table->table_mutex, std::defer_lock);
        std::unique_lock<std::mutex> right_lock(right_table->table_mutex, std::defer_lock);
        std::lock(left_lock, right_lock);

        SelectStreamContext stream_ctx;
        stream_ctx.socket_fd = client_socket;
        stream_ctx.first_row = true;
        stream_ctx.cache_buffer = &cache_buffer;

        int rc = executor::execute_join(&parsed,
                                       reinterpret_cast<executor::RowCallback>(select_stream_callback),
                                       &stream_ctx);
        if (rc != 0) {
            return "ERROR: Failed to execute JOIN";
        }

        
        return "OK";
    }

    executor::SelectQuery parsed;
    if (!parser::parse_select_query(query, parsed)) {
        return "ERROR: Failed to parse SELECT";
    }

    storage::Table* table = get_table(parsed.table_name);
    if (!table) {
        return "ERROR: Table not found";
    }

    std::lock_guard<std::mutex> lock(table->table_mutex);

    auto strip_table_prefix = [](const std::string& col_name) {
        size_t dot_pos = col_name.find_last_of('.');
        if (dot_pos != std::string::npos && dot_pos + 1 < col_name.size()) {
            return col_name.substr(dot_pos + 1);
        }
        return col_name;
    };

    std::vector<std::string> selected_cols = parsed.columns;
    bool is_count = false;
    if (selected_cols.size() == 1) {
        std::string upper_col = selected_cols[0];
        for (auto& c : upper_col) c = toupper(c);
        upper_col.erase(std::remove_if(upper_col.begin(), upper_col.end(), ::isspace), upper_col.end());
        if (upper_col == "COUNT(*)") {
            is_count = true;
            selected_cols.clear();
        }
    }

    if (selected_cols.size() == 1 && selected_cols[0] == "*") {
        selected_cols.clear();
        for (int i = 0; i < table->column_count; i++) {
            selected_cols.push_back(table->columns[i].name);
        }
    } else if (!is_count) {
        for (auto& col : selected_cols) {
            col = strip_table_prefix(col);
            for (auto& c : col) c = toupper((unsigned char)c);
            bool found = false;
            for (int i = 0; i < table->column_count; i++) {
                if (col == table->columns[i].name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return "ERROR: Unknown column";
            }
        }
    }

    if (parsed.where.active) {
        parsed.where.column = strip_table_prefix(parsed.where.column);
        for (auto& c : parsed.where.column) c = toupper((unsigned char)c);
        bool found = false;
        int where_col_idx = -1;
        for (int i = 0; i < table->column_count; i++) {
            if (parsed.where.column == table->columns[i].name) {
                found = true;
                where_col_idx = i;
                break;
            }
        }
        if (!found) {
            return "ERROR: Unknown column";
        }

        int where_col_type = table->columns[where_col_idx].type_id;
        if (where_col_type == TYPE_DECIMAL || where_col_type == TYPE_INT) {
            if (parsed.where.value.type == TYPE_STRING) {
                double parsed_numeric = 0;
                if (!parse_numeric_or_datetime_literal(parsed.where.value.string_val, parsed_numeric)) {
                    return "ERROR: Invalid numeric/datetime literal in WHERE";
                }
                parsed.where.value.decimal_val = parsed_numeric;
                parsed.where.value.int_val = (int64_t)parsed_numeric;
                parsed.where.value.type = (where_col_type == TYPE_INT) ? TYPE_INT : TYPE_DECIMAL;
                parsed.where.value.string_val.clear();
            } else if (where_col_type == TYPE_INT && parsed.where.value.type == TYPE_DECIMAL) {
                parsed.where.value.int_val = (int64_t)parsed.where.value.decimal_val;
                parsed.where.value.type = TYPE_INT;
            }
        }
    }

    SelectStreamContext stream_ctx;
    stream_ctx.socket_fd = client_socket;
    stream_ctx.first_row = true;
    stream_ctx.cache_buffer = &cache_buffer;

    executor::SelectQuery exec_query = {};
    exec_query.table_name = parsed.table_name;
    exec_query.table = table;
    exec_query.index = nullptr;
    exec_query.columns = selected_cols;
    exec_query.where = parsed.where;
    exec_query.limit = parsed.limit;

    if (is_count) {
        int row_count = 0;
        executor::execute_select(&exec_query, selected_cols,
                               reinterpret_cast<executor::RowCallback>(count_rows_callback),
                               &row_count);
        
        std::string header = "COUNT(*)\n";
        std::string result = std::to_string(row_count) + "\n";
        send(client_socket, header.c_str(), header.length(), 0);
        send(client_socket, result.c_str(), result.length(), 0);
        cache_buffer = header + result;
    } else {
        executor::execute_select(&exec_query, selected_cols,
                               reinterpret_cast<executor::RowCallback>(select_stream_callback),
                               &stream_ctx);
    }

    g_query_cache.put(query, cache_buffer);
    return "OK";
}

#include <chrono>

std::string handle_insert(const std::string& query) {
    executor::InsertQuery parsed;
    if (!parser::parse_insert_query(query, parsed)) {
        return "ERROR: Failed to parse INSERT";
    }

    storage::Table* table = get_table(parsed.table_name);
    if (!table) {
        return "ERROR: Table not found";
    }

    std::lock_guard<std::mutex> lock(table->table_mutex);

    if (parsed.rows.empty()) {
        return "ERROR: No rows to insert";
    }

    for (size_t i = 0; i < parsed.rows.size(); i++) {
        std::string coerce_error;
        if (!coerce_insert_row_types(table, parsed.rows[i], coerce_error)) {
            return coerce_error;
        }
    }

    storage::insert_rows_batch(table, parsed.rows);
    int total_inserted = parsed.rows.size();

    g_query_cache.clear();

    return "OK: " + std::to_string(total_inserted) + " row" + (total_inserted == 1 ? "" : "s") + " inserted";
}

std::string handle_drop_table(const std::string& query) {
    std::string upper_query = query;
    for (auto& c : upper_query) c = toupper(c);

    size_t table_pos = upper_query.find("TABLE");
    if (table_pos == std::string::npos) {
        return "ERROR: Invalid DROP TABLE";
    }

    std::string table_name = query.substr(table_pos + 5);
    size_t start = table_name.find_first_not_of(" \t\r\n;");
    if (start != std::string::npos) table_name = table_name.substr(start);
    size_t end = table_name.find_last_not_of(" \t\r\n;");
    if (end != std::string::npos) table_name = table_name.substr(0, end + 1);

    bool if_exists = false;
    std::string upper_name = table_name;
    for (auto& c : upper_name) c = toupper(c);
    
    if (upper_name.find("IF EXISTS") == 0) {
        if_exists = true;
        table_name = table_name.substr(9);
        start = table_name.find_first_not_of(" \t\r\n;");
        if (start != std::string::npos) table_name = table_name.substr(start);
    }
    
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_tables_mutex);
        for (size_t i = 0; i < g_table_names.size(); i++) {
            if (g_table_names[i] == table_name) {
                // Delete table from memory
                delete g_tables[i];
                g_tables.erase(g_tables.begin() + i);
                g_table_names.erase(g_table_names.begin() + i);
                found = true;
                break;
            }
        }
    }
    
    if (!found && !if_exists) {
        return "ERROR: Table not found";
    }
    
    std::string db_path = "tables/" + table_name + ".db";
    std::string schema_path = "tables/" + table_name + ".schema";
    remove(db_path.c_str());
    remove(schema_path.c_str());
    
    g_query_cache.clear();
    return "OK";
}

std::string handle_create_table(const std::string& query) {
    executor::CreateTableQuery parsed;
    if (!parser::parse_create_table(query, parsed)) {
        return "ERROR: Failed to parse CREATE TABLE";
    }

    bool if_not_exists = false;
    std::string upper_name = parsed.table_name;
    for (auto& c : upper_name) c = toupper(c);
    if (upper_name.find("IF NOT EXISTS ") == 0) {
        if_not_exists = true;
        parsed.table_name = parsed.table_name.substr(14);
    } // Trim spaces? Usually "IF NOT EXISTS table_name" has spaces which we should trim.
    
    size_t start = parsed.table_name.find_first_not_of(" \t\r\n;");
    if (start != std::string::npos) parsed.table_name = parsed.table_name.substr(start);

    {
        std::lock_guard<std::mutex> lock(g_tables_mutex);
        for (size_t i = 0; i < g_table_names.size(); i++) {
            if (g_table_names[i] == parsed.table_name) {
                if (if_not_exists) return "OK";
                
                // Drop existing to support automated benchmark reruns
                delete g_tables[i];
                g_tables.erase(g_tables.begin() + i);
                g_table_names.erase(g_table_names.begin() + i);
                
                std::string db_path = "tables/" + parsed.table_name + ".db";
                std::string schema_path = "tables/" + parsed.table_name + ".schema";
                remove(db_path.c_str());
                remove(schema_path.c_str());
                break;
            }
        }
    }

    std::vector<storage::Column> columns;
    for (size_t i = 0; i < parsed.column_names.size(); i++) {
        storage::Column col;
        strncpy(col.name, parsed.column_names[i].c_str(), 63);
        col.name[63] = '\0';
        col.type_id = parsed.column_types[i];

        std::string logical_type;
        if (i < parsed.column_type_names.size() && !parsed.column_type_names[i].empty()) {
            logical_type = parsed.column_type_names[i];
        } else if (col.type_id == TYPE_INT) {
            logical_type = "INT";
        } else if (col.type_id == TYPE_DECIMAL) {
            logical_type = "DECIMAL";
        } else {
            logical_type = "STRING";
        }
        strncpy(col.type, logical_type.c_str(), 63);
        col.type[63] = '\0';

        columns.push_back(col);
    }

    storage::Table* table = storage::create_table(parsed.table_name, columns);
    if (!table) {
        return "ERROR: Failed to create table";
    }

    // Persist schema
    std::string schema_path = "tables/" + parsed.table_name + ".schema";
    std::ofstream schema_file(schema_path);
    for (const auto& col : columns) {
        schema_file << col.name << " " << col.type << "\n";
    }
    schema_file.close();

    {
        std::lock_guard<std::mutex> lock(g_tables_mutex);
        g_tables.push_back(table);
        g_table_names.push_back(parsed.table_name);
    }

    g_query_cache.clear();
    return "OK: Table '" + parsed.table_name + "' created";
}

std::string execute_query(const std::string& query) {
    if (query.empty()) {
        return "ERROR: Empty query";
    }

    std::string upper_query = query;
    for (auto& c : upper_query) c = toupper(c);

    if (upper_query.rfind("CREATE", 0) == 0 && upper_query.find("TABLE") != std::string::npos) {
        return handle_create_table(query);
    } else if (upper_query.rfind("DROP", 0) == 0 && upper_query.find("TABLE") != std::string::npos) {
        return handle_drop_table(query);
    } else if (upper_query.rfind("INSERT", 0) == 0) {
        return handle_insert(query);
    } else {
        return "ERROR: Unknown query type";
    }
}

void handle_client(int client_socket) {
    char buffer[256 * 1024]; // 256KB buffer for fast network reads
    std::string pending;
    pending.reserve(10 * 1024 * 1024); // Reserve 10MB to avoid reallocation
    
    while (true) {
        ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            break;
        }

        buffer[bytes_read] = '\0';
        size_t search_start = pending.size();
        pending.append(buffer, bytes_read);

        size_t pos = 0;
        while ((pos = pending.find(';', search_start)) != std::string::npos) {
            std::string query;
            if (pos == pending.length() - 1) {
                query = std::move(pending);
                pending.clear();
            } else {
                query = pending.substr(0, pos + 1);
                pending.erase(0, pos + 1);
                search_start = 0;
            }

            size_t start = query.find_first_not_of(" \t\r\n");
            size_t end = query.find_last_not_of(" \t\r\n");
            if (start != std::string::npos) {
                query = query.substr(start, end - start + 1);
            } else {
                continue;
            }

            // Only uppercase the first 32 characters for command detection to save CPU on huge inserts
            std::string upper_query = query.substr(0, std::min((size_t)32, query.length()));
            for (auto& c : upper_query) c = ::toupper((unsigned char)c);


            std::string response;
            bool is_select = upper_query.rfind("SELECT", 0) == 0;
            if (is_select) {
                response = handle_select_streaming(client_socket, query);
            } else {
                response = execute_query(query);
            }

            // For SELECT queries, data has already been streamed to socket, only send END marker
            if (is_select) {
                if (response.find("ERROR:") == 0) {
                    response += "\nEND\n";
                } else {
                    response = "END\n";
                }
            } else {
                // For other queries, send the response with END marker
                if (response != "OK" && !response.empty()) {
                    response += "\n";
                }
                response += "END\n";
            }

            send(client_socket, response.c_str(), response.length(), 0);
        }
    }

    close(client_socket);
}

static bool parse_schema_line(const std::string& line, storage::Column& col) {
    std::istringstream iss(line);
    std::string name;
    std::string type;
    if (!(iss >> name >> type)) {
        return false;
    }

    std::string upper_type = type;
    for (auto& c : upper_type) c = toupper(c);

    int type_id = -1;
    if (upper_type == "INT" || upper_type == "INTEGER") {
        type_id = TYPE_INT;
    } else if (upper_type == "DECIMAL" || upper_type == "FLOAT" || upper_type == "DOUBLE" || upper_type == "DATETIME") {
        type_id = TYPE_DECIMAL;
    } else if (upper_type == "STRING" || upper_type == "VARCHAR" || upper_type == "TEXT") {
        type_id = TYPE_STRING;
    } else {
        return false;
    }

    strncpy(col.name, name.c_str(), 63);
    col.name[63] = '\0';
    strncpy(col.type, upper_type.c_str(), 63);
    col.type[63] = '\0';
    col.type_id = type_id;
    return true;
}

static void load_existing_tables() {
    DIR* dir = opendir("tables");
    if (!dir) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        const std::string suffix = ".schema";
        if (name.size() <= suffix.size() || name.substr(name.size() - suffix.size()) != suffix) {
            continue;
        }

        std::string table_name = name.substr(0, name.size() - suffix.size());
        std::ifstream schema_file("tables/" + name);
        if (!schema_file.is_open()) {
            continue;
        }

        std::vector<storage::Column> columns;
        std::string line;
        while (std::getline(schema_file, line)) {
            if (line.empty()) {
                continue;
            }
            storage::Column col;
            if (parse_schema_line(line, col)) {
                columns.push_back(col);
            }
        }

        if (columns.empty()) {
            continue;
        }

        storage::Table* table = storage::open_table(table_name, columns);
        if (!table) {
            continue;
        }

        std::lock_guard<std::mutex> lock(g_tables_mutex);
        g_tables.push_back(table);
        g_table_names.push_back(table_name);
    }

    closedir(dir);
}

int main() {
    // Create tables directory if it doesn't exist
    mkdir("tables", 0755);

    load_existing_tables();

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        return 1;
    }

    int reuse = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(9000);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) < 0) {
        perror("listen");
        close(server_socket);
        return 1;
    }

    std::cout << "FlexQL Server listening on port 9000...\n";

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            perror("accept");
            continue;
        }

        std::thread client_thread(handle_client, client_socket);
        client_thread.detach();
    }

    close(server_socket);
    return 0;
}
