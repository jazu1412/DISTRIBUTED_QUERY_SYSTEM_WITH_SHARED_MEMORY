import sys
import grpc
import basecamp_pb2
import basecamp_pb2_grpc
import uuid 

if __name__ == "__main__":
 
    if len(sys.argv) < 2:
        print("Usage: python client.py <host:port> <min> <max> [query_id] [sender_id]")
        print("Examples:")
        print("  python client.py 10.0.0.35:50051 10 11                  # Normal query to node A")
        print("  python client.py 10.0.0.35:50051 10 11 abc123           # Query with specific ID to node A")
        print("  python client.py 10.0.0.16:50053 10 11 abc123           # Query with specific ID to node C (Linux)")
        print("  python client.py 10.0.0.16:50053 10 11 abc123 node_B    # Query with specific ID to node C, pretending to be node B")
        sys.exit(1)

    # python client.py <host:port> <min_injury> <max_injury> [query_id - <optional>]
    # Parse arguments
    target = sys.argv[1]
    
   
    mini = 0
    maxi = 100
    query_id = str(uuid.uuid4())[:8] 
    sender_id = "client" 
    
   
    if len(sys.argv) >= 4:
        try:
            mini = int(sys.argv[2])
            maxi = int(sys.argv[3])
            
          
            if len(sys.argv) >= 5:
                query_id = sys.argv[4]
                print(f"Using provided query_id: {query_id}")
            else:
                print(f"Generated new query_id: {query_id}")
        except ValueError:
            query_id = sys.argv[2]
            print(f"Using provided query_id: {query_id}")
            print(f"Using default injury range: {mini}-{maxi}")
            
           

   
    channel = grpc.insecure_channel(target)
    stub = basecamp_pb2_grpc.QueryServiceStub(channel)

  
    req = basecamp_pb2.QueryRequest(
        query_id=query_id,
        min_injury=mini,
        max_injury=maxi,
        sender_id=sender_id
    )

   
    print(f"Sending query to {target} with injury range {mini}-{maxi}")
    resp = stub.QueryByInjuryRange(req)
    
  
    print(f"Got {len(resp.records)} records from overlay.")
    for r in resp.records:
        print(r)
