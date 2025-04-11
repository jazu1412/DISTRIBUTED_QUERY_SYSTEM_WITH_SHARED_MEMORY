#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include "basecamp.pb.h"

class DataManager {
public:
    // Constructor that takes a node ID
    DataManager(const std::string& nodeId = "");
    
    void loadAll();
    std::vector<basecamp::Record> filterByInjuryRange(int minI, int maxI);
    
private:
    std::string nodeId_; // The ID of the node this DataManager belongs to
    std::vector<basecamp::Record> allRecs_;
    
    // Helper methods for CSV parsing
    std::vector<std::string> splitCSVLine(const std::string& line);
    int parseIntWithDefault(const std::string& str, int defaultValue = 0);
    
    // CSV file path
    const std::string csvFilePath = "Motor_Vehicle_Collisions_-_Crashes_20250212.csv";
};
