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

Node::Node(const std::string& nodeId, const std::string& configFile, bool useSharedMemory)
 : id_(nodeId), dataMgr_(nodeId), useSharedMemory_(useSharedMemory) // Pass node ID to DataManager and store useSharedMemory flag
{
    std::cout << getElapsedTime() << " [" << id_ << "] Initializing node with shared memory " 
              << (useSharedMemory_ ? "enabled" : "disabled") << std::endl;
    
    loadConfig(configFile);
    
    auto startLoad = std::chrono::steady_clock::now();
    dataMgr_.loadAll(); // load entire dataset for demonstration
    auto endLoad = std::chrono::steady_clock::now();
    auto loadTime = std::chrono::duration_cast<std::chrono::milliseconds>(endLoad - startLoad).count();
    std::cout << getElapsedTime() << " [" << id_ << "] Loaded data in " << loadTime << "ms" << std::endl;

    // create stubs for neighbors
    for (auto& nbr : neighbors_) {
        auto channel = grpc::CreateChannel(nbr.address, grpc::InsecureChannelCredentials());
        nbr.stub = QueryService::NewStub(channel);
        
        // Only set up shared memory if it's enabled
        if (useSharedMemory_ && nbr.localEdge) {
            // define a stable name
            std::string nm = nbr.id < id_ ? ("shm_"+nbr.id+"_"+id_) : ("shm_"+id_+"_"+nbr.id);
            // Use 1MB as the shared memory size
            ShmCache* ptr = (ShmCache*) openOrCreateShm(nm, 1024*1024);
            if (!ptr) {
                std::cerr << getElapsedTime() << " [" << id_ << "] Failed to create shared memory with " << nbr.id << std::endl;
            } else {
                std::cout << getElapsedTime() << " [" << id_ << "] Successfully created shared memory with " << nbr.id << std::endl;
                localShmMap_[nbr.id] = ptr;
            }
        }
    }
}

void Node::loadConfig(const std::string& file) {
    std::ifstream f(file);
    json j;
    f >> j;
    // find my host/port
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

    // read edges
    for (auto& edge : j["edges"]) {
        if (edge.size() != 2) continue;
        std::string e1 = edge[0].get<std::string>();
        std::string e2 = edge[1].get<std::string>();
        if (e1 == id_ || e2 == id_) {
            std::string nbrId = (e1 == id_) ? e2 : e1;
            // find that node
            std::string nbrHost;
            int nbrPort=0;
            for (auto& nd : j["nodes"]) {
                if (nd["id"].get<std::string>() == nbrId) {
                    nbrHost = nd["host"].get<std::string>();
                    nbrPort = nd["port"].get<int>();
                    break;
                }
            }
            Neighbor nb;
            nb.id = nbrId;
            nb.address = nbrHost + ":" + std::to_string(nbrPort);
            nb.localEdge = (nbrHost == host_);
            neighbors_.push_back(std::move(nb));
        }
    }
}

void Node::startServer() {
    std::string addr = "0.0.0.0:" + std::to_string(port_);
    ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(this);
    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Node " << id_ << " listening on " << addr << "\n";
    server->Wait();
}

bool Node::checkCache(const QueryRequest& req, QueryResponse* out) {
    auto startTime = std::chrono::steady_clock::now();
    std::string queryId = req.query_id();
    
    // // First check in-memory cache (faster)
    // auto it = cache_.find(queryId);
    // if (it != cache_.end()) {
    //     *out = it->second;
    //     auto endTime = std::chrono::steady_clock::now();
    //     auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
    //     std::cout << getElapsedTime() << " [" << id_ << "] in-mem cache hit for " << queryId 
    //               << " (" << duration << "µs)" << std::endl;
    //     return true;
    // }

    // Then check shared memory cache if enabled
    if (useSharedMemory_) {
        for (auto& kv : localShmMap_) {
            std::string neighborId = kv.first;
            ShmCache* shm = kv.second;
            
            // Skip invalid shared memory segments
            if (!shm) {
                continue;
            }
            
            // Skip segments that are being updated
            if (!shm->ready) {
                continue;
            }
            
            // Check if this segment has the query we're looking for
            std::string cachedQueryId(shm->query_id);
            if (queryId == cachedQueryId) {
                try {
                    // Parse the serialized response
                    std::string raw((char*)shm->data, shm->data_size);
                    QueryResponse tmp;
                    if (!tmp.ParseFromString(raw)) {
                        std::cerr << getElapsedTime() << " [" << id_ << "] Failed to parse shared memory data from " 
                                << neighborId << std::endl;
                        continue;
                    }
                    
                    // Copy the response and update in-memory cache
                    *out = tmp;
                    cache_[queryId] = tmp;
                    
                    auto endTime = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
                    std::cout << getElapsedTime() << " [" << id_ << "] SHM cache hit for " << queryId 
                            << " from " << neighborId << " (" << shm->data_size 
                            << " bytes, " << tmp.records_size() << " records, " << duration << "µs)" << std::endl;
                    return true;
                } catch (const std::exception& e) {
                    std::cerr << getElapsedTime() << " [" << id_ << "] Exception while parsing shared memory data: " 
                            << e.what() << std::endl;
                }
            }
        }
    }

    // Not found in any cache
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
    std::cout << getElapsedTime() << " [" << id_ << "] Cache miss for " << queryId 
              << " (" << duration << "µs)" << std::endl;
    return false;
}

