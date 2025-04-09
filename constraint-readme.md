# Constraint Analysis for Distributed Query System

This document analyzes how the distributed query system implementation addresses the specified constraints.

## Constraint 1: Two-way Communication Without Request-Response Style

**Constraint:** How do you create a two-way communication without using a request-response style? For example, a client sends a query to A. A then attempts to answer the question by contacting its peers (B,C,D, and E). The results are gathered and the client is returned the results. It is expected the peers are independently contributing to the reply (data). Each peer has a subset of the data space (no sharing, no replication, no mirroring). Lastly, only A is allowed to reply to the client through gRPC.

**Implementation:**

The system implements this constraint through a forwarding mechanism that avoids the traditional request-response pattern:

1. **Query Propagation:**
   - When node A receives a query from the client, it doesn't directly request data from specific peers.
   - Instead, it forwards the query to its immediate neighbors (in this case, node B).
   - Each node that receives the query:
     - Contributes its own local data that matches the query criteria
     - Forwards the query to its neighbors (except the sender)
     - This creates a propagation pattern where the query flows through the entire overlay network

2. **Asynchronous Processing:**
   - The `forwardQuery` method in `node.cc` uses asynchronous processing with `std::async` to forward queries to neighbors:
   ```cpp
   futs.push_back(std::async(std::launch::async, [&, req]() {
       QueryRequest subReq = req;
       subReq.set_sender_id(id_);
       QueryResponse nresp;
       grpc::ClientContext ctx;
       auto st = nbr.stub->QueryByInjuryRange(&ctx, subReq, &nresp);
       // ...
       return nresp;
   }));
   ```
   - This allows each node to process queries independently and in parallel.

3. **Independent Data Contribution:**
   - Each node maintains its own dataset through the `DataManager` class.
   - When a node receives a query, it filters its local data using `dataMgr_.filterByInjuryRange()`.
   - The filtered local data is combined with results from neighbors.
   - This ensures each node independently contributes its own subset of data.

4. **Single Client Response Point:**
   - Only node A (the entry point) communicates with the client.
   - Other nodes (B, C, D, E) never directly respond to the client.
   - The client only interacts with node A through the gRPC interface.

5. **Loop Prevention:**
   - The `sender_id` field in the `QueryRequest` prevents infinite loops in the network.
   - Each node skips forwarding to the sender of the query:
   ```cpp
   if (nbr.id == sender) continue;
   ```

This approach creates a two-way communication pattern that differs from traditional request-response:
- Instead of A directly requesting data from specific peers, the query propagates through the network.
- Each node contributes data independently without being directly requested.
- Results flow back through the network to node A, which aggregates them for the client.

## Constraint 2: No Hard-Coded Connections

**Constraint:** It is not intended for this mini to have processes discover each other dynamically. Rather a mapping can exist that provides guidance to each process' edges (connections). This however, should not be hard coded within the code.

**Implementation:**

The system uses external configuration rather than hard-coded connections:

1. **External Configuration File:**
   - The network topology is defined in `config/overlay.json`.
   - This file specifies all nodes and their connections (edges).
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

2. **Dynamic Configuration Loading:**
   - The configuration is loaded at runtime in the `loadConfig` method:
   ```cpp
   void Node::loadConfig(const std::string& file) {
       std::ifstream f(file);
       json j;
       f >> j;
       // ...
   }
   ```
   - This allows the network topology to be changed without modifying the code.

3. **Command-Line Configuration:**
   - The configuration file path is provided as a command-line argument:
   ```cpp
   int main(int argc, char** argv) {
       if (argc < 3) {
           std::cerr << "Usage: ./basecamp_node <NodeID> <overlay.json path>\n";
           return 1;
       }
       std::string nodeId = argv[1];
       std::string configPath = argv[2];
       // ...
   }
   ```
   - This allows different configurations to be used without recompiling.

4. **Dynamic Neighbor Discovery:**
   - Each node discovers its neighbors by parsing the edges in the configuration:
   ```cpp
   for (auto& edge : j["edges"]) {
       if (edge.size() != 2) continue;
       std::string e1 = edge[0].get<std::string>();
       std::string e2 = edge[1].get<std::string>();
       if (e1 == id_ || e2 == id_) {
           std::string nbrId = (e1 == id_) ? e2 : e1;
           // ...
       }
   }
   ```
   - This approach allows for flexible network topologies without hard-coding.

This implementation satisfies the constraint by using an external configuration file to define the network topology, rather than hard-coding connections in the source code.

## Constraint 3: Overlays and Transactionless Caching

**Constraint:** The challenge for mini 2 is to explore overlays and research how transactionless caching can be used.

**Implementation:**

The system implements both overlay networks and transactionless caching:

