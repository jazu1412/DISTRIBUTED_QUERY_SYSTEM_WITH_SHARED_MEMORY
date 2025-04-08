STEPS

jayasurya-murali@jaz MINI-2-CODE 2 % cd build && rm -rf CMakeCache.txt CMakeFiles && export CC=clang && export CXX=clang++ && cmake .. && make


# MINI2 Basecamp - Distributed Query System

This project implements a distributed query system using an overlay network with shared memory optimization for local nodes. The system is designed to run across multiple machines, with nodes communicating via gRPC.

## Overview

The MINI2 Basecamp system consists of 5 nodes (A, B, C, D, E) connected in an overlay network with the following edges:
- A-B
- B-C
- B-D
- C-E
- D-E

Nodes A and B run on the Mac machine (10.0.0.35), while nodes C, D, and E run on the Windows machine (10.0.0.36).

## Features

- Distributed query processing across multiple nodes
- Shared memory optimization for local node communication
- Query result caching for improved performance
- Cross-platform support (macOS and Windows)
- Protocol Buffers for serialization
- gRPC for network communication

## Project Structure

- `proto/`: Protocol Buffer definitions
- `config/`: Configuration files for the overlay network
- `src/`: C++ source code for the nodes
- `python/`: Python client for testing
- `scripts/`: Utility scripts
- `build/`: Build output directory (created during build)

## Setup Guides

This project needs to be set up on two machines:

1. [Mac Setup Guide](mac_setup.md) - For the Mac machine (10.0.0.35)
2. [Windows Setup Guide](windows_setup.md) - For the Windows machine (10.0.0.36)

Follow the appropriate guide for your machine to install dependencies, build the project, and run the nodes.

## Running the System

1. Start nodes A and B on the Mac machine
2. Start nodes C, D, and E on the Windows machine
3. Use the Python client to query the system

For detailed instructions, see the setup guides.

## Query Example

To query records with injury counts between 5 and 15:

```bash
# On Mac
python3 client.py 10.0.0.35:50051 5 15
```

This will send a query to node A, which will propagate through the overlay network to collect matching records from all nodes.

## Architecture

The system uses a distributed query processing approach:

1. A client sends a query to any node in the network
2. The receiving node checks its local cache for the query
3. If not cached, the node filters its local data and forwards the query to its neighbors
4. Neighbors process the query and return their results
5. The original node combines all results and returns them to the client
6. Results are cached for future queries

Nodes on the same machine communicate via shared memory for efficiency, while nodes on different machines use gRPC.

## Dependencies

- C++11 or later
- gRPC
- Protocol Buffers
- nlohmann/json
- CMake (3.14 or later)
- Python 3.6 or later (for the client)