void Node::updateCache(const QueryRequest& req, const QueryResponse& r) {
    auto startTime = std::chrono::steady_clock::now();
    std::string queryId = req.query_id();
    
    // Update in-memory cache
    cache_[queryId] = r;
    
    // Also store in shared memory if enabled and we have local neighbors
    if (useSharedMemory_) {
        for (auto& kv : localShmMap_) {
            ShmCache* shm = kv.second;
            if (!shm) continue;
            
            // Set ready to false while updating
            shm->ready = false;
            
            // Clear and set the query ID
            memset(shm->query_id, 0, sizeof(shm->query_id));
            strncpy(shm->query_id, queryId.c_str(), sizeof(shm->query_id)-1);
            
            // Serialize the response
            auto ser = r.SerializeAsString();
            
            // Check if it fits in the shared memory buffer
            if (ser.size() > sizeof(shm->data)) {
                std::cout << getElapsedTime() << " [" << id_ << "] Not enough shm space for cache: " 
                        << ser.size() << " bytes needed, " 
                        << sizeof(shm->data) << " bytes available" << std::endl;
                continue;
            }
            
            // Copy the serialized data to shared memory
            memcpy(shm->data, ser.data(), ser.size());
            shm->data_size = ser.size();
            
            // Mark as ready for reading
            shm->ready = true;
            
            auto endTime = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            std::cout << getElapsedTime() << " [" << id_ << "] Updated shared memory cache for query " 
                    << queryId << " with neighbor " << kv.first 
                    << " (" << ser.size() << " bytes, " << duration << "µs)" << std::endl;
        }
    } else {
        auto endTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
        std::cout << getElapsedTime() << " [" << id_ << "] Updated in-memory cache for query " 
                << queryId << " (" << duration << "µs)" << std::endl;
    }
}

void Node::forwardQuery(const QueryRequest& req,
                        std::vector<QueryResponse>& results,
                        const std::string& sender) {
    auto startTime = std::chrono::steady_clock::now();
    std::string queryId = req.query_id();
    std::vector<std::future<QueryResponse>> futs;
    
    // Get or create the set of neighbors this query has been forwarded to
    auto& forwardedTo = forwardedQueries_[queryId];
    
    // Count how many neighbors we're forwarding to
    int forwardCount = 0;
    
    for (auto& nbr : neighbors_) {
        // Skip the sender and nodes we've already forwarded this query to
        if (nbr.id == sender || forwardedTo.find(nbr.id) != forwardedTo.end()) continue;
        
        // Mark this neighbor as having received this query
        forwardedTo.insert(nbr.id);
        forwardCount++;
        
        std::cout << getElapsedTime() << " [" << id_ << "] Forwarding query " << queryId 
                  << " to neighbor " << nbr.id << std::endl;
        
        // Capture nbr by reference to avoid copying the unique_ptr
        futs.push_back(std::async(std::launch::async, [&, req]() {
            auto rpcStartTime = std::chrono::steady_clock::now();
            std::string nbrId = nbr.id; // Store the ID for logging
            
            QueryRequest subReq = req;
            subReq.set_sender_id(id_);
            QueryResponse nresp;
            grpc::ClientContext ctx;
            
            auto st = nbr.stub->QueryByInjuryRange(&ctx, subReq, &nresp);
            
            auto rpcEndTime = std::chrono::steady_clock::now();
            auto rpcDuration = std::chrono::duration_cast<std::chrono::microseconds>(rpcEndTime - rpcStartTime).count();
            
            if (!st.ok()) {
                std::cerr << getElapsedTime() << " [" << id_ << "] RPC to " << nbr.id 
                          << " failed: " << st.error_message() << " (" << rpcDuration << "µs)" << std::endl;
                return QueryResponse();
            }
            
            std::cout << getElapsedTime() << " [" << id_ << "] RPC to " << nbr.id 
                      << " completed in " << rpcDuration << "µs, received " 
                      << nresp.records_size() << " records" << std::endl;
            
            return nresp;
        }));
    }
    
    if (forwardCount == 0) {
        std::cout << getElapsedTime() << " [" << id_ << "] No neighbors to forward query " 
                  << queryId << " to" << std::endl;
    } else {
        std::cout << getElapsedTime() << " [" << id_ << "] Forwarded query " << queryId 
                  << " to " << forwardCount << " neighbors, waiting for responses..." << std::endl;
    }
    
    // Wait for all responses
    for (auto& f : futs) {
        auto r = f.get();
        if (r.records_size() > 0) {
            results.push_back(r);
        }
    }
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
    
    std::cout << getElapsedTime() << " [" << id_ << "] All " << forwardCount 
              << " neighbors responded in " << duration << "µs, received " 
              << results.size() << " non-empty responses" << std::endl;
}

