#include "xc/protocol.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

namespace {

int createServer(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    int enabled = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return -1;
    }

    if (::listen(fd, 4) != 0) {
        ::close(fd);
        return -1;
    }

    return fd;
}

void sendLine(int fd, const std::string& line) {
    std::string data = line + "\n";
    const char* ptr = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0) {
        const ssize_t written = ::send(fd, ptr, remaining, 0);
        if (written <= 0) {
            return;
        }
        ptr += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

void handleClient(int clientFd) {
    sendLine(clientFd, xc::helloResponseJson(0));

    char buffer[1024];
    while (true) {
        const ssize_t readCount = ::recv(clientFd, buffer, sizeof(buffer) - 1, 0);
        if (readCount <= 0) {
            break;
        }
        buffer[readCount] = '\0';
        const std::string request(buffer);

        if (request.find("hello") != std::string::npos) {
            sendLine(clientFd, xc::helloResponseJson(1));
        } else if (request.find("driver.status") != std::string::npos) {
            sendLine(clientFd, "{\"id\":1,\"ok\":true,\"driver\":{\"connected\":false,\"status\":\"stub\"}}");
        } else {
            sendLine(clientFd, "{\"id\":1,\"ok\":false,\"error\":\"command not implemented in skeleton\"}");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::uint16_t port = xc::kDefaultAgentPort;
    if (argc > 1) {
        port = static_cast<std::uint16_t>(std::stoi(argv[1]));
    }

    const int serverFd = createServer(port);
    if (serverFd < 0) {
        std::cerr << "failed to listen on port " << port << ": " << std::strerror(errno) << "\n";
        return 1;
    }

    std::cout << "xc-hwbp-agent listening on 0.0.0.0:" << port << "\n";

    while (true) {
        sockaddr_in clientAddress{};
        socklen_t clientLength = sizeof(clientAddress);
        const int clientFd = ::accept(serverFd, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
        if (clientFd < 0) {
            continue;
        }
        handleClient(clientFd);
        ::close(clientFd);
    }
}
