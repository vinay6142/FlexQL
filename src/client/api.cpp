#include "flexql.h"
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <sstream>
#include <cstdlib>

struct FlexQL {
    int sockfd;
    std::string last_error;
};

std::vector<char*> parse_row(const std::string& line, int& col_count) {
    std::vector<char*> values;
    std::string current_value;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];

        if (c == '|') {
            if (current_value == "NULL") {
                values.push_back(nullptr);
            } else {
                char* val = strdup(current_value.c_str());
                values.push_back(val);
            }
            current_value.clear();
            if (i + 1 < line.length() && line[i+1] == ' ') {
                ++i;
            }
        } else {
            current_value += c;
        }
    }

    if (!current_value.empty() || values.size() > 0) {
        if (current_value == "NULL") {
            values.push_back(nullptr);
        } else {
            char* val = strdup(current_value.c_str());
            values.push_back(val);
        }
    }

    col_count = values.size();
    return values;
}

std::vector<char*> parse_header(const std::string& line, int& col_count) {
    std::vector<char*> names;
    std::string current_name;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];

        if (c == '|') {
            if (!current_name.empty()) {
                while (!current_name.empty() && current_name.back() == ' ') {
                    current_name.pop_back();
                }
                char* name = strdup(current_name.c_str());
                names.push_back(name);
                current_name.clear();
            }
            if (i + 1 < line.length() && line[i+1] == ' ') {
                ++i;
            }
        } else {
            current_name += c;
        }
    }

    if (!current_name.empty()) {
        while (!current_name.empty() && current_name.back() == ' ') {
            current_name.pop_back();
        }
        char* name = strdup(current_name.c_str());
        names.push_back(name);
    }

    col_count = names.size();
    return names;
}

void free_row(std::vector<char*>& row) {
    for (auto ptr : row) {
        if (ptr) {
            free(ptr);
        }
    }
}

int flexql_open(const char* host, int port, FlexQL** db) {
    if (!host || !db) {
        return FLEXQL_ERROR;
    }

    *db = nullptr;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return FLEXQL_ERROR;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        close(sockfd);
        return FLEXQL_ERROR;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return FLEXQL_ERROR;
    }

    FlexQL* context = new FlexQL();
    context->sockfd = sockfd;
    *db = context;

    return FLEXQL_OK;
}

int flexql_close(FlexQL* db) {
    if (!db) {
        return FLEXQL_ERROR;
    }

    if (db->sockfd >= 0) {
        close(db->sockfd);
    }

    delete db;
    return FLEXQL_OK;
}

int flexql_exec(
    FlexQL* db,
    const char* sql,
    int (*callback)(void*, int, char**, char**),
    void* arg,
    char** errmsg) {

    if (!db || !sql || !errmsg) {
        return FLEXQL_ERROR;
    }

    *errmsg = nullptr;

    size_t sql_len = strlen(sql);
    bool needs_newline = (sql_len > 0 && sql[sql_len - 1] != '\n');

    if (send(db->sockfd, sql, sql_len, 0) < 0) {
        *errmsg = strdup("Network error: failed to send query");
        return FLEXQL_ERROR;
    }
    
    if (needs_newline) {
        if (send(db->sockfd, "\n", 1, 0) < 0) {
            *errmsg = strdup("Network error: failed to send newline");
            return FLEXQL_ERROR;
        }
    }

    std::string response;
    char buffer[4096];

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = recv(db->sockfd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read < 0) {
            *errmsg = strdup("Network error: failed to receive response");
            return FLEXQL_ERROR;
        }

        if (bytes_read == 0) {
            *errmsg = strdup("Connection closed by server");
            return FLEXQL_ERROR;
        }

        buffer[bytes_read] = '\0';
        response += buffer;

        if (response.find("\nEND\n") != std::string::npos ||
            response.find("END\n") == response.length() - 4) {
            size_t end_pos = response.rfind("\nEND\n");
            if (end_pos != std::string::npos) {
                response = response.substr(0, end_pos);
            } else {
                end_pos = response.rfind("END\n");
                if (end_pos != std::string::npos) {
                    response = response.substr(0, end_pos);
                }
            }
            break;
        }
    }

    std::istringstream iss(response);
    std::string line;

    std::vector<char*> column_names;
    int col_count = 0;
    bool is_first_line = true;
    bool is_error = false;

    while (std::getline(iss, line)) {
        if (line.empty() || (line.find_first_not_of("-+") == std::string::npos)) {
            continue;
        }

        if (line.find("ERROR") == 0) {
            *errmsg = strdup(line.c_str());
            is_error = true;
            break;
        }

        if (is_first_line) {
            column_names = parse_header(line, col_count);
            is_first_line = false;
            continue;
        }

        if (col_count > 0) {
            std::vector<char*> row_values = parse_row(line, col_count);

            if (!row_values.empty() && callback) {
                char** values_array = new char*[row_values.size()];
                char** names_array = new char*[column_names.size()];

                for (size_t i = 0; i < row_values.size(); ++i) {
                    values_array[i] = row_values[i];
                }

                for (size_t i = 0; i < column_names.size(); ++i) {
                    names_array[i] = column_names[i];
                }

                callback(arg, col_count, values_array, names_array);

                delete[] values_array;
                delete[] names_array;
            }

            free_row(row_values);
        }
    }

    free_row(column_names);

    return is_error ? FLEXQL_ERROR : FLEXQL_OK;
}

void flexql_free(void* ptr) {
    if (ptr) {
        free(ptr);
    }
}
