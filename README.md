# Distributed Query System with Shared Memory Caching

This project implements a distributed query system using gRPC for communication between nodes and shared memory for efficient caching between processes on the same machine. The system forms an overlay network based on a configuration file, allowing queries to be propagated through the network and results to be aggregated.

## System Architecture

The system consists of multiple nodes forming an overlay network. Each node:
- Runs as a separate process
- Communicates with other nodes via gRPC
- Uses shared memory for efficient caching between nodes on the same machine
- Processes queries for records based on injury count range

### Network Topology

The network topology is defined in `config/overlay.json`. Nodes are identified by IDs (A, B, C, etc.) and are connected by edges. In the current configuration:
- Nodes A and B run on the mac (IP 10.0.0.35)
- Nodes C, D, and E run on the Linux box (IP 10.0.0.16)
- The edges form the following connections: A-B, B-C, B-D, C-E, D-E

## File Structure and Detailed Explanation

### Source Files (src/)

#### main.cc

The entry point of the application.

```cpp
#include <iostream>
#include <string>
#include "node.h"

int main(int argc, char** argv) {
    // Check command line arguments
    if (argc < 3) {
        std::cerr << "Usage: ./basecamp_node <NodeID> <overlay.json path>\n";
        return 1;
    }
    std::string nodeId = argv[1];      // Get node ID from command line
    std::string configPath = argv[2];  // Get config file path from command line

    try {
        // Create a Node instance with the specified ID and config file
        Node node(nodeId, configPath);
        // Start the gRPC server to listen for incoming requests
        node.startServer();
    } catch (const std::exception& e) {
        // Handle any exceptions that occur during initialization or execution
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
```

This file:
1. Parses command line arguments to get the node ID and configuration file path
2. Creates a Node instance with the specified parameters
3. Starts the gRPC server to listen for incoming requests
4. Handles any exceptions that occur during initialization or execution

#### node.h

Defines the Node class and Neighbor structure.

```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include <grpcpp/grpcpp.h>
#include "basecamp.grpc.pb.h"
#include "shm_utils.h"
#include "data_manager.h"

// Represents a neighboring node in the overlay network
struct Neighbor {
    std::string id;                                        // Neighbor's node ID
    std::string address;                                   // Neighbor's address (host:port)
    std::unique_ptr<basecamp::QueryService::Stub> stub;    // gRPC stub for communication
    bool localEdge;                                        // Whether this neighbor is on the same machine
};

// Main node class that implements the gRPC service
class Node final : public basecamp::QueryService::Service {
public:
    // Constructor takes node ID and config file path
    Node(const std::string& nodeId, const std::string& configFile);
    
    // Starts the gRPC server
    void startServer();

    // Implements the gRPC service method for querying by injury range
    ::grpc::Status QueryByInjuryRange(::grpc::ServerContext* ctx,
                                      const basecamp::QueryRequest* req,
                                      basecamp::QueryResponse* resp) override;

private:
    std::string id_;                                                // This node's ID
    std::string host_;                                              // This node's host
    int port_;                                                      // This node's port
    DataManager dataMgr_;                                           // Manages the data records
    std::vector<Neighbor> neighbors_;                               // List of neighboring nodes
    
    std::unordered_map<std::string, basecamp::QueryResponse> cache_;    // In-memory cache for query results
    std::unordered_map<std::string, ShmCache*> localShmMap_;            // Shared memory cache for local nodes

    // Loads the configuration from the specified file
    void loadConfig(const std::string& file);
    
    // Checks if a query result is in the cache (memory or shared memory)
    bool checkCache(const basecamp::QueryRequest& req, basecamp::QueryResponse* out);
    
    // Updates the cache (memory and shared memory) with a query result
    void updateCache(const basecamp::QueryRequest& req, const basecamp::QueryResponse& r);
    
    // Forwards a query to neighboring nodes and collects results
    void forwardQuery(const basecamp::QueryRequest& req,
                      std::vector<basecamp::QueryResponse>& results,
                      const std::string& sender);
};
```

