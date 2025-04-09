#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include "basecamp.pb.h"

class DataManager {
public:
    void loadAll();
    std::vector<basecamp::Record> filterByInjuryRange(int minI, int maxI);
    
private:
    std::vector<basecamp::Record> allRecs_;
    
    // Helper methods for CSV parsing
    std::vector<std::string> splitCSVLine(const std::string& line);
    int parseIntWithDefault(const std::string& str, int defaultValue = 0);
    
    // CSV file path
    const std::string csvFilePath = "Motor_Vehicle_Collisions_-_Crashes_20250212.csv";
};
