#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int connect_to_server() {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::cerr << "ERROR: Failed to create socket" << std::endl;
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);

    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        std::cerr << "ERROR: Invalid address" << std::endl;
        close(socket_fd);
        return -1;
    }

    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "ERROR: Failed to connect to server" << std::endl;
        close(socket_fd);
        return -1;
    }

    return socket_fd;
}

std::string send_query(int socket_fd, const std::string& query) {
    if (send(socket_fd, query.c_str(), query.length(), 0) < 0) {
        return "ERROR: Failed to send query";
    }

    std::string response;
    char buffer[4096];

    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_read = recv(socket_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read <= 0) {
            break;
        }

        buffer[bytes_read] = '\0';
        response += buffer;

        if (response.find("\nEND\n") != std::string::npos) {
            size_t end_pos = response.find("\nEND\n");
            response = response.substr(0, end_pos);
            break;
        }
    }

    return response;
}

int main() {
    std::cout << "FlexQL Client v1.0\n";
    std::cout << "Type 'exit' to disconnect\n\n";

    int socket_fd = connect_to_server();
    if (socket_fd < 0) {
        return 1;
    }

    std::cout << "Connected to FlexQL Server\n\n";

    std::string query;
    while (true) {
        std::cout << "flexql> ";
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

        std::string response = send_query(socket_fd, query + "\n");
        std::cout << response << "\n";
    }

    close(socket_fd);
    return 0;
}