This file defines:
1. The `Neighbor` structure representing a neighboring node in the overlay network
2. The `Node` class that implements the gRPC service for handling queries
3. Methods for loading configuration, checking and updating caches, and forwarding queries

#### node.cc

Implements the Node class functionality.

```cpp
#include "node.h"
#include <fstream>
#include <iostream>
#include <future>
#include <nlohmann/json.hpp>
#include <grpcpp/server_builder.h>
#include <grpcpp/security/server_credentials.h>

using json = nlohmann::json;
using namespace basecamp;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

// Constructor initializes the node with its ID and configuration
Node::Node(const std::string& nodeId, const std::string& configFile)
 : id_(nodeId)
{
    // Load configuration from the specified file
    loadConfig(configFile);
    // Load all data records
    dataMgr_.loadAll();

    // Create gRPC stubs for each neighbor and set up shared memory for local neighbors
    for (auto& nbr : neighbors_) {
        // Create a gRPC channel to the neighbor
        auto channel = grpc::CreateChannel(nbr.address, grpc::InsecureChannelCredentials());
        // Create a gRPC stub for the neighbor
        nbr.stub = QueryService::NewStub(channel);
        
        // If the neighbor is on the same machine, set up shared memory
        if (nbr.localEdge) {
            // Create a stable name for the shared memory segment
            std::string nm = nbr.id < id_ ? ("shm_"+nbr.id+"_"+id_) : ("shm_"+id_+"_"+nbr.id);
            // Open or create the shared memory segment
            ShmCache* ptr = (ShmCache*) openOrCreateShm(nm, 1024*1024);
            // Store the shared memory pointer
            localShmMap_[nbr.id] = ptr;
        }
    }
}

// Loads the configuration from the specified file
void Node::loadConfig(const std::string& file) {
    // Open the configuration file
    std::ifstream f(file);
    json j;
    // Parse the JSON configuration
    f >> j;
    
    // Find this node's host and port
    bool found = false;
    for (auto& nd : j["nodes"]) {
        if (nd["id"].get<std::string>() == id_) {
            host_ = nd["host"].get<std::string>();
            port_ = nd["port"].get<int>();
            found = true;
            break;
        }
    }
    if (!found) {
        throw std::runtime_error("Node " + id_ + " not found in config");
    }

    // Read edges to find neighbors
    for (auto& edge : j["edges"]) {
        if (edge.size() != 2) continue;
        std::string e1 = edge[0].get<std::string>();
        std::string e2 = edge[1].get<std::string>();
        
        // If this node is part of the edge
        if (e1 == id_ || e2 == id_) {
            // Get the neighbor's ID
            std::string nbrId = (e1 == id_) ? e2 : e1;
            
            // Find the neighbor's host and port
            std::string nbrHost;
            int nbrPort=0;
            for (auto& nd : j["nodes"]) {
                if (nd["id"].get<std::string>() == nbrId) {
                    nbrHost = nd["host"].get<std::string>();
                    nbrPort = nd["port"].get<int>();
                    break;
                }
            }
            
            // Create a Neighbor object
            Neighbor nb;
            nb.id = nbrId;
            nb.address = nbrHost + ":" + std::to_string(nbrPort);
            // Check if the neighbor is on the same machine
            nb.localEdge = (nbrHost == host_);
            // Add the neighbor to the list
            neighbors_.push_back(std::move(nb));
        }
    }
}

// Starts the gRPC server
void Node::startServer() {
    // Create the server address
    std::string addr = "0.0.0.0:" + std::to_string(port_);
    // Create a server builder
    ServerBuilder builder;
    // Add a listening port with insecure credentials
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    // Register this service
    builder.RegisterService(this);
    // Build and start the server
    std::unique_ptr<Server> server(builder.BuildAndStart());
    // Log that the server is running
    std::cout << "Node " << id_ << " listening on " << addr << "\n";
    // Wait for the server to shutdown
    server->Wait();
}

// Checks if a query result is in the cache (memory or shared memory)
bool Node::checkCache(const QueryRequest& req, QueryResponse* out) {
    // Check in-memory cache
    auto it = cache_.find(req.query_id());
    if (it != cache_.end()) {
        *out = it->second;
        std::cout << "[" << id_ << "] in-mem cache hit for " << req.query_id() << "\n";
        return true;
    }

    // Check shared memory cache
    for (auto& kv : localShmMap_) {
        ShmCache* shm = kv.second;
        if (!shm) continue;
        if (!shm->ready) continue;
        
        // If the query ID matches
        if (req.query_id() == std::string(shm->query_id)) {
            // Parse the serialized response
            std::string raw((char*)shm->data, shm->data_size);
            QueryResponse tmp;
            tmp.ParseFromString(raw);
            // Copy the response
            *out = tmp;
            // Update the in-memory cache
            cache_[req.query_id()] = tmp;
            std::cout << "["<<id_<<"] SHM cache hit for "<<req.query_id()<<" from "<<kv.first<<"\n";
            return true;
        }
    }

    return false;
}

// Updates the cache (memory and shared memory) with a query result
void Node::updateCache(const QueryRequest& req, const QueryResponse& r) {
    // Update in-memory cache
    cache_[req.query_id()] = r;

    // Update shared memory cache for local neighbors
    for (auto& kv : localShmMap_) {
        ShmCache* shm = kv.second;
        if (!shm) continue;
        
        // Mark as not ready while updating
        shm->ready = false;
        // Clear the query ID
        memset(shm->query_id, 0, sizeof(shm->query_id));
        // Set the query ID
        strncpy(shm->query_id, req.query_id().c_str(), sizeof(shm->query_id)-1);
        
        // Serialize the response
        auto ser = r.SerializeAsString();
        if (ser.size() > sizeof(shm->data)) {
            std::cout << "["<<id_<<"] Not enough shm space for cache\n";
            continue;
        }
        
        // Copy the serialized data
        memcpy(shm->data, ser.data(), ser.size());
        // Set the data size
        shm->data_size = ser.size();
        // Mark as ready
        shm->ready = true;
    }
}

// Forwards a query to neighboring nodes and collects results
void Node::forwardQuery(const QueryRequest& req,
                        std::vector<QueryResponse>& results,
                        const std::string& sender) {
    // Create a vector of futures for asynchronous calls
    std::vector<std::future<QueryResponse>> futs;
    
    // For each neighbor
    for (auto& nbr : neighbors_) {
        // Skip the sender to avoid loops
        if (nbr.id == sender) continue;
        
        std::cout << "[" << id_ << "] Forwarding query " << req.query_id() << " to neighbor " << nbr.id << std::endl;
        
        // Create an asynchronous task
        futs.push_back(std::async(std::launch::async, [&, req]() {
            // Create a new request with this node as the sender
            QueryRequest subReq = req;
            subReq.set_sender_id(id_);
            
            // Create a response object
            QueryResponse nresp;
            // Create a client context
            grpc::ClientContext ctx;
            
            // Call the gRPC method
            auto st = nbr.stub->QueryByInjuryRange(&ctx, subReq, &nresp);
            if (!st.ok()) {
                std::cerr << "["<<id_<<"] RPC to "<<nbr.id<<" failed: "<<st.error_message()<<"\n";
                return QueryResponse();
            }
            return nresp;
        }));
    }
    
    // Collect results from all futures
    for (auto& f : futs) {
        auto r = f.get();
        std::cout << "[" << id_ << "] Received response with " << r.records_size() << " records from a neighbor" << std::endl;
        results.push_back(r);
    }
}

// Implements the gRPC service method for querying by injury range
Status Node::QueryByInjuryRange(ServerContext* ctx,
                                const QueryRequest* req,
                                QueryResponse* resp) {
    std::cout << "[" << id_ << "] Received query_id: " << req->query_id()  << " from sender: " << req->sender_id() << std::endl;

    // 1. Check cache
    if (checkCache(*req, resp)) {
        return Status::OK;
    }

    // 2. Filter local data
    auto local = dataMgr_.filterByInjuryRange(req->min_injury(), req->max_injury());
    QueryResponse combined;
    for (auto& r : local) {
        auto p = combined.add_records();
        p->CopyFrom(r);
    }

    // 3. Forward to neighbors, skipping sender
    std::vector<QueryResponse> neighborRes;
    forwardQuery(*req, neighborRes, req->sender_id());

    // 4. Merge results
    for (auto& nr : neighborRes) {
        for (auto& rr : nr.records()) {
            auto p = combined.add_records();
            p->CopyFrom(rr);
        }
    }

    // 5. Update cache
    updateCache(*req, combined);

    // Return the combined response
    *resp = combined;
    return Status::OK;
}
```

