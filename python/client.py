# usage: python client.py <host:port> <min_injury> <max_injury> [query_id]
import sys
import grpc
import basecamp_pb2
import basecamp_pb2_grpc
import uuid 

if __name__ == "__main__":
    # Check if we have enough arguments
    if len(sys.argv) < 4:
        print("Usage: python client.py <host:port> <min> <max> [query_id]")
        print("  - If query_id is provided, it will be used instead of generating a new one")
        print("  - This is useful for testing the shared memory cache")
        sys.exit(1)

    # Parse command-line arguments
    target = sys.argv[1]
    
    # Check if the third argument is a query ID (string) or min_injury (integer)
    try:
        # Try to parse as integers (normal mode)
        mini = int(sys.argv[2])
        maxi = int(sys.argv[3])
        
        # Generate a unique query_id using uuid if not provided
        if len(sys.argv) > 4:
            # Use the provided query ID
            query_id = sys.argv[4]
            print(f"Using provided query_id: {query_id}")
        else:
            # Generate a new query ID
            query_id = str(uuid.uuid4())[:8]  # shortens the UUID to 8 characters
            print(f"Generated new query_id: {query_id}")
    except ValueError:
        # If we can't parse as integers, assume the second argument is a query ID
        query_id = sys.argv[2]
        print(f"Using provided query_id: {query_id}")
        
        # Use default values for min and max injury
        mini = 0
        maxi = 100
        print(f"Using default injury range: {mini}-{maxi}")

    # Create gRPC channel and stub
    channel = grpc.insecure_channel(target)
    stub = basecamp_pb2_grpc.QueryServiceStub(channel)

    # Create the request
    req = basecamp_pb2.QueryRequest(
        query_id=query_id,
        min_injury=mini,
        max_injury=maxi,
        sender_id="client"
    )

    # Send the request and get the response
    print(f"Sending query to {target} with injury range {mini}-{maxi}")
    resp = stub.QueryByInjuryRange(req)
    
    # Print the results
    print(f"Got {len(resp.records)} records from overlay.")
    for r in resp.records:
        print(r)
