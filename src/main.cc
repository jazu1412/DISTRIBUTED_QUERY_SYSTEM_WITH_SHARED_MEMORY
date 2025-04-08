#include <iostream>
#include <string>
#include "node.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ./basecamp_node <NodeID> <overlay.json path>\n";
        return 1;
    }
    std::string nodeId = argv[1];
    std::string configPath = argv[2];

    try {
        Node node(nodeId, configPath);
        node.startServer();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
