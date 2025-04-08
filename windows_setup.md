# Windows Setup Guide for MINI2 Basecamp

This guide will walk you through setting up the MINI2 Basecamp project on your Windows machine (10.0.0.36).

## Prerequisites

You'll need to install the following dependencies:

1. **Visual Studio** (2019 or newer recommended) with C++ development tools
2. **CMake** (3.14 or newer)
3. **Git** (for cloning repositories)
4. **Python** (3.6 or newer)
5. **vcpkg** (for managing C++ libraries)

## Step 1: Install Dependencies

### Install Visual Studio
1. Download Visual Studio from [https://visualstudio.microsoft.com/](https://visualstudio.microsoft.com/)
2. During installation, select the "Desktop development with C++" workload
3. Make sure to include the "C++ CMake tools for Windows" component

### Install CMake
1. Download CMake from [https://cmake.org/download/](https://cmake.org/download/)
2. Add CMake to your system PATH during installation

### Install Git
1. Download Git from [https://git-scm.com/download/win](https://git-scm.com/download/win)
2. Use the default installation options

### Install Python
1. Download Python from [https://www.python.org/downloads/windows/](https://www.python.org/downloads/windows/)
2. Make sure to check "Add Python to PATH" during installation
3. Install the required Python packages:
   ```
   pip install grpcio grpcio-tools protobuf
   ```

### Install vcpkg
1. Open a Command Prompt or PowerShell window
2. Clone the vcpkg repository:
   ```
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```
3. Install the required libraries:
   ```
   .\vcpkg install grpc:x64-windows
   .\vcpkg install protobuf:x64-windows
   .\vcpkg install nlohmann-json:x64-windows
   ```
4. Integrate vcpkg with Visual Studio:
   ```
   .\vcpkg integrate install
   ```

## Step 2: Project Setup

1. Create a directory for the project:
   ```
   mkdir C:\MINI2-Basecamp
   cd C:\MINI2-Basecamp
   ```

2. Copy all the project files from the Mac machine to this directory, or clone from a repository if available.

3. Create a `build` directory:
   ```
   mkdir build
   ```

## Step 3: Generate Protobuf Files

1. Open a Command Prompt in the project directory
2. Run the following commands to generate the protobuf files:
   ```
   cd scripts
   bash build_proto.sh
   ```
   
   If bash is not available, you can run the protoc command directly:
   ```
   cd C:\MINI2-Basecamp
   protoc -I=proto --cpp_out=build --grpc_out=build --plugin=protoc-gen-grpc="C:\path\to\grpc_cpp_plugin.exe" proto\basecamp.proto
   ```

3. Copy the generated files to the Python directory for the client:
   ```
   cd C:\MINI2-Basecamp
   python -m grpc_tools.protoc -I=proto --python_out=python --grpc_python_out=python proto/basecamp.proto
   ```

## Step 4: Build the Project

1. Open a Command Prompt with Visual Studio developer tools (or a "Developer Command Prompt for VS")
2. Navigate to the project directory:
   ```
   cd C:\MINI2-Basecamp
   ```
3. Configure and build with CMake:
   ```
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
   cmake --build build --config Release
   ```

## Step 5: Running the Nodes

Since you're on the Windows machine (10.0.0.36), you'll be running nodes C, D, and E.

1. Open three separate Command Prompt windows
2. In each window, navigate to the build directory:
   ```
   cd C:\MINI2-Basecamp\build
   ```
3. Run each node in a separate window:
   ```
   # Window 1
   .\Release\basecamp_node.exe C ..\config\overlay.json
   
   # Window 2
   .\Release\basecamp_node.exe D ..\config\overlay.json
   
   # Window 3
   .\Release\basecamp_node.exe E ..\config\overlay.json
   ```

## Step 6: Testing with the Client

Once all nodes are running (both on your Windows machine and on the Mac machine), you can test the system:

1. Open a new Command Prompt
2. Navigate to the Python directory:
   ```
   cd C:\MINI2-Basecamp\python
   ```
3. Run the client:
   ```
   python client.py 10.0.0.35:50051 5 15
   ```

This will connect to node A on the Mac machine and query for records with injury counts between 5 and 15.

## Troubleshooting

### Firewall Issues
Make sure Windows Firewall allows incoming connections on ports 50053, 50054, and 50055.

### Missing Libraries
If you encounter missing library errors, make sure all dependencies are properly installed through vcpkg.

### Shared Memory Issues
The code uses shared memory for local communication between nodes. On Windows, this is implemented using Windows-specific APIs in the `shm_utils.cc` file.

### Path Issues
If you encounter "command not found" errors, make sure all tools are properly added to your PATH.
