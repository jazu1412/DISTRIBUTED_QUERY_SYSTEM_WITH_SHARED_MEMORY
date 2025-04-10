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
            ShmCache* ptr = (ShmCache*) openOrCreateShm(nm,1024*1024); // 1MB shared memory
            if (!ptr) {
                std::cerr << "[" << id_ << "] Failed to create shared memory with " << nbr.id << std::endl;
            } else {
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
    auto it = cache_.find(req.query_id());
    if (it != cache_.end()) {
        *out = it->second;
        std::cout << "[" << id_ << "] in-mem cache hit for " << req.query_id() << "\n";
        return true;
    }

    // check shared mem
    for (auto& kv : localShmMap_) {
        ShmCache* shm = kv.second;
        if (!shm) continue;
        if (!shm->ready) continue;
        if (req.query_id() == std::string(shm->query_id)) {
            // parse
            std::string raw((char*)shm->data, shm->data_size);
            QueryResponse tmp;
            tmp.ParseFromString(raw);
            *out = tmp;
            cache_[req.query_id()] = tmp;
            std::cout << "["<<id_<<"] SHM cache hit for "<<req.query_id()<<" from "<<kv.first<<"\n";
            return true;
        }
    }

    return false;
}

void Node::updateCache(const QueryRequest& req, const QueryResponse& r) {
    cache_[req.query_id()] = r;

    // also store in shm if local
    for (auto& kv : localShmMap_) {
        ShmCache* shm = kv.second;
        if (!shm) continue;
        shm->ready = false;
        memset(shm->query_id, 0, sizeof(shm->query_id));
        strncpy(shm->query_id, req.query_id().c_str(), sizeof(shm->query_id)-1);
        auto ser = r.SerializeAsString();
        if (ser.size() > sizeof(shm->data)) {
            std::cout << "["<<id_<<"] Not enough shm space for cache\n";
            continue;
        }
        memcpy(shm->data, ser.data(), ser.size());
        shm->data_size = ser.size();
        shm->ready = true;
    }
}

void Node::forwardQuery(const QueryRequest& req,
                        std::vector<QueryResponse>& results,
                        const std::string& sender) {
    std::vector<std::future<QueryResponse>> futs;
    for (auto& nbr : neighbors_) {
        if (nbr.id == sender) continue;
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



    std::cout << "[" << id_ << "] Received query_id: " << req->query_id()  << " from sender: " << req->sender_id() << std::endl;


    // 1. check cache
    if (checkCache(*req, resp)) {
        return Status::OK;
    }

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

    // 4. merge
    for (auto& nr : neighborRes) {
        for (auto& rr : nr.records()) {
            auto p = combined.add_records();
            p->CopyFrom(rr);
        }
    }

    // skip dedup for brevity

    // 5. cache
    updateCache(*req, combined);

    *resp = combined;
    return Status::OK;
}
