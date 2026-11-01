#include <string>
#include <iostream>
#include <map> 

#include "Schedule.h"
#include "DataLoader.h"

using namespace std;

int main() {


    map<int,Person> staff = DataLoader::loadStaffFromFile("staff.txt");  
    vector<Shift> shifts = DataLoader::loadShiftsFromFile("shift.txt");

    
 /*   WeeklyPeriod week;
    Schedule mySchedule(&week);*/

   
    /*mySchedule.runScheduling(staff);*/


	return 1;
}