/*
 * migration_model.hpp
 *
 *  Created on: Jul 27, 2019
 *      Author: harsh
 */

#ifndef MIGRATION_MODEL_HPP_
#define MIGRATION_MODEL_HPP_

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Request.hpp"

class migration_model
{
public:
	migration_model(uint64_t num_dram_pages, uint64_t num_disk_nvm_pages, std::string migration_policy,
			uint32_t migration_threshold)
	:num_dram_pages_(num_dram_pages),
	 num_disk_nvm_pages_(num_disk_nvm_pages),
	 migration_policy_(migration_policy),
	 migration_threshold_(migration_threshold)
	{

		page_counts = new int64_t[num_disk_nvm_pages];
	    migrated_pages = new int64_t[num_dram_pages];
	    dram_page_references = new bool[num_dram_pages];
	    dram_page_location = new int64_t[num_disk_nvm_pages];
	    num_empty_dram_pages_ = num_dram_pages;

	    if (migration_policy == "mig_dmn")
	    {
		    required_empty_pages_ = 0.001 * num_dram_pages;
	    }
	    else
	    {
	    	required_empty_pages_ = 0;
	    }

	    //Initialization

		for (int i=0; i < num_disk_nvm_pages; i ++) {
	        dram_page_location[i] = -1;
	    }

	    memset(migrated_pages,0,num_dram_pages*sizeof(int64_t));
	    memset(dram_page_references,false,num_dram_pages*sizeof(bool));
	    memset(page_counts,0,num_disk_nvm_pages*sizeof(int64_t));

	    num_migrations = std::make_shared <counter>("Total Num of Migrations (Insertions to Queue)",module_counters);
		eviction_count_1 = std::make_shared <counter>("Single Evictions",module_counters);
		eviction_count_2 = std::make_shared <counter>("Double Evictions",module_counters);
		prefetch_already_in_dram = std::make_shared <counter>("Prefetches already in DRAM",module_counters);;

	    std::cout << "[MIGRATION_MODEL] Initialized hybrid mem system with " << num_dram_pages_
	    		<< " DRAM pages, " << num_disk_nvm_pages_ << " NVM/DISK pages, "
				<< migration_policy_ << " Migration policy, "
				<< migration_threshold_ << " threshold, " << std::endl;

	}

	void evict_page_premptive()
	{
		int64_t ref_itr = ref_ptr_;

		while(1)
		{
			if (dram_page_references[ref_itr] == true)
			{
				dram_page_references[ref_itr] = false;
			}

			if (migrated_pages[ref_itr] != 0 && dram_page_references[ref_itr] == false)
			{
				break;
			}
            ++ref_itr;
            ref_itr = ref_itr%num_dram_pages_;
		}

        page_counts[migrated_pages[ref_itr]] = 0;
        dram_page_location[migrated_pages[ref_itr]] = -1;
        std::cout << "Page : " << migrated_pages[ref_itr] << " returned to NVM/Disk" << std::endl;
        ref_ptr_ = (ref_itr + 1)%num_dram_pages_;

        (*num_migrations)++;
        num_empty_dram_pages_++;

	}

	void evict_page(int page_num)
	{
        int64_t ref_itr = ref_ptr_;

        while (dram_page_references[ref_itr] == true) {
            dram_page_references[ref_itr] = false;
            ++ref_itr;
            ref_itr = ref_itr%num_dram_pages_;
        }

        if ((dram_page_location[migrated_pages[ref_itr]] != -1)) {
            page_counts[migrated_pages[ref_itr]] = 0;
            dram_page_location[migrated_pages[ref_itr]] = -1;
            std::cout << "Page : " << migrated_pages[ref_itr] << " returned to NVM/Disk" << std::endl;
        }
        else
        {
        	num_empty_dram_pages_--;
        }

		dram_page_location[page_num] = ref_itr;
		migrated_pages[ref_itr] = page_num;

        ref_ptr_ = (ref_itr + 1)%num_dram_pages_;

//        if (tp_ptr->global_ts > (tp_ptr->skip_instructions + tp_ptr->warmup_period))
//        {
        	(*num_migrations)++;
//        }
	}

