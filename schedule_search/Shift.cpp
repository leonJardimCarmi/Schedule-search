#include "Shift.h"

bool Shift::canAssign(const Person& p) const {
	return p.isAvailable(day_idx, period);
} 

Shift Shift::fromString(const string& line) {
	stringstream ss(line);
	string segment;
	vector <string> parts;

	while (getline(ss, segment, ',')) {
		parts.push_back(segment);
	}

	if (parts.size() < 5)
		throw invalid_argument("expected 5 fields: day,date,HH:MM-HH:MM,min,wanted");

	int d = stoi(parts[0]);
	string date = parts[1];
	TimePeriod period = TimePeriod::fromString(parts[2]);
	int min = stoi(parts[3]);
	int want = stoi(parts[4]);

	return Shift(d, date, period, min, want);
}

void Shift::assign(Person& p) {
	assigned_person.push_back(p);
	p.addShift();
}

void  Shift::unassign(Person& p) {
	auto it = find_if(assigned_person.begin(), assigned_person.end(),
		[&p](const Person& a) {return a.getId() == p.getId(); });

	if (it != assigned_person.end()) { 
		assigned_person.erase(it);  
		p.unassignShift(); 
	}
}


bool Shift::isAssigned(Person const& p) const {
	for (const Person& assigned : assigned_person) { 
		if (assigned.getId() == p.getId())
			return true;
		
	}
	return false;
}