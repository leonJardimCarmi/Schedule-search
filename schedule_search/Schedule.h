#pragma once
#include <vector>

#include "TimePeriod.h"
#include "Person.h"
#include "Shift.h"

using namespace std;

class Schedule{
private:
	map<int, Person> staff;       // מפה: ID -> Person
	vector<Shift> shifts;         // sorted chronologically; index = shift id

public:
	Schedule(map<int, Person> s, vector<Shift> sh)
		: staff(s), shifts(sh) {}

	bool canAssignPersonToShift(Person& p, Shift& s);
	bool solve(size_t shift_idx);



};

