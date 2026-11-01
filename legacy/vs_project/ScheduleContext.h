#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <map>
#include <set>


class ScheduleContext{
private:
	int min_worker_scount;
	int max_worker_scount;
	int min_rest_days;
	int max_con_work_days;
	std::map<int, std::set<int>> worker_days;

public:
	void addWorkDay(int person_id, int day_idx);
	void removeWorkDay(int person_id, int day_idx);
	int getConsecutiveDays(int person_id, int current_day) const;


};