	bool processPage(Request * req, int &eviction_count)
	{
		int page_num = (req->m_addr/4096) & (0xffffff);

		bool is_page_migrated = false;

		if (dram_page_location[page_num] < 0)
			page_counts[page_num]++;
		else
			page_counts[page_num] = 0;

	    num_entries_in_trace++;
	    eviction_count = 0;

	    //std::cout << "[MIGRATION] Got request for page num : " << std::hex << page_num << std::dec
	    //		<< " page_num count : " << page_counts[page_num]  << std::endl;

		if ((page_counts[page_num] >= migration_threshold_) && (dram_page_location[page_num] < 0)) {

		    std::cout << "[MIGRATION] Migrating page : " << std::hex << page_num << std::dec << std::endl;

			evict_page(page_num);
			eviction_count++;
			is_page_migrated = true;

			std::cout << "num_empty_dram_pages_ " << num_empty_dram_pages_ << std::endl;

			if (num_empty_dram_pages_ < required_empty_pages_)
			{
				evict_page_premptive();
				eviction_count++;

				if (required_empty_pages_ == 0)
				{
					assert(false);
				}

				assert(num_empty_dram_pages_ >= required_empty_pages_);
			}
		}

		if (dram_page_location[page_num] >= 0) {
			dram_page_references[dram_page_location[page_num]] = true;
		}

		if (is_page_migrated && (migration_policy_ == "prefetch"))
		{
			++page_num;
			bool page_already_in_dram = false;

			for (int j = 0 ; j < num_dram_pages_; j++)
			{
				if (migrated_pages[j] == page_num)
				{
					page_already_in_dram = true;
					break;
				}
			}

			if (!page_already_in_dram)
			{
				evict_page(page_num);
				eviction_count++;
				is_page_migrated = true;
			}
			else
			{
				(*prefetch_already_in_dram)++;
			}
		}

		if (is_page_migrated)
		{
			std::cout << "[MIGRATION] eviction_count returned : " << eviction_count << std::endl;
		}

		if (eviction_count == 1)
		{
			(*eviction_count_1)++;
		}
		if (eviction_count == 2)
		{
			(*eviction_count_2)++;
		}

	    return is_page_migrated;
	}

	bool is_page_in_nvm(Request *req)
	{
		int page_num = (req->m_addr/4096) & (0xffffff);

		return (dram_page_location[page_num] == -1);
	}

	void add_traceprocessor(TraceProcessor *tp)
	{
		tp_ptr = tp;

	    for(int i = 0; i < module_counters.size(); i++)
	    {
	    	module_counters[i]->set_tp(tp);
	    }
	}


private:
	uint64_t num_dram_pages_ = 0;
	uint64_t num_disk_nvm_pages_ = 0;
	uint32_t num_empty_dram_pages_ = 0;
	int64_t * page_counts = 0;
	int64_t * migrated_pages = 0;
	int64_t * dram_page_location = 0;
	bool * dram_page_references;
	uint32_t migration_threshold = 0;
	int64_t next_free_dram_page = 0;
	int64_t num_entries_in_trace = 0;
	uint64_t migration_threshold_ = 0;
	int64_t ref_ptr_ = 0;
	int required_empty_pages_ = 0;
	TraceProcessor* tp_ptr = nullptr;

public:
	std::vector <counter *> module_counters;

	std::shared_ptr <counter> num_migrations;
	std::shared_ptr <counter> eviction_count_1 = 0;
	std::shared_ptr <counter> eviction_count_2 = 0;
	std::shared_ptr <counter> prefetch_already_in_dram = 0;
	std::string migration_policy_ = "";

};

#endif /* MIGRATION_MODEL_HPP_ */
