#pragma once
#include <vector>
#include "basecamp.pb.h"

class DataManager {
public:
    void loadAll();
    std::vector<basecamp::Record> filterByInjuryRange(int minI, int maxI);
private:
    std::vector<basecamp::Record> allRecs_;
};
