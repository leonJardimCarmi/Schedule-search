#pragma once
#include "Person.h"
#include "Shift.h" 



class Constraint{
public:
	virtual ~Constraint() = default;
	virtual bool isSatisfied(const Person& p, const Shift& s) const = 0;
};

class NoDoubleShiftConstraint : public Constraint {
public:
	bool isSatisfied(const Person& p, const Shift& s) const override;
};

//class FairConstraint : public Constraint {
//protected:
//	ScheduleContext& context;
//public:
//	FairConstraint(ScheduleContext& ctx) : context(ctx) {}
//};

