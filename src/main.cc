#include <iostream>
#include <string>
#include <chrono>
#include "node.h"

// Add a global start time for timing measurements
std::chrono::steady_clock::time_point g_startTime = std::chrono::steady_clock::now();

// Function to get elapsed time since program start
std::string getElapsedTime() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_startTime).count();
    return "[" + std::to_string(elapsed) + "ms]";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ./basecamp_node <NodeID> <overlay.json path> [--no-shm]\n";
        std::cerr << "  --no-shm: Disable shared memory caching (use only gRPC)\n";
        return 1;
    }
    
    std::string nodeId = argv[1];
    std::string configPath = argv[2];
    
    // Check for --no-shm flag
    bool useSharedMemory = true;
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--no-shm") {
            useSharedMemory = false;
            std::cout << "Shared memory caching disabled. Using only gRPC." << std::endl;
        }
    }

    try {
        // Initialize the node with the shared memory flag
        Node node(nodeId, configPath, useSharedMemory);
        node.startServer();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