This file implements:
1. Node initialization and configuration loading
2. gRPC server setup and execution
3. Cache management (in-memory and shared memory)
4. Query forwarding to neighboring nodes
5. Query processing and result aggregation

#### shm_utils.h

Defines utilities for shared memory operations.

```cpp
#pragma once
#include <cstdint>
#include <cstddef>  // for size_t
#include <string>   // for std::string
#include <atomic>

#ifdef _WIN32
#include <Windows.h>
#endif

// Structure for shared memory cache
struct ShmCache {
    std::atomic<bool> ready;       // Flag indicating if the cache is ready to be read
    char query_id[64];             // Query ID for the cached result
    size_t data_size;              // Size of the cached data
    unsigned char data[1024*512];  // Buffer for cached data (512KB)
};

// Opens or creates a shared memory segment with the specified name and size
void* openOrCreateShm(const std::string& name, size_t size);
```

This file defines:
1. The `ShmCache` structure for storing cached query results in shared memory
2. A function for opening or creating shared memory segments

#### shm_utils.cc

Implements shared memory utilities.

```cpp
#include "shm_utils.h"
#include <iostream>
#include <cstring>
#ifdef __APPLE__
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#endif

// Opens or creates a shared memory segment with the specified name and size
void* openOrCreateShm(const std::string& name, size_t size){
#ifdef _WIN32
    // Windows implementation
    HANDLE hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL,
                        PAGE_READWRITE, 0, (DWORD)size, name.c_str());
    if (!hMapFile) {
        std::cerr << "CreateFileMapping failed." << std::endl;
        return nullptr;
    }
    void* p = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!p) {
        std::cerr << "MapViewOfFile failed." << std::endl;
    }
    ShmCache* sc = (ShmCache*) p;
    sc->ready = false;
    sc->data_size = 0;
    memset(sc->query_id, 0, sizeof(sc->query_id));
    return p;
#else
    // POSIX implementation (Linux, macOS)
    std::string realName = "/" + name;
    int fd = shm_open(realName.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd<0) {
        perror("shm_open");
        return nullptr;
    }
    ftruncate(fd, size);
    void* p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (p==MAP_FAILED) {
        perror("mmap");
        return nullptr;
    }
    ShmCache* sc = (ShmCache*) p;
    sc->ready = false;
    sc->data_size=0;
    memset(sc->query_id, 0, sizeof(sc->query_id));
    return p;
#endif
}
```

