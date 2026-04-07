#pragma once

#include <string>
#include "executor/executor.hpp"

namespace flexql {
namespace parser {

bool parse_select_query(const std::string& sql, executor::SelectQuery& out);
bool parse_insert_query(const std::string& sql, executor::InsertQuery& out);
bool parse_delete_query(const std::string& sql, executor::DeleteQuery& out);
bool parse_update_query(const std::string& sql, executor::UpdateQuery& out);
bool parse_create_table(const std::string& sql, executor::CreateTableQuery& out);
bool parse_join_query(const std::string& sql, executor::JoinQuery& out);

} // namespace parser
} // namespace flexql
