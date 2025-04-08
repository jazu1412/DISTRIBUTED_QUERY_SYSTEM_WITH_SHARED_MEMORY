#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include <grpcpp/grpcpp.h>
#include "basecamp.grpc.pb.h"
#include "shm_utils.h"
#include "data_manager.h"

struct Neighbor {
    std::string id;
    std::string address;
    std::unique_ptr<basecamp::QueryService::Stub> stub;
    bool localEdge;
};

class Node final : public basecamp::QueryService::Service {
public:
    Node(const std::string& nodeId, const std::string& configFile);
    void startServer();

    ::grpc::Status QueryByInjuryRange(::grpc::ServerContext* ctx,
                                      const basecamp::QueryRequest* req,
                                      basecamp::QueryResponse* resp) override;

private:
    std::string id_;
    std::string host_;
    int port_;
    DataManager dataMgr_;
    std::vector<Neighbor> neighbors_;

    std::unordered_map<std::string, basecamp::QueryResponse> cache_;
    std::unordered_map<std::string, ShmCache*> localShmMap_;

    void loadConfig(const std::string& file);
    bool checkCache(const basecamp::QueryRequest& req, basecamp::QueryResponse* out);
    void updateCache(const basecamp::QueryRequest& req, const basecamp::QueryResponse& r);
    void forwardQuery(const basecamp::QueryRequest& req,
                      std::vector<basecamp::QueryResponse>& results,
                      const std::string& sender);
};
