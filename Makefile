CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O3 -fno-strict-aliasing -I$(PWD)/include
LDFLAGS = -lm

# Directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = tests
BUILD_DIR = build

# Output
TARGET_STORAGE = $(BUILD_DIR)/test_storage
TARGET_INDEX = $(BUILD_DIR)/test_index
TARGET_EXECUTOR = $(BUILD_DIR)/test_executor
TARGET_PLANNER = $(BUILD_DIR)/test_planner
TARGET_PARSER = $(BUILD_DIR)/test_parser
TARGET_RESOLVER = $(BUILD_DIR)/test_resolver
TARGET_DELETE = $(BUILD_DIR)/test_delete
TARGET_UPDATE = $(BUILD_DIR)/test_update
TARGET_MAIN = $(BUILD_DIR)/query_runner

# Server and Client
TARGET_SERVER = $(BUILD_DIR)/server
TARGET_CLIENT = $(BUILD_DIR)/client

# Benchmark
TARGET_BENCH = $(BUILD_DIR)/benchmark_flexql
BENCH_SRC = $(TEST_DIR)/benchmark_flexql.cpp
BENCH_OBJ = $(BUILD_DIR)/benchmark_flexql.o
TARGET_BENCH_INSERT = $(BUILD_DIR)/benchmark_insert
BENCH_INSERT_SRC = $(TEST_DIR)/benchmark_insert.cpp
BENCH_INSERT_OBJ = $(BUILD_DIR)/benchmark_insert.o

# Source files
STORAGE_SRC = $(SRC_DIR)/storage/storage.cpp
INDEX_SRC = $(SRC_DIR)/index/index.cpp
EXECUTOR_SRC = $(SRC_DIR)/executor/executor.cpp
PLANNER_SRC = $(SRC_DIR)/planner/planner.cpp
RESOLVER_SRC = $(SRC_DIR)/executor/resolver.cpp
PARSER_SRC = $(SRC_DIR)/parser/parser.cpp
MAIN_SRC = $(SRC_DIR)/main.cpp
SERVER_SRC = $(SRC_DIR)/server/server.cpp
CLIENT_SRC = $(SRC_DIR)/client/client.cpp
API_SRC = $(SRC_DIR)/client/api.cpp
TEST_STORAGE_SRC = $(TEST_DIR)/test_storage.cpp
TEST_INDEX_SRC = $(TEST_DIR)/test_index.cpp
TEST_EXECUTOR_SRC = $(TEST_DIR)/test_executor.cpp
TEST_PLANNER_SRC = $(TEST_DIR)/test_planner.cpp
TEST_RESOLVER_SRC = $(TEST_DIR)/test_resolver.cpp
TEST_PARSER_SRC = $(TEST_DIR)/test_parser.cpp
TEST_DELETE_SRC = $(TEST_DIR)/test_delete.cpp
TEST_UPDATE_SRC = $(TEST_DIR)/test_update.cpp
TEST_PERSISTENCE_SRC = $(TEST_DIR)/test_persistence.cpp

# Object files
STORAGE_OBJ = $(BUILD_DIR)/storage.o
INDEX_OBJ = $(BUILD_DIR)/index.o
EXECUTOR_OBJ = $(BUILD_DIR)/executor.o
PLANNER_OBJ = $(BUILD_DIR)/planner.o
PARSER_OBJ = $(BUILD_DIR)/parser.o
RESOLVER_OBJ = $(BUILD_DIR)/resolver.o
MAIN_OBJ = $(BUILD_DIR)/main.o
SERVER_OBJ = $(BUILD_DIR)/server.o
CLIENT_OBJ = $(BUILD_DIR)/client.o
API_OBJ = $(BUILD_DIR)/api.o
TEST_STORAGE_OBJ = $(BUILD_DIR)/test_storage.o
TEST_INDEX_OBJ = $(BUILD_DIR)/test_index.o
TEST_EXECUTOR_OBJ = $(BUILD_DIR)/test_executor.o
TEST_PLANNER_OBJ = $(BUILD_DIR)/test_planner.o
TEST_PARSER_OBJ = $(BUILD_DIR)/test_parser.o
TEST_RESOLVER_OBJ = $(BUILD_DIR)/test_resolver.o
TEST_DELETE_OBJ = $(BUILD_DIR)/test_delete.o
TEST_UPDATE_OBJ = $(BUILD_DIR)/test_update.o
TEST_PERSISTENCE_OBJ = $(BUILD_DIR)/test_persistence.o
TARGET_PERSISTENCE = $(BUILD_DIR)/test_persistence

# Phony targets
.PHONY: all clean test test_storage test_index test_executor test_planner test_parser test_resolver test_delete test_update test_persistence benchmark benchmark_insert query_runner server client

all: $(TARGET_STORAGE) $(TARGET_INDEX) $(TARGET_EXECUTOR) $(TARGET_PLANNER) $(TARGET_PARSER) $(TARGET_RESOLVER) $(TARGET_DELETE) $(TARGET_UPDATE) $(TARGET_MAIN) $(TARGET_BENCH)

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Build storage object
$(STORAGE_OBJ): $(STORAGE_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(STORAGE_SRC) -o $(STORAGE_OBJ)

# Build index object
$(INDEX_OBJ): $(INDEX_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(INDEX_SRC) -o $(INDEX_OBJ)

# Build executor object
$(EXECUTOR_OBJ): $(EXECUTOR_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(EXECUTOR_SRC) -o $(EXECUTOR_OBJ)

# Build planner object
$(PLANNER_OBJ): $(PLANNER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(PLANNER_SRC) -o $(PLANNER_OBJ)
$(PLANNER_OBJ): $(PLANNER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(PLANNER_SRC) -o $(PLANNER_OBJ)

# Build parser object
$(PARSER_OBJ): $(PARSER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(PARSER_SRC) -o $(PARSER_OBJ)

# Build resolver object
$(RESOLVER_OBJ): $(RESOLVER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(RESOLVER_SRC) -o $(RESOLVER_OBJ)
$(RESOLVER_OBJ): $(RESOLVER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(RESOLVER_SRC) -o $(RESOLVER_OBJ)

# Build main object
$(MAIN_OBJ): $(MAIN_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(MAIN_SRC) -o $(MAIN_OBJ)

# Build test_storage object
$(TEST_STORAGE_OBJ): $(TEST_STORAGE_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_STORAGE_SRC) -o $(TEST_STORAGE_OBJ)

# Build test_index object
$(TEST_INDEX_OBJ): $(TEST_INDEX_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_INDEX_SRC) -o $(TEST_INDEX_OBJ)

# Build test_executor object
$(TEST_EXECUTOR_OBJ): $(TEST_EXECUTOR_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_EXECUTOR_SRC) -o $(TEST_EXECUTOR_OBJ)

# Build test_planner object
$(TEST_PLANNER_OBJ): $(TEST_PLANNER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_PLANNER_SRC) -o $(TEST_PLANNER_OBJ)
$(TEST_PLANNER_OBJ): $(TEST_PLANNER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_PLANNER_SRC) -o $(TEST_PLANNER_OBJ)

# Build test_parser object
$(TEST_PARSER_OBJ): $(TEST_PARSER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_PARSER_SRC) -o $(TEST_PARSER_OBJ)

# Build test_resolver object
$(TEST_RESOLVER_OBJ): $(TEST_RESOLVER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_RESOLVER_SRC) -o $(TEST_RESOLVER_OBJ)
$(TEST_RESOLVER_OBJ): $(TEST_RESOLVER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_RESOLVER_SRC) -o $(TEST_RESOLVER_OBJ)

# Build test_delete object
$(TEST_DELETE_OBJ): $(TEST_DELETE_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_DELETE_SRC) -o $(TEST_DELETE_OBJ)

# Build test_update object
$(TEST_UPDATE_OBJ): $(TEST_UPDATE_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_UPDATE_SRC) -o $(TEST_UPDATE_OBJ)

# Build test_persistence object
$(TEST_PERSISTENCE_OBJ): $(TEST_PERSISTENCE_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(TEST_PERSISTENCE_SRC) -o $(TEST_PERSISTENCE_OBJ)

# Build bench object
$(BENCH_OBJ): $(BENCH_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(BENCH_SRC) -o $(BENCH_OBJ)

# Build bench insert object
$(BENCH_INSERT_OBJ): $(BENCH_INSERT_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(BENCH_INSERT_SRC) -o $(BENCH_INSERT_OBJ)

# Link tests
$(TARGET_STORAGE): $(STORAGE_OBJ) $(TEST_STORAGE_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(TEST_STORAGE_OBJ) -o $(TARGET_STORAGE) $(LDFLAGS)

$(TARGET_INDEX): $(STORAGE_OBJ) $(INDEX_OBJ) $(TEST_INDEX_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(INDEX_OBJ) $(TEST_INDEX_OBJ) -o $(TARGET_INDEX) $(LDFLAGS)

$(TARGET_EXECUTOR): $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(TEST_EXECUTOR_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(TEST_EXECUTOR_OBJ) -o $(TARGET_EXECUTOR) $(LDFLAGS)

$(TARGET_PLANNER): $(STORAGE_OBJ) $(PLANNER_OBJ) $(TEST_PLANNER_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(PLANNER_OBJ) $(TEST_PLANNER_OBJ) -o $(TARGET_PLANNER) $(LDFLAGS)

$(TARGET_PARSER): $(STORAGE_OBJ) $(PARSER_OBJ) $(TEST_PARSER_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(PARSER_OBJ) $(TEST_PARSER_OBJ) -o $(TARGET_PARSER) $(LDFLAGS)

$(TARGET_RESOLVER): $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(TEST_RESOLVER_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(TEST_RESOLVER_OBJ) -o $(TARGET_RESOLVER) $(LDFLAGS)

$(TARGET_DELETE): $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(TEST_DELETE_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(TEST_DELETE_OBJ) -o $(TARGET_DELETE) $(LDFLAGS)

$(TARGET_UPDATE): $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(TEST_UPDATE_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(TEST_UPDATE_OBJ) -o $(TARGET_UPDATE) $(LDFLAGS)

$(TARGET_PERSISTENCE): $(STORAGE_OBJ) $(TEST_PERSISTENCE_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(TEST_PERSISTENCE_OBJ) -o $(TARGET_PERSISTENCE) $(LDFLAGS)

# Link query runner
$(TARGET_MAIN): $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(MAIN_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(MAIN_OBJ) -o $(TARGET_MAIN) $(LDFLAGS)

# Link benchmark
$(TARGET_BENCH): $(API_OBJ) $(BENCH_OBJ)
	$(CXX) $(CXXFLAGS) $(API_OBJ) $(BENCH_OBJ) -o $(TARGET_BENCH) $(LDFLAGS)

# Link benchmark insert
$(TARGET_BENCH_INSERT): $(STORAGE_OBJ) $(BENCH_INSERT_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(BENCH_INSERT_OBJ) -o $(TARGET_BENCH_INSERT) $(LDFLAGS)

# Build server object
$(SERVER_OBJ): $(SERVER_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(SERVER_SRC) -o $(SERVER_OBJ)

# Build client object
$(CLIENT_OBJ): $(CLIENT_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(CLIENT_SRC) -o $(CLIENT_OBJ)

# Build API object
$(API_OBJ): $(API_SRC) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $(API_SRC) -o $(API_OBJ)

# Link server
$(TARGET_SERVER): $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) $(STORAGE_OBJ) $(INDEX_OBJ) $(EXECUTOR_OBJ) $(PLANNER_OBJ) $(PARSER_OBJ) $(RESOLVER_OBJ) $(SERVER_OBJ) -o $(TARGET_SERVER) $(LDFLAGS)

# Link client
$(TARGET_CLIENT): $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) $(CLIENT_OBJ) -o $(TARGET_CLIENT) $(LDFLAGS)

# Run server
server: $(TARGET_SERVER)
	$(TARGET_SERVER)

# Run client
client: $(TARGET_CLIENT)
	$(TARGET_CLIENT)

# Run storage tests
test_storage: $(TARGET_STORAGE)
	$(TARGET_STORAGE)

# Run index tests
test_index: $(TARGET_INDEX)
	$(TARGET_INDEX)

# Run executor tests
test_executor: $(TARGET_EXECUTOR)
	$(TARGET_EXECUTOR)

# Run planner tests
test_planner: $(TARGET_PLANNER)
	$(TARGET_PLANNER)

# Run parser tests
test_parser: $(TARGET_PARSER)
	$(TARGET_PARSER)

# Run resolver tests
test_resolver: $(TARGET_RESOLVER)
	$(TARGET_RESOLVER)

# Run delete tests
test_delete: $(TARGET_DELETE)
	$(TARGET_DELETE)

# Run update tests
test_update: $(TARGET_UPDATE)
	$(TARGET_UPDATE)

# Run persistence tests
test_persistence: $(TARGET_PERSISTENCE)
	$(TARGET_PERSISTENCE)

# Run query runner
query_runner: $(TARGET_MAIN)
	$(TARGET_MAIN)

# Run benchmark
benchmark: $(TARGET_BENCH)
	$(TARGET_BENCH)

# Run benchmark insert
benchmark_insert: $(TARGET_BENCH_INSERT)
	$(TARGET_BENCH_INSERT)

# Run all tests
test: test_storage test_index test_executor test_planner test_parser test_resolver test_delete test_update test_persistence

clean:
	@rm -rf $(BUILD_DIR)
	@rm -rf tables/
	@echo "Cleaned"

.DEFAULT_GOAL := all
