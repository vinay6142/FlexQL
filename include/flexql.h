#ifndef FLEXQL_H
#define FLEXQL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * FlexQL Client C API
 * Connects to FlexQL server via TCP socket
 */

typedef struct FlexQL FlexQL;

#define FLEXQL_OK 0
#define FLEXQL_ERROR 1

/**
 * Connect to FlexQL server
 * @param host Server hostname (e.g., "127.0.0.1")
 * @param port Server port (e.g., 9000)
 * @param db Pointer to store FlexQL handle
 * @return FLEXQL_OK on success, FLEXQL_ERROR on failure
 */
int flexql_open(const char* host, int port, FlexQL** db);

/**
 * Close connection to FlexQL server
 * @param db FlexQL handle from flexql_open
 * @return FLEXQL_OK on success, FLEXQL_ERROR on failure
 */
int flexql_close(FlexQL* db);

/**
 * Execute SQL query on server
 * @param db FlexQL handle from flexql_open
 * @param sql SQL query string
 * @param callback Function called for each row: callback(arg, column_count, values, column_names)
 * @param arg User context passed to callback
 * @param errmsg Pointer to error message (caller must free with flexql_free)
 * @return FLEXQL_OK on success, FLEXQL_ERROR on failure
 */
int flexql_exec(
    FlexQL* db,
    const char* sql,
    int (*callback)(void*, int, char**, char**),
    void* arg,
    char** errmsg
);

/**
 * Free memory allocated by FlexQL API
 * @param ptr Pointer to free (typically error message from flexql_exec)
 */
void flexql_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif
