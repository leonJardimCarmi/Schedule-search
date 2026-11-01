#include "DataLoader.h"

map<int, Person> DataLoader::loadStaffFromFile(const string& filename) { 
	map<int, Person> staff; 
	ifstream file(filename);
	string line;

	if (!file.is_open()) {
		cerr << "Could not open file" << endl;
		return staff;
	}

    while (getline(file, line)) { 
        stringstream ss(line); 
        string segment; 
        vector<string> parts; 

        while (getline(ss, segment, ',')) parts.push_back(segment); 

        int id = stoi(parts[0]); 
        string fname = parts[1]; 
        string lname = parts[2]; 
        int day = stoi(parts[3]); 
        string time = parts[4]; 

        auto it = staff.try_emplace(id, id, fname, lname).first;
        it->second.addAvailability(day, time);
    }

	return staff;
}

vector<Shift> DataLoader::loadShiftsFromFile(const string& filename) {
    vector<Shift> shifts;
    ifstream file(filename);
    string line;

    if (!file.is_open()) {
        cerr << "Error: Could not open file " << filename << endl;
        return shifts;
    }

    while (getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        try {
            shifts.push_back(Shift::fromString(line));
        }
        catch (const exception& e) {
            cerr << "Error parsing line: " << line << " | " << e.what() << endl;
        }
    }

    sort(shifts.begin(), shifts.end(), [](const Shift& a, const Shift& b) {
        if (a.getDayIdx() != b.getDayIdx()) return a.getDayIdx() < b.getDayIdx();
        return a.getPeriod().getStart() < b.getPeriod().getStart();
    });

    return shifts;
}
