#include "parser/parser.hpp"
#include <cctype>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace flexql {
namespace parser {

using flexql::executor::SelectQuery;
using flexql::executor::InsertQuery;
using flexql::executor::DeleteQuery;
using flexql::executor::UpdateQuery;
using flexql::executor::CreateTableQuery;
using flexql::storage::Value;

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string to_upper(std::string str) {
    for (auto& c : str) c = toupper(c);
    return str;
}

size_t find_keyword(const std::string& str, const std::string& keyword) {
    auto it = std::search(
        str.begin(), str.end(),
        keyword.begin(), keyword.end(),
        [](char ch1, char ch2) { return std::toupper((unsigned char)ch1) == std::toupper((unsigned char)ch2); }
    );
    return (it != str.end()) ? std::distance(str.begin(), it) : std::string::npos;
}

std::vector<std::string> split(const std::string& str, const std::string& delim) {
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = str.find(delim);
    while (end != std::string::npos) {
        result.push_back(trim(str.substr(start, end - start)));
        start = end + delim.length();
        end = str.find(delim, start);
    }
    result.push_back(trim(str.substr(start)));
    return result;
}

bool parse_where_clause(const std::string& where_part, executor::WhereClause& out,
                        std::string* out_table_name) {
    out.active = false;
    out.column.clear();
    out.op.clear();
    out.value = Value();

    if (where_part.empty()) {
        return false;
    }

    const std::vector<std::string> ops = {"<=", ">=", "!=", "=", "<", ">"};
    size_t op_pos = std::string::npos;
    std::string op;
    for (const auto& candidate : ops) {
        op_pos = where_part.find(candidate);
        if (op_pos != std::string::npos) {
            op = candidate;
            break;
        }
    }

    if (op.empty()) {
        return false;
    }

    std::string lhs = trim(where_part.substr(0, op_pos));
    std::string rhs = trim(where_part.substr(op_pos + op.size()));

    if (lhs.empty() || rhs.empty()) {
        return false;
    }

    size_t dot_pos = lhs.find_last_of('.');
    if (dot_pos != std::string::npos && dot_pos + 1 < lhs.size()) {
        if (out_table_name) {
            *out_table_name = trim(lhs.substr(0, dot_pos));
        }
        lhs = lhs.substr(dot_pos + 1);
    }

    out.active = true;
    out.column = lhs;
    out.op = op;

    if (rhs.front() == '\'' && rhs.back() == '\'' && rhs.size() >= 2) {
        out.value.type = TYPE_STRING;
        out.value.is_null = false;
        out.value.string_val = rhs.substr(1, rhs.size() - 2);
    } else {
        out.value.type = TYPE_DECIMAL;
        out.value.is_null = false;
        try {
            out.value.decimal_val = std::stod(rhs);
            out.value.int_val = (int64_t)out.value.decimal_val;
        } catch (const std::exception&) {
            return false;
        }
    }

    return true;
}

bool parse_select_query(const std::string& sql, SelectQuery& out) {
    out.columns.clear();
    out.table_name.clear();
    out.where.active = false;
    out.where.column.clear();
    out.where.op.clear();
    out.where.value = Value();
    out.limit = -1;

    std::string upper_sql = to_upper(sql);

    size_t select_pos = find_keyword(sql, "SELECT");
    if (select_pos == std::string::npos) return false;

    size_t from_pos = find_keyword(sql, "FROM");
    if (from_pos == std::string::npos) return false;

    std::string select_part = sql.substr(select_pos + 6, from_pos - select_pos - 6);
    std::vector<std::string> cols = split(trim(select_part), ",");
    for (auto& col : cols) {
        out.columns.push_back(trim(col));
    }

    size_t where_pos = find_keyword(sql, "WHERE");
    size_t limit_pos = find_keyword(sql, "LIMIT");
    size_t end_pos = (where_pos != std::string::npos) ? where_pos : ((limit_pos != std::string::npos) ? limit_pos : sql.length());

    // Find end of table name (semicolon orend of string)
    size_t semicolon_pos = sql.find(';', from_pos + 4);
    if (semicolon_pos != std::string::npos && semicolon_pos < end_pos) {
        end_pos = semicolon_pos;
    }

    std::string table_part = sql.substr(from_pos + 4, end_pos - (from_pos + 4));
    out.table_name = trim(table_part);

    if (where_pos != std::string::npos) {
        size_t where_end = (limit_pos != std::string::npos && limit_pos > where_pos) ? limit_pos : sql.length();
        size_t where_semicolon = sql.find(';', where_pos + 5);
        if (where_semicolon != std::string::npos && where_semicolon < where_end) {
            where_end = where_semicolon;
        }

        std::string where_part = trim(sql.substr(where_pos + 5, where_end - (where_pos + 5)));
        if (!parse_where_clause(where_part, out.where, nullptr)) {
            return false;
        }
    }

    if (limit_pos != std::string::npos) {
        size_t limit_end = sql.find(';', limit_pos + 5);
        if (limit_end == std::string::npos) {
            limit_end = sql.length();
        }
        std::string limit_part = trim(sql.substr(limit_pos + 5, limit_end - (limit_pos + 5)));
        if (!limit_part.empty()) {
            try {
                out.limit = std::stoi(limit_part);
            } catch (const std::exception&) {
                return false;
            }
        }
    }

    return !out.table_name.empty() && !out.columns.empty();
}

bool parse_insert_query(const std::string& sql, InsertQuery& out) {
    out.table_name.clear();
    out.rows.clear();
    out.table = nullptr;

    size_t insert_pos = find_keyword(sql, "INSERT");
    if (insert_pos == std::string::npos) return false;

    size_t into_pos = find_keyword(sql, "INTO");
    if (into_pos == std::string::npos) return false;

    size_t values_pos = find_keyword(sql, "VALUES");
    if (values_pos == std::string::npos) return false;

    std::string table_part = sql.substr(into_pos + 4, values_pos - (into_pos + 4));
    out.table_name = trim(table_part);

    const char *ptr = sql.c_str() + values_pos + 6;
    size_t estimated_rows = 0;
    for (const char* p = ptr; *p; ++p) {
        if (*p == '(') {
            estimated_rows++;
        }
    }
    if (estimated_rows > 0) {
        out.rows.reserve(estimated_rows);
    }
    while (*ptr) {
        if (*ptr == '(') {
            ptr++;
            out.rows.emplace_back();
            auto& row = out.rows.back();
            row.reserve(5);
            
            while (*ptr && *ptr != ')') {
                while (*ptr == ' ' || *ptr == ',') ptr++;
                if (*ptr == ')') break;
                
                Value v;
                v.is_null = false;
                if (*ptr == '\'') { // String
                    ptr++;
                    const char *start = ptr;
                    while (*ptr && *ptr != '\'') ptr++;
                    v.type = TYPE_STRING;
                    v.string_val.assign(start, ptr - start);
                    if (*ptr == '\'') ptr++;
                } else if (*ptr == 'N' || *ptr == 'n') { // NULL
                    v.is_null = true;
                    // Skip correctly until end of token
                    while (*ptr && *ptr != ',' && *ptr != ')') ptr++;
                } else { // Number
                    double d = 0;
                    double fraction = 0;
                    double divisor = 1;
                    bool is_fraction = false;
                    bool is_neg = false;
                    if (*ptr == '-') { is_neg = true; ptr++; }
                    while ((*ptr >= '0' && *ptr <= '9') || *ptr == '.') {
                        if (*ptr == '.') {
                            is_fraction = true;
                        } else {
                            if (!is_fraction) {
                                d = d * 10 + (*ptr - '0');
                            } else {
                                fraction = fraction * 10 + (*ptr - '0');
                                divisor *= 10;
                            }
                        }
                        ptr++;
                    }
                    d += fraction / divisor;
                    if (is_neg) d = -d;

                    v.type = TYPE_DECIMAL;
                    v.decimal_val = d;
                    v.int_val = (int64_t)d;
                }
                row.push_back(std::move(v));
            }
            if (*ptr == ')') ptr++;
            if (row.empty()) {
                out.rows.pop_back();
            }
        } else {
            ptr++;
        }
    }

    return !out.rows.empty();
}

bool parse_delete_query(const std::string& sql, DeleteQuery& out) {
    std::string upper_sql = to_upper(sql);
    if (upper_sql.find("DELETE") != 0) return false;

    size_t from_pos = find_keyword(sql, "FROM");
    if (from_pos == std::string::npos) return false;

    size_t where_pos = find_keyword(sql, "WHERE");
    size_t end_pos = (where_pos != std::string::npos) ? where_pos : sql.find(';');
    if (end_pos == std::string::npos) end_pos = sql.length();

    std::string table_part = trim(sql.substr(from_pos + 4, end_pos - (from_pos + 4)));
    out.table_name = table_part;
    out.table = nullptr;

    out.where.active = false;

    return true;
}

bool parse_update_query(const std::string& sql, UpdateQuery& out) {
    std::string upper_sql = to_upper(sql);
    if (upper_sql.find("UPDATE") != 0) return false;

    size_t set_pos = find_keyword(sql, "SET");
    if (set_pos == std::string::npos) return false;

    size_t update_pos = find_keyword(sql, "UPDATE");
    std::string table_part = trim(sql.substr(update_pos + 6, set_pos - (update_pos + 6)));
    out.table_name = table_part;

    out.table = nullptr;
    out.where.active = false;

    return true;
}

bool parse_create_table(const std::string& sql, CreateTableQuery& out) {
    out.table_name.clear();
    out.column_names.clear();
    out.column_types.clear();
    out.column_type_names.clear();

    std::string trimmed = trim(sql);

    if (trimmed.empty()) return false;

    if (!trimmed.empty() && trimmed.back() == ';') {
        trimmed.pop_back();
        trimmed = trim(trimmed);
    }

    size_t create_pos = find_keyword(trimmed, "CREATE");
    if (create_pos != 0) return false;

    size_t table_pos = find_keyword(trimmed, "TABLE");
    if (table_pos == std::string::npos) return false;

    size_t name_start = table_pos + 5;
    size_t paren_pos = trimmed.find('(', name_start);
    if (paren_pos == std::string::npos) return false;

    out.table_name = trim(trimmed.substr(name_start, paren_pos - name_start));
    if (out.table_name.empty()) return false;

    size_t close_paren = trimmed.rfind(')');
    if (close_paren == std::string::npos || close_paren <= paren_pos) return false;

    std::string columns_str = trimmed.substr(paren_pos + 1, close_paren - paren_pos - 1);
    std::vector<std::string> column_defs = split(columns_str, ",");

    if (column_defs.empty()) return false;

    for (const auto& col_def : column_defs) {
        std::vector<std::string> parts = split(col_def, " ");
        if (parts.size() < 2) return false;

        std::string col_name = parts[0];
        std::string col_type_str = to_upper(parts[1]);

        size_t paren_pos = col_type_str.find('(');
        if (paren_pos != std::string::npos) {
            col_type_str = col_type_str.substr(0, paren_pos);
        }

        int col_type = -1;
        if (col_type_str == "INT" || col_type_str == "INTEGER") {
            col_type = TYPE_INT;
        } else if (col_type_str == "DECIMAL" || col_type_str == "FLOAT" || col_type_str == "DOUBLE" || col_type_str == "DATETIME") {
            col_type = TYPE_DECIMAL;
        } else if (col_type_str == "STRING" || col_type_str == "VARCHAR" || col_type_str == "TEXT") {
            col_type = TYPE_STRING;
        } else {
            return false;
        }

        out.column_names.push_back(col_name);
        out.column_types.push_back(col_type);
        out.column_type_names.push_back(col_type_str);
    }

    return true;
}

bool parse_join_query(const std::string& sql, executor::JoinQuery& out) {
    out.columns.clear();
    out.left_table_name.clear();
    out.right_table_name.clear();
    out.left_join_column.clear();
    out.right_join_column.clear();
    out.where.active = false;
    out.where_table_name.clear();
    out.limit = -1;
    out.left_table = nullptr;
    out.right_table = nullptr;

    size_t select_pos = find_keyword(sql, "SELECT");
    if (select_pos == std::string::npos) return false;

    size_t from_pos = find_keyword(sql, "FROM");
    if (from_pos == std::string::npos) return false;

    size_t join_pos = find_keyword(sql, "JOIN");
    if (join_pos == std::string::npos || join_pos < from_pos) return false;

    size_t on_pos = find_keyword(sql, "ON");
    if (on_pos == std::string::npos || on_pos < join_pos) return false;

    std::string select_part = sql.substr(select_pos + 6, from_pos - select_pos - 6);
    std::vector<std::string> cols = split(trim(select_part), ",");
    for (auto& col : cols) {
        out.columns.push_back(trim(col));
    }

    auto first_token = [](const std::string& input) {
        std::string trimmed = trim(input);
        size_t space_pos = trimmed.find(' ');
        if (space_pos == std::string::npos) {
            return trimmed;
        }
        return trim(trimmed.substr(0, space_pos));
    };

    std::string left_part = sql.substr(from_pos + 4, join_pos - (from_pos + 4));
    out.left_table_name = first_token(left_part);

    std::string right_part = sql.substr(join_pos + 4, on_pos - (join_pos + 4));
    out.right_table_name = first_token(right_part);

    if (out.left_table_name.empty() || out.right_table_name.empty()) {
        return false;
    }

    size_t where_pos = find_keyword(sql, "WHERE");
    size_t limit_pos = find_keyword(sql, "LIMIT");
    size_t on_end = sql.length();
    if (where_pos != std::string::npos && where_pos > on_pos) {
        on_end = where_pos;
    } else if (limit_pos != std::string::npos && limit_pos > on_pos) {
        on_end = limit_pos;
    }
    size_t on_semicolon = sql.find(';', on_pos + 2);
    if (on_semicolon != std::string::npos && on_semicolon < on_end) {
        on_end = on_semicolon;
    }

    std::string on_part = trim(sql.substr(on_pos + 2, on_end - (on_pos + 2)));
    size_t eq_pos = on_part.find('=');
    if (eq_pos == std::string::npos) {
        return false;
    }

    auto split_table_column = [](const std::string& input, std::string& table, std::string& column) {
        std::string trimmed = trim(input);
        size_t dot_pos = trimmed.find('.');
        if (dot_pos != std::string::npos && dot_pos + 1 < trimmed.size()) {
            table = trim(trimmed.substr(0, dot_pos));
            column = trim(trimmed.substr(dot_pos + 1));
        } else {
            table.clear();
            column = trimmed;
        }
    };

    std::string lhs_table;
    std::string rhs_table;
    std::string lhs_col;
    std::string rhs_col;
    split_table_column(on_part.substr(0, eq_pos), lhs_table, lhs_col);
    split_table_column(on_part.substr(eq_pos + 1), rhs_table, rhs_col);

    if (lhs_col.empty() || rhs_col.empty()) {
        return false;
    }

    bool swap_sides = !lhs_table.empty() && lhs_table == out.right_table_name;
    if (swap_sides) {
        out.left_join_column = rhs_col;
        out.right_join_column = lhs_col;
    } else {
        out.left_join_column = lhs_col;
        out.right_join_column = rhs_col;
    }

    if (where_pos != std::string::npos) {
        size_t where_end = (limit_pos != std::string::npos && limit_pos > where_pos) ? limit_pos : sql.length();
        size_t where_semicolon = sql.find(';', where_pos + 5);
        if (where_semicolon != std::string::npos && where_semicolon < where_end) {
            where_end = where_semicolon;
        }

        std::string where_part = trim(sql.substr(where_pos + 5, where_end - (where_pos + 5)));
        if (!parse_where_clause(where_part, out.where, &out.where_table_name)) {
            return false;
        }
    }

    if (limit_pos != std::string::npos) {
        size_t limit_end = sql.find(';', limit_pos + 5);
        if (limit_end == std::string::npos) {
            limit_end = sql.length();
        }
        std::string limit_part = trim(sql.substr(limit_pos + 5, limit_end - (limit_pos + 5)));
        if (!limit_part.empty()) {
            try {
                out.limit = std::stoi(limit_part);
            } catch (const std::exception&) {
                return false;
            }
        }
    }

    return !out.columns.empty();
}

} // namespace parser
} // namespace flexql
