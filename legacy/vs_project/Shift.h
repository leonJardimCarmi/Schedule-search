#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "Person.h"
#include "TimePeriod.h" 

using namespace std;

class Shift{
private:
	int day_idx;
	string date;
	TimePeriod period; 
	int min_staff, wanted_staff;
	size_t dstaff_size;
	vector<Person> assigned_person; 

public:
	Shift(int day, string str, TimePeriod p, int min, int wanted)
		: day_idx(day), date(str), period(p), min_staff(min), wanted_staff(wanted) {}

	const TimePeriod& getPeriod() const { return period; }
	bool canAssign(const Person& p) const;
	void assign(Person & p);
	void unassign(Person& p);
	const string& getShiftDate() const { return date; }
	static Shift fromString(const string& line);
	int getDayIdx() const { return day_idx; };
	int getWantedStaff() const { return wanted_staff; };
	int getMinStaff() const { return min_staff; };
	int getAssignedCount() const { return static_cast<int>(assigned_person.size()); }
	bool isAssigned(Person const & p) const ;

};

