#include "xc/mcp_core.hpp"

#include <iostream>
#include <string>

int main() {
    xc::mcp::McpState* state = xc::mcp::createState();
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        const std::string response = xc::mcp::handleRequest(*state, line);
        if (!response.empty()) {
            std::cout << response << "\n";
            std::cout.flush();
        }
    }
    xc::mcp::destroyState(state);
    return 0;
}
