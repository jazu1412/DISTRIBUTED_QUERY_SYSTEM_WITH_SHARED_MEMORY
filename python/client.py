# usage: python client.py <host:port> <min_injury> <max_injury>
import sys
import grpc
import basecamp_pb2
import basecamp_pb2_grpc

if __name__=="__main__":
    if len(sys.argv)<4:
        print("Usage: python client.py <host:port> <min> <max>")
        sys.exit(1)
    target = sys.argv[1]
    mini = int(sys.argv[2])
    maxi = int(sys.argv[3])

    channel = grpc.insecure_channel(target)
    stub = basecamp_pb2_grpc.QueryServiceStub(channel)

    req = basecamp_pb2.QueryRequest(
        query_id = "QXYZ",
        min_injury = mini,
        max_injury = maxi,
        sender_id = "client"
    )

    resp = stub.QueryByInjuryRange(req)
    print(f"Got {len(resp.records)} records from overlay.")
    for r in resp.records:
        print(r)