This file implements:
1. Platform-specific shared memory operations for Windows and POSIX systems
2. Initialization of shared memory segments

#### data_manager.h

Defines the DataManager class for managing data records.

```cpp
#pragma once
#include <vector>
#include "basecamp.pb.h"

// Manages data records and provides filtering functionality
class DataManager {
public:
    // Loads all data records
    void loadAll();
    
    // Filters records by injury count range
    std::vector<basecamp::Record> filterByInjuryRange(int minI, int maxI);
private:
    // All data records
    std::vector<basecamp::Record> allRecs_;
};
```

This file defines:
1. The `DataManager` class for managing data records
2. Methods for loading and filtering records

#### data_manager.cc

Implements the DataManager class functionality.

```cpp
#include "data_manager.h"
#include <iostream>

// Loads all data records (dummy implementation for demonstration)
void DataManager::loadAll() {
    // Create 20 dummy records
    for (int i=0; i<20; i++) {
        basecamp::Record r;
        r.set_record_id(i);
        r.set_borough((i%2==0)?"QUEENS":"BROOKLYN");
        r.set_injury_count(i);
        r.set_other_fields("Extra #" + std::to_string(i));
        allRecs_.push_back(r);
    }
}

// Filters records by injury count range
std::vector<basecamp::Record> DataManager::filterByInjuryRange(int minI, int maxI) {
    std::vector<basecamp::Record> out;
    // For each record
    for (auto& r: allRecs_) {
        // If the injury count is within the specified range
        if (r.injury_count()>=minI && r.injury_count()<=maxI) {
            // Add the record to the output
            out.push_back(r);
        }
    }
    return out;
}
```

