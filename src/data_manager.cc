#include "data_manager.h"
#include <iostream>

void DataManager::loadAll() {
    // dummy
    for (int i=0; i<20; i++) {
        basecamp::Record r;
        r.set_record_id(i);
        r.set_borough((i%2==0)?"QUEENS":"BROOKLYN");
        r.set_injury_count(i);
        r.set_other_fields("Extra #" + std::to_string(i));
        allRecs_.push_back(r);
    }
}

std::vector<basecamp::Record> DataManager::filterByInjuryRange(int minI, int maxI) {
    std::vector<basecamp::Record> out;
    for (auto& r: allRecs_) {
        if (r.injury_count()>=minI && r.injury_count()<=maxI) {
            out.push_back(r);
        }
    }
    return out;
}
