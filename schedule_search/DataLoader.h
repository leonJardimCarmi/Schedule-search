#pragma once
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <map>

#include "Person.h"
#include "Shift.h"

using namespace std;

class DataLoader{
public:
	//          id
	static map<int, Person> loadStaffFromFile(const string& filename);

	// sorted chronologically (day_idx, then start time)
	static vector<Shift> loadShiftsFromFile(const string& filename);
};