This file implements:
1. Loading of dummy data records for demonstration
2. Filtering of records based on injury count range

### Protocol Buffer Files (proto/)

#### basecamp.proto

Defines the protocol buffer messages and service.

```proto
syntax = "proto3";

package basecamp;

// A single record with some numeric fields.
message Record {
    int32 record_id = 1;
    string borough = 2;         // e.g. "QUEENS", "BRONX" etc.
    int32 injury_count = 3;     // for demonstration
    string other_fields = 4;
}

// The query request structure with a query_id and range filters
message QueryRequest {
    string query_id = 1;   // unique ID for dedup caching
    int32 min_injury = 2;
    int32 max_injury = 3;
    string sender_id = 4;  // to avoid sending back immediately
}

// A list of records in response
message QueryResponse {
    repeated Record records = 1;
}

service QueryService {
    // Asynchronous or sync gRPC method to get records in [min_injury, max_injury]
    rpc QueryByInjuryRange(QueryRequest) returns (QueryResponse);
}
```

This file defines:
1. The `Record` message representing a data record
2. The `QueryRequest` message for querying records by injury range
3. The `QueryResponse` message for returning query results
4. The `QueryService` service with a method for querying by injury range

### Python Client (python/)

#### client.py

Implements a Python client for querying the system.

```python
# usage: python client.py <host:port> <min_injury> <max_injury>
import sys
import grpc
import basecamp_pb2
import basecamp_pb2_grpc
import uuid 

if __name__ == "__main__":
    # Check command line arguments
    if len(sys.argv) < 4:
        print("Usage: python client.py <host:port> <min> <max>")
        sys.exit(1)

    # Parse command line arguments
    target = sys.argv[1]         # host:port
    mini = int(sys.argv[2])      # min injury
    maxi = int(sys.argv[3])      # max injury

    # Generate a unique query ID
    query_id = str(uuid.uuid4())[:8]  # shortens the UUID to 8 characters

    # Create a gRPC channel and stub
    channel = grpc.insecure_channel(target)
    stub = basecamp_pb2_grpc.QueryServiceStub(channel)

    # Create a query request
    req = basecamp_pb2.QueryRequest(
        query_id=query_id,
        min_injury=mini,
        max_injury=maxi,
        sender_id="client"
    )

    # Send the query and get the response
    print(f"Sending query_id: {query_id}")
    resp = stub.QueryByInjuryRange(req)
    
    # Print the results
    print(f"Got {len(resp.records)} records from overlay.")
    for r in resp.records:
        print(r)
```

This file implements:
1. Command line argument parsing
2. gRPC client setup
3. Query creation and submission
4. Result processing and display

### Build Scripts (scripts/)

#### build_proto.sh

Generates C++ code from protocol buffer definitions.

```bash
#!/usr/bin/env bash

# Generate C++ code from protocol buffer definitions
protoc -I=../proto \
    --cpp_out=../build \
    --grpc_out=../build \
    --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` \
    ../proto/basecamp.proto

echo "Protos generated into ../build/"
```

This script:
1. Runs the Protocol Buffer compiler (protoc) with the gRPC plugin
2. Generates C++ code for messages and services
3. Outputs the generated files to the build directory

### Configuration Files (config/)

#### overlay.json

Defines the overlay network topology.

```json
{
  "nodes": [
    { "id": "A", "host": "10.0.0.35", "port": 50051 },
    { "id": "B", "host": "10.0.0.35", "port": 50052 },
    { "id": "C", "host": "10.0.0.16", "port": 50053 },
    { "id": "D", "host": "10.0.0.16", "port": 50054 },
    { "id": "E", "host": "10.0.0.16", "port": 50055 }
  ],
  "edges": [
    ["A", "B"],
    ["B", "C"],
    ["B", "D"],
    ["C", "E"],
    ["D", "E"]
  ]
}
```

This file defines:
1. The nodes in the overlay network with their IDs, hosts, and ports
2. The edges connecting the nodes

### Build Configuration (CMakeLists.txt)

Configures the build process.

```cmake
cmake_minimum_required(VERSION 3.14)
# Disable OpenMP
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument")
project(Mini2BasecampAltOverlay CXX)

