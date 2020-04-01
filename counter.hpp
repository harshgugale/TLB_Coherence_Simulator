/*
 * counter.hpp
 *
 *  Created on: Mar 31, 2020
 *      Author: harsh
 */

#ifndef COUNTER_HPP_
#define COUNTER_HPP_
#include <iostream>
#include "utils.hpp"
#include <fstream>
#include "Request.hpp"
#include <cstring>
#include <unordered_map>
#include <set>
#include <assert.h>
#include <deque>
#include <vector>
#include <iostream>
#include "TraceProcessor.hpp"

class counter {

public:

	uint64_t value;
	TraceProcessor* tp_ptr = nullptr;
	std::string counter_name;

	counter(std::string counter_name_, std::vector <counter *> &ind_counters):
		counter_name(counter_name_)
	{
		value = 0;
		ind_counters.push_back(this);
	}

	uint64_t get_val()
	{
		return value;
	}

    void operator++()
	{
    	if (tp_ptr->global_ts > (tp_ptr->skip_instructions + tp_ptr->warmup_period))
    	{
    		value++;
    	}
    }

    void operator++(int a)
	{
    	if (tp_ptr->global_ts > (tp_ptr->skip_instructions + tp_ptr->warmup_period))
    	{
    		value++;
    	}
    }

    void operator+=(int const &inc)
	{
    	if (tp_ptr->global_ts > (tp_ptr->skip_instructions + tp_ptr->warmup_period))
    	{
    		value+=inc;
    	}
    }

    friend std::ostream& operator<<(std::ostream& os,const counter &c)
    {
        os << c.counter_name << " = " << c.value << std::endl;
        return os;
    }

    void set_tp(TraceProcessor* m_tp_ptr)
    {
    	tp_ptr = m_tp_ptr;
    }

    TraceProcessor* get_tp_ptr()
    {
    	return tp_ptr;
    }

};



#endif /* COUNTER_HPP_ */
