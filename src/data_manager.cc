#include "data_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <functional>

// Constructor
DataManager::DataManager(const std::string& nodeId) : nodeId_(nodeId) {
    // Initialize with the node ID
}

// Helper method to split CSV line into tokens
std::vector<std::string> DataManager::splitCSVLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    
    // Handle quoted fields with commas inside them
    bool inQuotes = false;
    std::string field;
    
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            tokens.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    
    // Add the last field
    tokens.push_back(field);
    
    return tokens;
}

// Helper method to parse integer with default value
int DataManager::parseIntWithDefault(const std::string& str, int defaultValue) {
    try {
        if (str.empty()) return defaultValue;
        return std::stoi(str);
    } catch (const std::exception&) {
        return defaultValue;
    }
}

void DataManager::loadAll() {
    std::ifstream file(csvFilePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open CSV file: " << csvFilePath << std::endl;
        // Fall back to dummy data if file can't be opened
        for (int i=0; i<20; i++) {
            basecamp::Record r;
            r.set_record_id(i);
            r.set_borough((i%2==0)?"QUEENS":"BROOKLYN");
            r.set_injury_count(i);
            r.set_other_fields("Extra #" + std::to_string(i));
            allRecs_.push_back(r);
        }
        return;
    }
    
    std::string line;
    // Skip header line
    std::getline(file, line);
    
    int recordId = 0;
    int maxRecords = 400000; 
    int loadedRecords = 0;
    
    while (std::getline(file, line) && loadedRecords < maxRecords) {
        auto tokens = splitCSVLine(line);
        
        // Check if we have enough tokens
        if (tokens.size() < 18) continue; // Need at least up to NUMBER_OF_MOTORIST_KILLED
        
        basecamp::Record r;
        r.set_record_id(recordId++);
        
        // Set borough (index 2)
        if (tokens.size() > 2 && !tokens[2].empty()) {
            r.set_borough(tokens[2]);
        } else {
            r.set_borough("UNKNOWN");
        }
        
        // Set injury count (index 10 - NUMBER OF PERSONS INJURED)
        int injuryCount = parseIntWithDefault(tokens[10], 0);
        r.set_injury_count(injuryCount);
        
        // Set other fields as a JSON-like string with additional information
        std::string otherFields = "{";
        if (tokens.size() > 3) otherFields += "\"zip_code\":\"" + tokens[3] + "\",";
        if (tokens.size() > 10) otherFields += "\"persons_injured\":" + tokens[10] + ",";
        if (tokens.size() > 11) otherFields += "\"persons_killed\":" + tokens[11] + ",";
        if (tokens.size() > 12) otherFields += "\"pedestrians_injured\":" + tokens[12] + ",";
        if (tokens.size() > 13) otherFields += "\"pedestrians_killed\":" + tokens[13] + ",";
        if (tokens.size() > 14) otherFields += "\"cyclist_injured\":" + tokens[14] + ",";
        if (tokens.size() > 15) otherFields += "\"cyclist_killed\":" + tokens[15] + ",";
        if (tokens.size() > 16) otherFields += "\"motorist_injured\":" + tokens[16] + ",";
        if (tokens.size() > 17) otherFields += "\"motorist_killed\":" + tokens[17];
        otherFields += "}";
        
        r.set_other_fields(otherFields);
        
        allRecs_.push_back(r);
        loadedRecords++;
    }
    
    std::cout << "Loaded " << allRecs_.size() << " records from CSV file." << std::endl;
}

std::vector<basecamp::Record> DataManager::filterByInjuryRange(int minI, int maxI) {
    std::vector<basecamp::Record> out;
    for (auto& r: allRecs_) {
        if (r.injury_count() >= minI && r.injury_count() <= maxI) {
            out.push_back(r);
        }
    }
    return out;
}
