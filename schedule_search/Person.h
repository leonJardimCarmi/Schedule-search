#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <map>
#include <map>

#include "TimePeriod.h"

using namespace std;

class Person{
private:
	int id;
	string fname, lname;
	int days_count = 0;
	int shift_count = 0;

	map<int, vector<TimePeriod>> availability_slots; // key = day_idx , val = time periods

public:
	Person(int ID, string fn, string ln) :id(ID), fname(fn), lname(ln) {}
	void addAvailability(int day_idx, const string& time_range);
	bool isAvailable(int day_idx, const TimePeriod& period) const;
	int getId() const { return id; } 
	void addShift() { shift_count++; } 
	void unassignShift() { shift_count--; }

};