1. **Overlay Network:**
   - The system creates a logical network topology on top of the physical network.
   - Nodes are connected according to the edges defined in the configuration.
   - The overlay is implemented through gRPC connections between nodes.
   - The specific topology in the example is:
     ```
     A -- B -- C
          |    |
          D -- E
     ```
   - This overlay structure allows for flexible routing and data aggregation.

2. **Transactionless Caching:**
   - The system implements caching without traditional transaction semantics:
     - No locks or transactions are used for cache updates
     - No complex consistency protocols are implemented
     - Cache updates are simple overwrites

   - **In-Memory Cache:**
     ```cpp
     std::unordered_map<std::string, basecamp::QueryResponse> cache_;
     ```
     - Each node maintains an in-memory cache of query results.
     - Results are indexed by query ID for fast lookup.

   - **Shared Memory Cache:**
     ```cpp
     std::unordered_map<std::string, ShmCache*> localShmMap_;
     ```
     - Nodes on the same machine share cache data through shared memory.
     - The `ShmCache` structure includes an atomic flag for basic synchronization:
     ```cpp
     struct ShmCache {
         std::atomic<bool> ready;
         char query_id[64];
         size_t data_size;
         unsigned char data[1024*512];
     };
     ```

3. **Cache Operations:**
   - **Cache Checking:**
     ```cpp
     bool Node::checkCache(const QueryRequest& req, QueryResponse* out) {
         // Check in-memory cache
         auto it = cache_.find(req.query_id());
         if (it != cache_.end()) {
             *out = it->second;
             return true;
         }
         // Check shared memory cache
         // ...
     }
     ```

   - **Cache Updating:**
     ```cpp
     void Node::updateCache(const QueryRequest& req, const QueryResponse& r) {
         // Update in-memory cache
         cache_[req.query_id()] = r;
         // Update shared memory cache
         // ...
     }
     ```

   - **Shared Memory Optimization:**
     - Nodes on the same machine use shared memory for efficient cache sharing.
     - The system detects local edges during configuration loading:
     ```cpp
     nb.localEdge = (nbrHost == host_);
     ```
     - For local edges, shared memory is set up:
     ```cpp
     if (nbr.localEdge) {
         std::string nm = nbr.id < id_ ? ("shm_"+nbr.id+"_"+id_) : ("shm_"+id_+"_"+nbr.id);
         ShmCache* ptr = (ShmCache*) openOrCreateShm(nm, 1024*1024);
         localShmMap_[nbr.id] = ptr;
     }
     ```

This implementation satisfies the constraint by exploring overlay networks for distributed query processing and implementing transactionless caching through both in-memory and shared memory mechanisms.

## Constraint 4: Focus on Overlays and Cache Coherency

**Constraint:** The first (overlays) is where the bulk of the work resides, and is dependent upon the second (cache coherency).

**Implementation:**

The system focuses on overlay networks with cache coherency as a supporting feature:

1. **Overlay Network Implementation:**
   - The overlay network is the primary focus of the implementation.
   - It defines how nodes are connected and how queries propagate through the network.
   - The overlay structure determines:
     - How queries flow through the network
     - How results are aggregated
     - The efficiency of the overall system

2. **Cache Coherency Approach:**
   - The system uses a simplified approach to cache coherency:
     - Each query has a unique ID (`query_id`)
     - Results for a query are cached using this ID as the key
     - Cache entries are never invalidated (for simplicity)
     - Cache updates are simple overwrites

   - **Atomic Ready Flag:**
     - The `ready` flag in `ShmCache` is atomic to prevent race conditions:
     ```cpp
     std::atomic<bool> ready;
     ```
     - It's set to false during updates and true when the update is complete:
     ```cpp
     shm->ready = false;
     // Update cache data...
     shm->ready = true;
     ```

   - **Cache Checking Protocol:**
     - Before reading from shared memory, the system checks if the cache is ready:
     ```cpp
     if (!shm->ready) continue;
     ```
     - This ensures that only complete cache entries are read.

3. **Dependency Relationship:**
   - The overlay network depends on cache coherency for efficient operation:
     - Without caching, every query would need to traverse the entire network
     - With caching, repeated queries can be answered immediately
     - Local shared memory caching reduces network traffic between nodes on the same machine

   - The cache coherency mechanism, while simplified, supports the overlay network by:
     - Reducing redundant query processing
     - Minimizing network traffic
     - Improving response times for repeated queries

This implementation satisfies the constraint by focusing on the overlay network implementation while using cache coherency as a supporting feature that enhances the efficiency of the overlay.

## Summary

The distributed query system successfully addresses all four constraints:

1. It implements two-way communication without a traditional request-response style by using query propagation through an overlay network.

2. It avoids hard-coded connections by using an external configuration file to define the network topology.

3. It explores overlay networks and transactionless caching through a flexible network structure and simple caching mechanisms.

4. It focuses on the overlay network implementation with cache coherency as a supporting feature.

The system demonstrates how a distributed query system can be implemented using overlay networks and simple caching mechanisms to efficiently process queries across multiple nodes.
