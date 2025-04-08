# Mac Setup Guide for MINI2 Basecamp

This guide will walk you through setting up the MINI2 Basecamp project on your Mac machine (10.0.0.35).

## Prerequisites

You'll need to install the following dependencies:

1. **Xcode Command Line Tools** (for C++ compiler and development tools)
2. **CMake** (3.14 or newer)
3. **Homebrew** (for package management)
4. **Python** (3.6 or newer)
5. **gRPC** and **Protocol Buffers**

## Step 1: Install Dependencies

### Install Xcode Command Line Tools
```bash
xcode-select --install
```

### Install Homebrew
```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### Install CMake
```bash
brew install cmake
```

### Install gRPC and Protocol Buffers
```bash
brew install grpc protobuf
```

### Install nlohmann/json
```bash
brew install nlohmann-json
```

### Install Python Dependencies
```bash
pip3 install grpcio grpcio-tools protobuf
```

## Step 2: Project Setup

1. Create a directory for the project:
```bash
mkdir -p ~/MINI2-Basecamp
cd ~/MINI2-Basecamp
```

2. Copy all the project files to this directory, or clone from a repository if available.

3. Create a `build` directory:
```bash
mkdir build
```

## Step 3: Generate Protobuf Files

1. Generate the C++ protobuf files:
```bash
cd ~/MINI2-Basecamp/scripts
./build_proto.sh
```

2. Generate the Python protobuf files for the client:
```bash
cd ~/MINI2-Basecamp
python3 -m grpc_tools.protoc -I=proto --python_out=python --grpc_python_out=python proto/basecamp.proto
```

## Step 4: Build the Project

1. Configure and build with CMake:
```bash
cd ~/MINI2-Basecamp
cmake -B build -S .
cmake --build build
```

## Step 5: Running the Nodes

Since you're on the Mac machine (10.0.0.35), you'll be running nodes A and B.

1. Open two separate Terminal windows
2. In each window, navigate to the build directory:
```bash
cd ~/MINI2-Basecamp/build
```

3. Run each node in a separate window:
```bash
# Terminal 1
./basecamp_node A ../config/overlay.json

# Terminal 2
./basecamp_node B ../config/overlay.json
```

## Step 6: Testing with the Client

Once all nodes are running (both on your Mac machine and on the Windows machine), you can test the system:

1. Open a new Terminal window
2. Navigate to the Python directory:
```bash
cd ~/MINI2-Basecamp/python
```

3. Run the client:
```bash
python3 client.py 10.0.0.35:50051 5 15
```

This will connect to node A on your Mac machine and query for records with injury counts between 5 and 15.

## Troubleshooting

### Firewall Issues
Make sure your Mac's firewall allows incoming connections on ports 50051 and 50052.

### Missing Libraries
If you encounter missing library errors, make sure all dependencies are properly installed through Homebrew.

### Shared Memory Issues
The code uses shared memory for local communication between nodes. On macOS, this is implemented using POSIX shared memory APIs in the `shm_utils.cc` file.

### Path Issues
If you encounter "command not found" errors, make sure all tools are properly added to your PATH.

### Python Version Issues
If you have multiple Python versions installed, make sure you're using Python 3. You might need to use `python3` and `pip3` commands explicitly.
