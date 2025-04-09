# usage: python client.py <host:port> <min_injury> <max_injury>
import sys
import grpc
import basecamp_pb2
import basecamp_pb2_grpc
import uuid 

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python client.py <host:port> <min> <max>")
        sys.exit(1)

    target = sys.argv[1]
    mini = int(sys.argv[2])
    maxi = int(sys.argv[3])

    #Generate a unique query_id using uuid
    query_id = str(uuid.uuid4())[:8]  # shortens the UUID to 8 characters

    channel = grpc.insecure_channel(target)
    stub = basecamp_pb2_grpc.QueryServiceStub(channel)

    req = basecamp_pb2.QueryRequest(
        query_id=query_id,
        min_injury=mini,
        max_injury=maxi,
        sender_id="client"
    )

    print(f"Sending query_id: {query_id}")
    resp = stub.QueryByInjuryRange(req)
    print(f"Got {len(resp.records)} records from overlay.")
    for r in resp.records:
        print(r)
