#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cstring>
#include <cstdlib>

using namespace std;
using namespace std::chrono;

enum DataType { TYPE_INT, TYPE_DECIMAL, TYPE_STRING, TYPE_DATE };
struct Value { DataType type; bool is_null; int64_t int_val; double decimal_val; string string_val; };

bool fast_parse(const string& sql, vector<vector<Value>>& rows) {
    size_t values_pos = sql.find("VALUES");
    if (values_pos == string::npos) return false;
    
    const char *ptr = sql.c_str() + values_pos + 6;
    while (*ptr) {
        if (*ptr == '(') {
            ptr++;
            vector<Value> row;
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
                    while (*ptr && *ptr != ',' && *ptr != ')') ptr++;
                } else { // Number
                    char *end;
                    double d = strtod(ptr, &end);
                    if (end == ptr) return false;
                    v.type = TYPE_DECIMAL;
                    v.decimal_val = d;
                    v.int_val = (int64_t)d;
                    ptr = end;
                }
                row.push_back(v);
            }
            if (*ptr == ')') ptr++;
            if (!row.empty()) rows.push_back(move(row));
        } else {
            ptr++;
        }
    }
    return !rows.empty();
}

int main() {
    string sql = "INSERT INTO BIG_USERS VALUES ";
    for(long long id = 1; id <= 100000; ++id) {
        sql += "(" + to_string(id) + ", 'user" + to_string(id) + "', 'user" + to_string(id) + "@mail.com', " + to_string(1000.0 + (id % 10000)) + ", 1893456000)";
        if(id < 100000) sql += ",";
    }
    sql += ";";

    auto t1 = high_resolution_clock::now();
    vector<vector<Value>> rows;
    fast_parse(sql, rows);
    auto t2 = high_resolution_clock::now();
    cout << duration_cast<milliseconds>(t2 - t1).count() << " ms for 100k, rows: " << rows.size() << endl;
    return 0;
}
