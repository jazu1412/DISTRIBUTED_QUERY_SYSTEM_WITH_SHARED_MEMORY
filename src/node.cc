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

Node::Node(const std::string& nodeId, const std::string& configFile)
 : id_(nodeId)
{
    loadConfig(configFile);
    dataMgr_.loadAll(); // load entire dataset for demonstration

    // create stubs for neighbors
    for (auto& nbr : neighbors_) {
        auto channel = grpc::CreateChannel(nbr.address, grpc::InsecureChannelCredentials());
        nbr.stub = QueryService::NewStub(channel);
        if (nbr.localEdge) {
            // define a stable name
            std::string nm = nbr.id < id_ ? ("shm_"+nbr.id+"_"+id_) : ("shm_"+id_+"_"+nbr.id);
            // Use 1MB as the shared memory size
            ShmCache* ptr = (ShmCache*) openOrCreateShm(nm, 1024*1024);
            if (!ptr) {
                std::cerr << "[" << id_ << "] Failed to create shared memory with " << nbr.id << std::endl;
            } else {
                std::cout << "[" << id_ << "] Successfully created shared memory with " << nbr.id << std::endl;
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
    std::string queryId = req.query_id();
    
    // First check in-memory cache (faster)
    // auto it = cache_.find(queryId);
    // if (it != cache_.end()) {
    //     *out = it->second;
    //     std::cout << "[" << id_ << "] in-mem cache hit for " << queryId << "\n";
    //     return true;
    // }

    // Then check shared memory cache
    for (auto& kv : localShmMap_) {
        std::string neighborId = kv.first;
        ShmCache* shm = kv.second;
        
        // Skip invalid shared memory segments
        if (!shm) {
            std::cout << "[" << id_ << "] Shared memory with " << neighborId << " is null" << std::endl;
            continue;
        }
        
        // Skip segments that are being updated
        if (!shm->ready) {
            std::cout << "[" << id_ << "] Shared memory with " << neighborId << " is not ready" << std::endl;
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
                    std::cerr << "[" << id_ << "] Failed to parse shared memory data from " 
                              << neighborId << std::endl;
                    continue;
                }
                
                // Copy the response and update in-memory cache
                *out = tmp;
                cache_[queryId] = tmp;
                
                std::cout << "[" << id_ << "] SHM cache hit for " << queryId 
                          << " from " << neighborId << " (" << shm->data_size 
                          << " bytes, " << tmp.records_size() << " records)" << std::endl;
                return true;
            } catch (const std::exception& e) {
                std::cerr << "[" << id_ << "] Exception while parsing shared memory data: " 
                          << e.what() << std::endl;
            }
        }
    }

    // Not found in any cache
    return false;
}

void Node::updateCache(const QueryRequest& req, const QueryResponse& r) {
    std::string queryId = req.query_id();
    // Update in-memory cache
    cache_[queryId] = r;

    // Also store in shared memory if we have local neighbors
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
            std::cout << "[" << id_ << "] Not enough shm space for cache: " 
                      << ser.size() << " bytes needed, " 
                      << sizeof(shm->data) << " bytes available" << std::endl;
            continue;
        }
        
        // Copy the serialized data to shared memory
        memcpy(shm->data, ser.data(), ser.size());
        shm->data_size = ser.size();
        
        // Mark as ready for reading
        shm->ready = true;
        
        std::cout << "[" << id_ << "] Updated shared memory cache for query " 
                  << queryId << " with neighbor " << kv.first 
                  << " (" << ser.size() << " bytes)" << std::endl;
    }
}

void Node::forwardQuery(const QueryRequest& req,
                        std::vector<QueryResponse>& results,
                        const std::string& sender) {
    std::vector<std::future<QueryResponse>> futs;
    
    // Get or create the set of neighbors this query has been forwarded to
    auto& forwardedTo = forwardedQueries_[req.query_id()];
    
    for (auto& nbr : neighbors_) {
        // Skip the sender and nodes we've already forwarded this query to
        if (nbr.id == sender || forwardedTo.find(nbr.id) != forwardedTo.end()) continue;
        
        // Mark this neighbor as having received this query
        forwardedTo.insert(nbr.id);
        
        std::cout << "[" << id_ << "] Forwarding query " << req.query_id() << " to neighbor " << nbr.id << std::endl;
        futs.push_back(std::async(std::launch::async, [&, req]() {
            QueryRequest subReq = req;
            subReq.set_sender_id(id_);
            QueryResponse nresp;
            grpc::ClientContext ctx;
            auto st = nbr.stub->QueryByInjuryRange(&ctx, subReq, &nresp);
            if (!st.ok()) {
                std::cerr << "["<<id_<<"] RPC to "<<nbr.id<<" failed: "<<st.error_message()<<"\n";
                return QueryResponse();
            }
            return nresp;
        }));
    }
    
    for (auto& f : futs) {
        auto r = f.get();
        std::cout << "[" << id_ << "] Received response with " << r.records_size() << " records from a neighbor" << std::endl;
        results.push_back(r);
    }
}

Status Node::QueryByInjuryRange(ServerContext* ctx,
                                const QueryRequest* req,
                                QueryResponse* resp) {
    std::string queryId = req->query_id();
    std::cout << "[" << id_ << "] Received query_id: " << queryId  << " from sender: " << req->sender_id() << std::endl;

    // Check if we've already processed this query (to break cycles)
    static std::unordered_set<std::string> processedQueries;
    bool alreadyProcessed = processedQueries.find(queryId) != processedQueries.end();

    // 1. check cache
    if (checkCache(*req, resp)) {
        return Status::OK;
    }

    // If we've already processed this query, just return what we have in cache
    // This shouldn't happen now that we have proper caching, but it's a safeguard
    if (alreadyProcessed) {
        std::cout << "[" << id_ << "] Already processed query " << queryId << ", skipping redundant processing" << std::endl;
        // Return an empty response since we don't have it in cache
        return Status::OK;
    }

    // Mark this query as processed
    processedQueries.insert(queryId);

    // 2. local filter
    auto local = dataMgr_.filterByInjuryRange(req->min_injury(), req->max_injury());
    QueryResponse combined;
    for (auto& r : local) {
        auto p = combined.add_records();
        p->CopyFrom(r);
    }

    // 3. forward to neighbors, skipping sender
    std::vector<QueryResponse> neighborRes;
    forwardQuery(*req, neighborRes, req->sender_id());

    // 4. merge with deduplication
    std::unordered_set<int> seen_record_ids;
    
    // First add local records and track their IDs
    for (auto& r : local) {
        seen_record_ids.insert(r.record_id());
    }
    
    // Then add records from neighbors, skipping duplicates
    for (auto& nr : neighborRes) {
        for (auto& rr : nr.records()) {
            // Skip if we've already seen this record ID
            if (seen_record_ids.find(rr.record_id()) != seen_record_ids.end()) {
            //    std::cout << "[" << id_ << "] Skipping duplicate record " << rr.record_id() << std::endl;
                continue;
            }
            
            // Add the record and mark it as seen
            seen_record_ids.insert(rr.record_id());
            auto p = combined.add_records();
            p->CopyFrom(rr);
        }
    }

    // 5. cache
    updateCache(*req, combined);

    *resp = combined;
    return Status::OK;
}
