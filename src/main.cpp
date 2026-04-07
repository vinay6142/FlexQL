#include <iostream>
#include <string>

int main() {
    std::cout << "FlexQL Interactive Query Runner\n";
    std::cout << "Type SQL queries to execute them\n\n";

    std::string query;
    while (true) {
        std::cout << "query> ";
        std::cout.flush();

        if (!std::getline(std::cin, query)) {
            break;
        }

        if (query == "exit" || query == "quit") {
            break;
        }

        if (query.empty()) {
            continue;
        }

        // Query execution would happen here
        std::cout << "Query: " << query << "\n";
    }

    return 0;
}