Status Node::QueryByInjuryRange(ServerContext* ctx,
                                const QueryRequest* req,
                                QueryResponse* resp) {
    auto startTime = std::chrono::steady_clock::now();
    std::string queryId = req->query_id();
    std::cout << getElapsedTime() << " [" << id_ << "] Received query_id: " << queryId  
              << " from sender: " << req->sender_id() << std::endl;

    // Check if we've already processed this query (to break cycles)
    static std::unordered_set<std::string> processedQueries;
    bool alreadyProcessed = processedQueries.find(queryId) != processedQueries.end();

    // 1. check cache
    auto cacheStartTime = std::chrono::steady_clock::now();
    bool cacheHit = checkCache(*req, resp);
    auto cacheEndTime = std::chrono::steady_clock::now();
    auto cacheDuration = std::chrono::duration_cast<std::chrono::microseconds>(cacheEndTime - cacheStartTime).count();
    
    if (cacheHit) {
        auto endTime = std::chrono::steady_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
        std::cout << getElapsedTime() << " [" << id_ << "] Query completed from cache in " 
                  << totalDuration << "µs" << std::endl;
        return Status::OK;
    }

    // If we've already processed this query, just return what we have in cache
    // This shouldn't happen now that we have proper caching, but it's a safeguard
    if (alreadyProcessed) {
        std::cout << getElapsedTime() << " [" << id_ << "] Already processed query " << queryId 
                  << ", skipping redundant processing" << std::endl;
        // Return an empty response since we don't have it in cache
        return Status::OK;
    }

    // Mark this query as processed
    processedQueries.insert(queryId);

    // 2. local filter
    auto filterStartTime = std::chrono::steady_clock::now();
    auto local = dataMgr_.filterByInjuryRange(req->min_injury(), req->max_injury());
    auto filterEndTime = std::chrono::steady_clock::now();
    auto filterDuration = std::chrono::duration_cast<std::chrono::microseconds>(filterEndTime - filterStartTime).count();
    std::cout << getElapsedTime() << " [" << id_ << "] Local filtering completed in " 
              << filterDuration << "µs, found " << local.size() << " records" << std::endl;
    
    QueryResponse combined;
    for (auto& r : local) {
        auto p = combined.add_records();
        p->CopyFrom(r);
    }

    // 3. forward to neighbors, skipping sender
    auto forwardStartTime = std::chrono::steady_clock::now();
    std::vector<QueryResponse> neighborRes;
    forwardQuery(*req, neighborRes, req->sender_id());
    auto forwardEndTime = std::chrono::steady_clock::now();
    auto forwardDuration = std::chrono::duration_cast<std::chrono::microseconds>(forwardEndTime - forwardStartTime).count();
    std::cout << getElapsedTime() << " [" << id_ << "] Query forwarding completed in " 
              << forwardDuration << "µs, received " << neighborRes.size() << " responses" << std::endl;

    // 4. merge with deduplication
    auto mergeStartTime = std::chrono::steady_clock::now();
    std::unordered_set<int> seen_record_ids;
    int totalNeighborRecords = 0;
    int duplicateRecords = 0;
    
    // First add local records and track their IDs
    for (auto& r : local) {
        seen_record_ids.insert(r.record_id());
    }
    
    // Then add records from neighbors, skipping duplicates
    for (auto& nr : neighborRes) {
        totalNeighborRecords += nr.records_size();
        for (auto& rr : nr.records()) {
            // Skip if we've already seen this record ID
            if (seen_record_ids.find(rr.record_id()) != seen_record_ids.end()) {
                duplicateRecords++;
                continue;
            }
            
            // Add the record and mark it as seen
            seen_record_ids.insert(rr.record_id());
            auto p = combined.add_records();
            p->CopyFrom(rr);
        }
    }
    
    auto mergeEndTime = std::chrono::steady_clock::now();
    auto mergeDuration = std::chrono::duration_cast<std::chrono::microseconds>(mergeEndTime - mergeStartTime).count();
    std::cout << getElapsedTime() << " [" << id_ << "] Merge completed in " << mergeDuration 
              << "µs, " << totalNeighborRecords << " neighbor records, " 
              << duplicateRecords << " duplicates removed" << std::endl;

    // 5. cache
    auto cacheUpdateStartTime = std::chrono::steady_clock::now();
    updateCache(*req, combined);
    auto cacheUpdateEndTime = std::chrono::steady_clock::now();
    auto cacheUpdateDuration = std::chrono::duration_cast<std::chrono::microseconds>(cacheUpdateEndTime - cacheUpdateStartTime).count();
    std::cout << getElapsedTime() << " [" << id_ << "] Cache update completed in " 
              << cacheUpdateDuration << "µs" << std::endl;

    *resp = combined;
    
    auto endTime = std::chrono::steady_clock::now();
    auto totalDuration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
    std::cout << getElapsedTime() << " [" << id_ << "] Query completed in " << totalDuration 
              << "µs, returned " << combined.records_size() << " records" << std::endl;
    
    return Status::OK;
}