# Use C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)   # Use CMake config for Abseil

# Include directories
include_directories(
    ${GRPC_INCLUDE_DIRS}
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}  # location of generated *.pb.h
    include
)

# Link directories
link_directories(
    ${GRPC_LIBRARY_DIRS}
    ${PROTOBUF_LIBRARY_DIRS}
)

# Add executable
add_executable(basecamp_node
    src/main.cc
    src/node.cc
    src/shm_utils.cc
    src/data_manager.cc
    ${CMAKE_CURRENT_BINARY_DIR}/basecamp.pb.cc
    ${CMAKE_CURRENT_BINARY_DIR}/basecamp.grpc.pb.cc
)

# Link libraries – must include absl sub-libraries used by gRPC/Protobuf
target_link_libraries(basecamp_node
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    nlohmann_json::nlohmann_json

    # core abseil libs:
    absl::base
    absl::strings
    absl::time
    absl::synchronization

    # logging + debugging used by recent gRPC/Protobuf
    absl::log
    absl::cord
    absl::symbolize
    absl::stacktrace
    absl::debugging
)

# Filter out OpenMP flags from CFLAGS_OTHER
set(FILTERED_GRPC_CFLAGS)
foreach(FLAG ${GRPC_CFLAGS_OTHER})
    if(NOT "${FLAG}" MATCHES "-fopenmp")
        list(APPEND FILTERED_GRPC_CFLAGS ${FLAG})
    endif()
endforeach()

set(FILTERED_PROTOBUF_CFLAGS)
foreach(FLAG ${PROTOBUF_CFLAGS_OTHER})
    if(NOT "${FLAG}" MATCHES "-fopenmp")
        list(APPEND FILTERED_PROTOBUF_CFLAGS ${FLAG})
    endif()
endforeach()

# Add compile options without OpenMP
target_compile_options(basecamp_node PRIVATE
    ${FILTERED_GRPC_CFLAGS}
    ${FILTERED_PROTOBUF_CFLAGS}
)
```

This file configures:
1. The C++ standard (C++17)
2. Required packages (gRPC, Protobuf, nlohmann_json, Abseil)
3. Include and link directories
4. The executable target and its source files
5. Library dependencies
6. Compiler options (filtering out OpenMP flags)

## Building and Running

### Building the Project

1. Generate the Protocol Buffer and gRPC stubs:
   ```bash
   cd scripts
   ./build_proto.sh
   ```

2. Build the project using CMake:
   ```bash
   mkdir -p build
   cd build
   cmake ..
   make
   ```

### Running the System

1. Start the nodes on the mac (10.0.0.35):
   ```bash
   ./build/basecamp_node A config/overlay.json
   ./build/basecamp_node B config/overlay.json
   ```

2. Start the nodes on the Linux box (10.0.0.16):
   ```bash
   ./build/basecamp_node C config/overlay.json
   ./build/basecamp_node D config/overlay.json
   ./build/basecamp_node E config/overlay.json
   ```

3. Run the Python client to query the system:
   ```bash
   cd python
   python client.py 10.0.0.35:50051 5 15
   ```

## System Features

1. **Distributed Query Processing**: Queries are propagated through the overlay network and results are aggregated.
2. **Caching**: Results are cached in memory and in shared memory for efficient retrieval.
3. **Shared Memory Optimization**: Nodes on the same machine use shared memory for efficient communication.
4. **Asynchronous Communication**: Queries are forwarded to neighbors asynchronously for better performance.
5. **Configurable Topology**: The overlay network topology is defined in a configuration file.

## Dependencies

- gRPC: For communication between nodes
- Protocol Buffers: For message serialization
- nlohmann_json: For JSON parsing
- Abseil: For various utilities
