//
//  Cache.cpp
//  TLB-Coherence-Simulator
//
//  Created by Yashwant Marathe on 12/6/17.
//  Copyright © 2017 Yashwant Marathe. All rights reserved.
//


#include "Cache.hpp"
#include "CacheSys.hpp"
#include <assert.h>
#include <vector>
#include <iomanip>
#include <iostream>
#include <bitset>
#include "utils.hpp"
#include "Core.hpp"
#include "migration_model.hpp"
#include <climits>

uint64_t Cache::get_line_offset(const uint64_t addr)
{
    return (addr & ((1 << m_num_line_offset_bits) - 1));
}

uint64_t Cache::get_index(const uint64_t addr)
{
    return (addr >> m_num_line_offset_bits) & (((1 << m_num_index_bits) - 1));
}

uint64_t Cache::get_tag(const uint64_t addr)
{
    return ((addr >> m_num_line_offset_bits) >> m_num_index_bits);
}

bool Cache::is_found(const std::vector<CacheLine>& set,
		const uint64_t tag, bool is_translation, uint64_t tid, unsigned int &hit_pos)
{
    auto it = std::find_if(set.begin(), set.end(), [tag, is_translation, tid, this](const CacheLine &l)
                           {
                               //Threads share address space, so no need to check for tid if the request type is not translation
//                               return (is_translation) ?  ((l.tag == tag) && (l.valid) &&
//                            		   (l.is_translation == is_translation) && (l.tid == tid)) :
//                            		   ((l.tag == tag) && (l.valid) && (l.is_translation == is_translation));

    							return ((l.tag == tag) && (l.valid) && (l.is_translation == is_translation));

                           });
    
    hit_pos = static_cast<unsigned int>(it - set.begin());
    return (it != set.end());
}

bool Cache::is_partially_found(const std::vector<CacheLine>& set,
		const uint64_t tag, bool is_translation, uint64_t tid, unsigned int &hit_pos, uint64_t partial_addr, bool is_large)
{
	uint64_t partial_tag = 0;

	assert(is_translation);

	if (is_large)
	{
		partial_tag = ((partial_addr - m_core->m_l3_small_tlb_size - (m_core->m_l3_small_tlb_base>>6)) >> m_num_index_bits);
	}
	else
	{
		partial_tag = ((partial_addr - (m_core->m_l3_small_tlb_base>>6)) >> m_num_index_bits);
	}


	uint64_t mask_for_partial_compare;

	if(is_large)
		mask_for_partial_compare = ((1 << (12-m_num_index_bits)) - 1);
	else
		mask_for_partial_compare = ((1 << (14-m_num_index_bits)) - 1);

	auto it = std::find_if(set.begin(), set.end(), [tag, is_translation, tid, this,mask_for_partial_compare,partial_tag](const CacheLine &l)
							   {
									bool partial_match = false;

									if((l.tag & mask_for_partial_compare) == partial_tag)
									{
										assert((partial_tag >> log2(mask_for_partial_compare + 1)) == 0);
										partial_match = true;
									}
								   //Threads share address space, so no need to check for tid if the request type is not translation
//								   return (is_translation) ?  (partial_match && (l.valid) &&
//										   (l.is_translation == is_translation) && (l.tid == tid)) :
//										   ((l.tag == tag) && (l.valid) && (l.is_translation == is_translation));

								   return (partial_match && (l.tag == tag) && (l.valid) && (l.is_translation == is_translation));
							   });

	hit_pos = static_cast<unsigned int>(it - set.begin());
	return (it != set.end());
}
bool Cache::is_hit(const std::vector<CacheLine> &set, const uint64_t tag, bool is_translation, uint64_t tid, unsigned int &hit_pos)
{
    return is_found(set, tag, is_translation, tid, hit_pos) & !set[hit_pos].lock;
}

void Cache::invalidate(const uint64_t addr, uint64_t tid, bool is_translation)
{
    unsigned int hit_pos;
    uint64_t tag = get_tag(addr);
    uint64_t index = get_index(addr);
    std::vector<CacheLine>& set = m_tagStore[index];
    
    //If we find line in the cache, invalidate the line
    
    if(is_found(set, tag, is_translation, tid, hit_pos))
    {
        set[hit_pos].valid = false;
    }
    
    //Go all the way up to highest cache
    //There might be more than one higher cache (example L3)
    try
    {
        for(int i = 0; i < m_higher_caches.size(); i++)
        {
            auto higher_cache = m_higher_caches[i].lock();
            if(higher_cache != nullptr)
            {
                higher_cache->invalidate(addr, tid, is_translation);
            }
        }
    }
    catch(std::bad_weak_ptr &e)
    {
        std::cout << "Cache " << m_cache_level << ":" << e.what() << std::endl;
    }

}

void Cache::evict(uint64_t set_num, const CacheLine &line, int req_core_id)
{
    //Send back invalidate
    
    uint64_t evict_addr = ((line.tag << m_num_line_offset_bits) << m_num_index_bits) | (set_num << m_num_line_offset_bits);
    
    if(m_inclusive)
    {
        try
        {
            //No harm in blindly invalidating, since is_found checks is_translation.
            for(int i = 0; i < m_higher_caches.size(); i++)
            {
                auto higher_cache = m_higher_caches[i].lock();
                if(higher_cache != nullptr)
                {
                    higher_cache->invalidate(evict_addr, line.tid, line.is_translation);
                }
            }
        }
        catch(std::bad_weak_ptr &e)
        {
            std::cout << "Cache " << m_cache_level << "doesn't have a valid higher cache" << std::endl;
        }
    }
    
	if(line.valid)
		(*conflict_tr_misses) += line.is_translation;
	else
		(*compulsary_tr_misses) += line.is_translation;

    //Send writeback if dirty
    //If not, due to inclusiveness, lower caches still have data
    //So do runtime check to ensure hit (and inclusiveness)
    if(line.valid)
    {

        std::shared_ptr<Cache> lower_cache = find_lower_cache_in_core(evict_addr, line.is_translation, line.is_large);
    
        Request req(evict_addr, line.is_translation ? TRANSLATION_WRITEBACK : DATA_WRITEBACK, line.tid, line.is_large, m_core_id);

        if((line.dirty || line.is_translation))
        {
            if(lower_cache != nullptr)
            {
                CacheType lower_cache_type = lower_cache->get_cache_type();
                bool is_tr_to_dat_boundary = (m_cache_type == TRANSLATION_ONLY) && (lower_cache_type == DATA_AND_TRANSLATION);
                req.m_addr = (is_tr_to_dat_boundary) ? m_core->getL3TLBAddr(req.m_addr, req.m_type, req.m_tid, req.m_is_large, false) : req.m_addr;
                RequestStatus val = lower_cache->lookupAndFillCache(req, 0, line.m_coherence_prot->getCoherenceState());
                line.m_coherence_prot->forceCoherenceState(INVALID);

                if(m_inclusive)
                {
                    assert((val == REQUEST_HIT) || (val == MSHR_HIT_AND_LOCKED));
                }
            }
            else
            {
				//----Check for migration in lower disk/NVM
				if (m_cache_sys->is_last_level(m_cache_level) && !m_cache_sys->get_is_translation_hier())
				{
					assert(m_core_id == -1);

					trace_tlb_tid_entry_t trace_entry;
					trace_entry.va        = (uint64_t) req.m_addr;
					trace_entry.ts        = (uint64_t) 0;
					trace_entry.write     = !req.m_is_read;
					trace_entry.large     = req.m_is_large;
					trace_entry.tid       = (uint64_t) req.m_tid;
					int eviction_count    = 0;

#if 0
					memFile_ptr_->write((char*)&trace_entry, 1);
#endif
					if (!req.m_is_large && page_migration_model_->processPage(&req,eviction_count))
					{
						assert(eviction_count == 1 || eviction_count == 2);

						for (int i = 0 ; i < eviction_count; i++)
						{
							//std::cout << "Migration itr " << m_tp_ptr->migration_iter;

							Request * mig_req = new Request(req.m_addr, TRANSLATION_WRITE, req.m_tid, req.m_is_large, req_core_id);

							if (mig_req->m_core_id == -1)
							{
								std::cout << "[ERROR] Invalid core id. 1";
								exit(0);
							}

							mig_req->is_migration_shootdown = true;

							if (m_tp_ptr->migration_iter%4 != 0) //25% of all migration shootdowns are guest
							{
								std::cout << "[SHOOTDOWN_EVENT] Host shootdown\n";
								mig_req->is_guest_shootdown = false;
							}
							else
							{
								m_tp_ptr->migration_iter = 0;
							}

							m_tp_ptr->addition_to_migration_queue++;
							m_tp_ptr->add_to_migration_shootdown_queue(mig_req);
							m_tp_ptr->migration_iter++;
						}

					}

					(*mem_accesses)++;

					if (mem_accesses->get_val()%1000000 == 0)
						std::cout << "[MEMWRITE] Mem Accesses " << mem_accesses->get_val() << std::endl;
				}
            }
        }
        else if(!line.dirty)
        {
            line.m_coherence_prot->forceCoherenceState(INVALID);
        }
        else
        {
            if(lower_cache != nullptr && m_inclusive)
            {
                unsigned int hit_pos;
                uint64_t index = lower_cache->get_index(evict_addr);
                uint64_t tag = lower_cache->get_tag(evict_addr);
                std::vector<CacheLine> set = lower_cache->m_tagStore[index];
                assert(lower_cache->is_found(set, tag, line.is_translation, line.tid, hit_pos));
            }
        }
    }

    //Not a part of cache functionality, but needs to be done to support TLB shootdown functionality nonetheless
    //If we're in penultimate TLB and performing an eviction, update the presence map
    if(m_cache_sys->get_is_translation_hier() && m_cache_sys->is_penultimate_level(m_cache_level))
    {
        Request req(evict_addr, TRANSLATION_READ, line.tid, line.is_large, m_core_id);
        bool is_found = false;

        for(int i = 0; i < m_higher_caches.size(); i++)
        {
            auto higher_cache = m_higher_caches[i].lock();
            is_found = is_found | higher_cache->lookupCache(req);
        }

        if(!is_found)
        {
            //std::cout << "[EVICTION] Removing from presence map = " << std::hex << req << std::dec;
            m_tp_ptr->remove_from_presence_map(evict_addr, line.tid, line.is_large, m_core_id);
        }
        //std::cout << "[EVICTION]: In level = " << m_cache_level << " and in hier = " << m_cache_sys->get_is_translation_hier() << "\n";
    }
}

bool Cache::lookupCache(Request &req)
{
    unsigned int hit_pos;

    uint64_t addr = req.m_addr;
    kind txn_kind = req.m_type;
    uint64_t tid = req.m_tid;
    bool is_large = req.m_is_large;

    uint64_t tag = get_tag(addr);
    uint64_t index = get_index(addr);
    std::vector<CacheLine>& set = m_tagStore[index];

    bool is_translation = (txn_kind == TRANSLATION_WRITE) | (txn_kind == TRANSLATION_WRITEBACK) | (txn_kind == TRANSLATION_READ);

    if(is_found(set, tag, is_translation, tid, hit_pos))
    {
        return true;
    }

    return false;
}

bool Cache::cotaglessLookup(uint64_t addr, uint64_t tid, bool is_large)
{
    unsigned int hit_pos;

    uint64_t tag = get_tag(addr);
    uint64_t index = get_index(addr);
    std::vector<CacheLine>& set = m_tagStore[index];

    uint64_t partial_addr = (m_core->getL3TLBAddr(addr, TRANSLATION_WRITE, tid, is_large,false)>>6);


    if (is_partially_found(set, tag, true, tid, hit_pos, partial_addr,is_large) == is_found(set, tag, true, tid, hit_pos))
    {
    	return true;
    }

    if (is_found(set, tag, true, tid, hit_pos))
    {
        std::cout << "Addr " << std::bitset<64>(addr) << "\nPartial Addr " << std::bitset<64>(partial_addr) << "\nPartial tag " << std::bitset<64>(partial_addr >> m_num_index_bits)
        		<< "\nActual tag " << std::bitset<64>(tag) << "\n";
    	assert(is_partially_found(set, tag, true, tid, hit_pos, partial_addr,is_large));
    }
    else
    {
        std::cout << "Addr " << std::bitset<64>(addr) << "\nPartial Addr " << std::bitset<64>(partial_addr) << "\nPartial tag " << std::bitset<64>(partial_addr >> m_num_index_bits)
        		<< "\nActual tag " << std::bitset<64>(tag) << "\n";
    }

    return false;
}

RequestStatus Cache::lookupAndFillCache(Request &req, unsigned int curr_latency, CoherenceState propagate_coh_state)
{
    unsigned int hit_pos = 0;
    
    uint64_t addr = req.m_addr;
    kind txn_kind = req.m_type;
    uint64_t tid = req.m_tid;
    bool is_large = req.m_is_large;

    if(!m_is_callback_initialized)
    {
        initialize_callback();
        m_is_callback_initialized = true;
    }

    #ifdef DEADLOCK_DEBUG
    if(req.m_addr == 0x0)
    {
        std::cout << "[LOOKUPANDFILL] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << req << std::dec;
    }
    #endif
    
    uint64_t tag = get_tag(addr);
    uint64_t index = get_index(addr);
    std::vector<CacheLine>& set = m_tagStore[index];

    if(m_core_id == -1)
    {
        req.m_is_core_agnostic = true;
    }
    
    uint64_t coh_addr;
    uint64_t coh_tid;
    bool coh_is_large;
    uint64_t cur_addr;

    bool is_translation = (txn_kind == TRANSLATION_WRITE) | (txn_kind == TRANSLATION_WRITEBACK) | (txn_kind == TRANSLATION_READ);

    if(is_hit(set, tag, is_translation, tid, hit_pos))
    {
        #ifdef DEADLOCK_DEBUG
        if(req.m_addr == 0x0)
        {
            std::cout << "[LOOKUPANDFILL_HIT] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << req << std::dec;
        }
        #endif

        CacheLine &line = set[hit_pos];
        
        cur_addr = ((line.tag << m_num_line_offset_bits) << m_num_index_bits) | (index << m_num_line_offset_bits);
        
        //Is line dirty now?
        line.dirty = line.dirty || (((txn_kind == DATA_WRITE) ||
        		(txn_kind == TRANSLATION_WRITE)) && (m_cache_level == 1)) || (txn_kind == DATA_WRITEBACK);
        
        assert(!((txn_kind == TRANSLATION_WRITE) | (txn_kind == TRANSLATION_WRITEBACK) | (txn_kind == TRANSLATION_READ)) ^(line.is_translation));
        
        m_repl->updateReplState(index, hit_pos);

        req.add_callback(m_callback);
        std::shared_ptr<Request> r = std::make_shared<Request>(req);
        
        uint64_t deadline = m_cache_sys->m_clk + curr_latency;
        
        //If element already exists in list, push deadline.
        while(m_cache_sys->m_hit_list.find(deadline) != m_cache_sys->m_hit_list.end())
        {
            deadline++;
        }
    
        m_cache_sys->m_hit_list.insert(std::make_pair(deadline, r));

        //Coherence handling
        CoherenceAction coh_action = line.m_coherence_prot->setNextCoherenceState(txn_kind, propagate_coh_state);
        
        //If we need to do writeback, we need to do it for addr already in cache
        //If we need to broadcast, we need to do it for addr in lookupAndFillCache call
        coh_addr = (coh_action == MEMORY_DATA_WRITEBACK || coh_action == MEMORY_TRANSLATION_WRITEBACK) ? cur_addr : addr;
        coh_tid = (coh_action == MEMORY_DATA_WRITEBACK || coh_action == MEMORY_TRANSLATION_WRITEBACK) ? line.tid : tid;
        coh_is_large = (coh_action == MEMORY_DATA_WRITEBACK || coh_action == MEMORY_TRANSLATION_WRITEBACK) ? line.is_large : is_large;
        
        req.m_addr = coh_addr;
        req.m_tid = coh_tid;
        req.m_is_large = coh_is_large;
        
        handle_coherence_action(coh_action, req, curr_latency, true);

		(*num_tr_hits) += (is_translation);
		(*num_data_hits) += (!is_translation);

		(*num_tr_accesses) += (is_translation);
		(*num_data_accesses) += (!is_translation);

        return REQUEST_HIT;
    }
    
    auto it_not_valid = std::find_if(set.begin(), set.end(), [](const CacheLine &l) { return !l.valid; });
    bool needs_eviction = (it_not_valid == set.end()) && (!is_found(set, tag, is_translation, tid, hit_pos));
  
    if(txn_kind == TRANSLATION_WRITEBACK || txn_kind == DATA_WRITEBACK)
    {
        assert(m_cache_level != 1);

        QueueEntry *queue_entry = new QueueEntry();
        queue_entry->m_dirty = true;
        queue_entry->m_coh_state = propagate_coh_state;

        auto itr = m_wb_entries.find(req);

        if (itr != m_wb_entries.end())
        {
        	delete(itr->second);
        	m_wb_entries.erase(itr);
        }

        m_wb_entries.insert(std::make_pair(req, queue_entry));

        //Insert in lower cache
        std::shared_ptr<Cache> lower_cache = find_lower_cache_in_core(addr, is_translation, is_large);
        req.add_callback(m_callback);
        std::shared_ptr<Request> r = std::make_shared<Request>(req);

        uint64_t deadline = m_cache_sys->m_clk + curr_latency;

        //If element already exists in list, push deadline.
        while(m_cache_sys->m_hit_list.find(deadline) != m_cache_sys->m_hit_list.end())
        {
            deadline++;
        }

        m_cache_sys->m_hit_list.insert(std::make_pair(deadline, r));

        return REQUEST_MISS;
    }

    assert(txn_kind != TRANSLATION_WRITEBACK);
    assert(txn_kind != DATA_WRITEBACK);

    bool mshr_hit = false;

    unsigned int mshr_size = m_cache_sys->is_last_level(m_cache_level) ? 32 * NUM_CORES :
                                m_cache_sys->is_penultimate_level(m_cache_level) && !m_cache_sys->get_is_translation_hier() ? 32 : 16;

    auto mshr_iter = m_mshr_addr.find(req.m_addr);

    if(mshr_iter != m_mshr_addr.end())
    {
        bool found_req = (m_mshr_entries.find(req) != m_mshr_entries.end());

        if(req.m_type == TRANSLATION_WRITE || req.m_type == DATA_WRITE)
        {
            for(auto it = mshr_iter->second.begin(); it != mshr_iter->second.end(); it++)
            {
                QueueEntry *q = *it;
                q->m_dirty = true;
            }
        }

        //If MSHR hit due to request from another core, mark MSHR entry as core-agnostic.
        //OR safely mark all MSHR hits as core agnostic
        for(auto it = mshr_iter->second.begin(); it != mshr_iter->second.end(); it++)
        {
            QueueEntry *q = *it;
            q->m_is_core_agnostic = true;
        }

        //If Cachesys is not last cachesys, point to last cachesys
        //TODO: FIXME: Ugly hack, fix this
        CacheSys* cs_ptr = (m_cache_sys->get_core_id() != (NUM_CORES - 1)) ? m_cache_sys->m_other_cache_sys[NUM_CORES - 2].get() : m_cache_sys;
        assert(cs_ptr->get_core_id() == (NUM_CORES - 1));

        bool added_to_list = false;

        for(auto it = cs_ptr->m_wait_list.begin(); it != cs_ptr->m_wait_list.end() ; it++)
        {
            //If we did not find exact request in MSHR, add the current request to wait list
            if(it->second->m_addr == req.m_addr && !found_req)
            {
                req.add_callback(m_callback);
                std::shared_ptr<Request> r = std::make_shared<Request>(req);

                uint64_t deadline = it->first;

                while(cs_ptr->m_wait_list.find(deadline) != cs_ptr->m_wait_list.end())
                {
                    deadline++;
                }

                cs_ptr->m_wait_list.insert(std::make_pair(deadline, r));
                added_to_list = true;
                break;
            }
        }

        for(auto it = cs_ptr->m_hit_list.begin(); it != cs_ptr->m_hit_list.end() && !added_to_list; it++)
        {
            //If we did not find exact request in MSHR, add the current request to wait list
            if(it->second->m_addr == req.m_addr && !found_req && it->second->m_is_core_agnostic)
            {
                req.add_callback(m_callback);
                std::shared_ptr<Request> r = std::make_shared<Request>(req);

                uint64_t deadline = it->first;

                while(cs_ptr->m_wait_list.find(deadline) != cs_ptr->m_wait_list.end())
                {
                    deadline++;
                }

                cs_ptr->m_wait_list.insert(std::make_pair(deadline, r));
                added_to_list = true;
                break;
            }
        }

        for(auto it = m_cache_sys->m_hit_list.begin(); it != m_cache_sys->m_hit_list.end() && !added_to_list; it++)
        {
            //If we did not find exact request in MSHR, add the current request to wait list
            if(it->second->m_addr == req.m_addr && !found_req)
            {
                req.add_callback(m_callback);
                std::shared_ptr<Request> r = std::make_shared<Request>(req);

                uint64_t deadline = it->first;

                while(m_cache_sys->m_hit_list.find(deadline) != m_cache_sys->m_hit_list.end())
                {
                    deadline++;
                }

                m_cache_sys->m_hit_list.insert(std::make_pair(deadline, r));
                added_to_list = true;
                break;
            }
        }

        //TODO:FIXME: Ugly hack to fix TID mismatch while retiring from MSHR
        if(!added_to_list && !found_req)
        {
            std::cout << "[MSHR_HIT_NOT_ADDED_TO_LIST] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ":" << std::hex << req << std::dec;
            req.add_callback(m_callback);
            std::shared_ptr<Request> r = std::make_shared<Request>(req);

            uint64_t deadline = m_cache_sys->m_clk + 1;

            while(m_cache_sys->m_hit_list.find(deadline) != m_cache_sys->m_hit_list.end())
            {
                deadline++;
            }

            m_cache_sys->m_hit_list.insert(std::make_pair(deadline, r));
        }

		(*num_mshr_tr_hits) += (is_translation);
		(*num_mshr_data_hits) += (!is_translation);

		(*num_tr_misses) += (is_translation);
		(*num_data_misses) += (!is_translation);

		(*num_tr_accesses) += (is_translation);
		(*num_data_accesses) += (!is_translation);

        #ifdef DEADLOCK_DEBUG
        if(req.m_addr == 0x0)
        {
            std::cout << "[LOOKUPANDFILL_MSHR_HIT] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << req << std::dec;
        }
        #endif

        mshr_hit = true;
    }
    else if(m_mshr_entries.size() < mshr_size)
    {
        QueueEntry *queue_entry = new QueueEntry();

        if(req.m_type == TRANSLATION_WRITE || req.m_type == DATA_WRITE)
        {
            queue_entry->m_dirty = true;
        }

        m_mshr_entries.insert(std::make_pair(req, queue_entry));
        if(m_mshr_addr.find(req.m_addr) != m_mshr_addr.end())
        {
            m_mshr_addr[req.m_addr].push_back(queue_entry);
        }
        else
        {
            m_mshr_addr[req.m_addr] = std::list<QueueEntry*>();
            m_mshr_addr[req.m_addr].push_back(queue_entry);
        }

//        std::cout << "unordered map : " << m_mshr_addr.size();
//        std::cout << "addr size : " << m_mshr_addr[req.m_addr].size() << std::endl;

	    auto it = m_mshr_entries.find(req);
	    //Ensure insertion in the MSHR
	    assert(it != m_mshr_entries.end());

        //Ensure insertion in the MSHR address list
        assert(m_mshr_addr.find(req.m_addr) != m_mshr_addr.end());

		(*num_tr_misses) += (is_translation);
		(*num_data_misses) += (!is_translation);

		(*num_tr_accesses)  += (is_translation);
		(*num_data_accesses) += (!is_translation);

		//----Check for migration in lower disk/NVM
		if (m_cache_sys->is_last_level(m_cache_level) && !is_translation && !m_cache_sys->get_is_translation_hier())
		{
			assert(m_core_id == -1);

			trace_tlb_tid_entry_t trace_entry;
			trace_entry.va        = (uint64_t) req.m_addr;
			trace_entry.ts        = (uint64_t) 0;
			trace_entry.write     = !req.m_is_read;
			trace_entry.large     = req.m_is_large;
			trace_entry.tid       = (uint64_t) req.m_tid;
			int eviction_count	  = 0;
#if 0
					memFile_ptr_->write((char*)&trace_entry, 1);
#endif

			//std::cout << "Mem Access : " << req;

			if (!req.m_is_large && page_migration_model_->processPage(&req, eviction_count))
			{

				assert(eviction_count == 1 || eviction_count == 2);

				for (int i = 0 ; i < eviction_count; i++)
				{
					Request * mig_req = new Request(req.m_addr,
												TRANSLATION_WRITE, req.m_tid, req.m_is_large, req.m_core_id);

					if (mig_req->m_core_id == -1)
					{
						std::cout << "[ERROR] Invalid core id. 1";
						exit(0);
					}

					mig_req->is_migration_shootdown = true;

					if (m_tp_ptr->migration_iter%4 != 0) //25% of all migration shootdowns are guest
					{
						std::cout << "[SHOOTDOWN_EVENT] Host shootdown\n";
						mig_req->is_guest_shootdown = false;
					}
					else
					{
						m_tp_ptr->migration_iter = 0;
					}

					m_tp_ptr->addition_to_migration_queue++;
					m_tp_ptr->add_to_migration_shootdown_queue(mig_req);
					m_tp_ptr->migration_iter++;
				}
			}

			(*mem_accesses)++;

			if (mem_accesses->get_val()%1000000 == 0)
				std::cout << "[MEMWRITE] Mem Accesses " << mem_accesses->get_val() << std::endl;

        }
        if(m_cache_type == TRANSLATION_ONLY && ((m_cache_level == 1) || (m_cache_level == 2)))
        {
            m_tp_ptr->add_to_presence_map(req);
        }

        #ifdef DEADLOCK_DEBUG
        if(req.m_addr == 0x0)
        {
            std::cout << "[LOOKUPANDFILL_MISS] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << req << std::dec;
        }
        #endif

    }
    else
    {
        //MSHR full
        return REQUEST_RETRY;
    }
    
    //We are in upper levels of TLB/cache and we aren't doing writeback.
    //Go to lower caches and do lookup.
    if(!m_cache_sys->is_last_level(m_cache_level) && !mshr_hit)
    {
        std::shared_ptr<Cache> lower_cache = find_lower_cache_in_core(addr, is_translation, is_large);
        if(lower_cache != nullptr)
        {
            CacheType lower_cache_type = lower_cache->get_cache_type();
            bool is_tr_to_dat_boundary = (m_cache_type == TRANSLATION_ONLY) && (lower_cache_type == DATA_AND_TRANSLATION);
            //TODO: YMARATHE: eliminate this copy in all cases
            if(is_tr_to_dat_boundary)
            {
                Request req_copy(m_core->getL3TLBAddr(addr, txn_kind, tid, is_large), req.m_type, req.m_tid, req.m_is_large, req.m_core_id);
                lower_cache->lookupAndFillCache(req_copy, curr_latency + m_latency_cycles);
            }
            else
            {
                lower_cache->lookupAndFillCache(req, curr_latency + m_latency_cycles);
            }
        }
    }
    //We are in last level of cache hier and handling a data entry
    //We are in last level of TLB hier and handling a TLB entry
    //Go to memory.
    else if(!mshr_hit && ((m_cache_sys->is_last_level(m_cache_level) && !is_translation && !m_cache_sys->get_is_translation_hier()) || \
            (m_cache_sys->is_last_level(m_cache_level) && is_translation && (m_cache_sys->get_is_translation_hier()))))
    {
        req.add_callback(m_callback);
        std::shared_ptr<Request> r = std::make_shared<Request>(req);
//        uint64_t deadline = m_cache_sys->m_clk + curr_latency + m_cache_sys->m_memory_latency;

        uint64_t deadline = 0;
        if (!page_migration_model_->is_page_in_nvm(&req))
        	deadline = m_cache_sys->m_clk + curr_latency + m_cache_sys->m_memory_latency;
        else
        	deadline = m_cache_sys->m_clk + curr_latency + 800;
        
        //If element already exists in the list, move deadline.
        while(m_cache_sys->m_wait_list.find(deadline) != m_cache_sys->m_wait_list.end())
        {
            deadline++;
        }
        m_cache_sys->m_wait_list.insert(std::make_pair(deadline, r));

    }
    //We are in last level of cache hier and translation entry and not doing writeback.
    //Go to L3 TLB.
    else if(!mshr_hit)
    {
        std::shared_ptr<Cache> lower_cache = find_lower_cache_in_core(addr, is_translation, is_large);
        if(lower_cache != nullptr)
        {
            lower_cache->lookupAndFillCache(req, curr_latency + m_latency_cycles);
        }
    }
    
    return (mshr_hit) ? MSHR_HIT : REQUEST_MISS;
}

void Cache::add_lower_cache(const std::weak_ptr<Cache>& c)
{
    m_lower_cache = c;
}

void Cache::add_higher_cache(const std::weak_ptr<Cache>& c)
{
    m_higher_caches.push_back(c);
}

void Cache::set_level(unsigned int level)
{
    m_cache_level = level;
    for(int i = 0; i < m_num_sets; i++)
    {
        std::vector<CacheLine> set = m_tagStore[i];
        for(int j = 0; j < m_associativity; j++)
        {
            set[j].m_coherence_prot->set_level(level);
            assert(set[j].m_coherence_prot->get_level() == m_cache_level);
        }
    }}

unsigned int Cache::get_level()
{
    return m_cache_level;
}

void Cache::printContents()
{
    for(auto &set: m_tagStore)
    {
        for(auto &line : set)
        {
            std::cout << *(line.m_coherence_prot) << line;
        }
        std::cout << std::endl;
    }
}

void Cache::set_cache_sys(CacheSys *cache_sys)
{
    m_cache_sys = cache_sys;
}

void Cache::release_lock(std::shared_ptr<Request> r)
{
    auto it = m_mshr_entries.find(*r);

    #ifdef DEADLOCK_DEBUG
    if(r->m_addr == 0x0)
    {
        std::cout << "[RELEASE_LOCK] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << (*r) << std::dec;
    }
    #endif

    if(it != m_mshr_entries.end())
    {
        #ifdef DEADLOCK_DEBUG
        if(r->m_addr == 0x0)
        {
            std::cout << "[RELEASE_LOCK_MSHR] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << (*r) << std::dec;
        }
        #endif

        //std::cout << "Request for release_lock " << *r;

        uint64_t addr = r->m_addr;
        kind txn_kind = r->m_type;
        uint64_t tid = r->m_tid;
        bool is_large = r->m_is_large;
        unsigned int index = get_index(r->m_addr);
        unsigned int tag = get_tag(r->m_addr);
        int req_core_id = r->m_core_id;

        std::vector<CacheLine>& set = m_tagStore[index];
        unsigned int insert_pos = m_repl->getVictim(set, index);
        CacheLine &line = set[insert_pos];
        QueueEntry *q = it->second;

//        std::cout << "Evicting in MSHR entries " << line << " from idx " << index
//        		<< " tag " << tag << " cache level " << m_cache_level << std::endl;

        evict(index, line, req_core_id);

        m_repl->updateReplState(index, insert_pos);

        line.valid = true;
        line.lock = false;
        line.tag = tag;
        line.is_translation = (txn_kind == TRANSLATION_READ) || (txn_kind == TRANSLATION_WRITE) || (txn_kind == TRANSLATION_WRITEBACK);
        line.is_large = is_large;
        line.tid = tid;
        line.dirty = q->m_dirty || (((txn_kind == TRANSLATION_WRITE) || (txn_kind == DATA_WRITE)) && (m_cache_level == 1)) || (txn_kind == DATA_WRITEBACK);
        //If cache type is TRANSLATION_ONLY, include co-tag
        line.cotag = (m_cache_type == TRANSLATION_ONLY) ? m_core->getL3TLBAddr(addr, txn_kind, tid, is_large, false) : -1;

        //If we are in the last level cache and queue_entry has been made core agnostic, make the upstream request core agnostic.
        if(q->m_is_core_agnostic && m_core_id == -1)
            r->m_is_core_agnostic = true;

        CoherenceState propagate_coh_state = q->m_coh_state;

        CoherenceAction coh_action = line.m_coherence_prot->setNextCoherenceState(txn_kind, propagate_coh_state);

        handle_coherence_action(coh_action, *r, 0, true);

        assert(m_mshr_addr.find(r->m_addr) != m_mshr_addr.end());

        m_mshr_addr[r->m_addr].remove(q);

        if(m_mshr_addr[r->m_addr].size() == 0)
        {
            m_mshr_addr.erase(r->m_addr);
        }

        delete(q);
        
        m_mshr_entries.erase(it);
        
        //Ensure erasure in the MSHR
        assert(m_mshr_entries.find(*r) == m_mshr_entries.end());
    }

    it = m_wb_entries.find(*r);

    if(it != m_wb_entries.end())
    {
        #ifdef DEADLOCK_DEBUG
        if(r->m_addr == 0x0)
        {
            std::cout << "[RELEASE_LOCK_WB] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << (*r) << std::dec;
        }
        #endif

        uint64_t addr = r->m_addr;
        kind txn_kind = r->m_type;
        uint64_t tid = r->m_tid;
        bool is_large = r->m_is_large;
        unsigned int index = get_index(r->m_addr);
        unsigned int tag = get_tag(r->m_addr);
        std::vector<CacheLine>& set = m_tagStore[index];
        int req_core_id = r->m_core_id;

        unsigned int insert_pos = m_repl->getVictim(set, index);
        CacheLine &line = set[insert_pos];

        Request req = *r;

        req.m_addr = ((line.tag << m_num_line_offset_bits) << m_num_index_bits) | (index << m_num_line_offset_bits);
        req.m_tid = line.tid;
        req.m_is_large = line.is_large;

        CoherenceState propagate_coh_state = it->second->m_coh_state;

        CoherenceAction coh_action = line.m_coherence_prot->setNextCoherenceState(txn_kind, propagate_coh_state);

        handle_coherence_action(coh_action, req, 0, true);

//        std::cout << "Evicting in WB entries " << line << " from idx " << index
//        		<< " tag " << tag << " cache level " << m_cache_level << std::endl;

        evict(index, line, req_core_id);

        m_repl->updateReplState(index, insert_pos);

        line.valid = true;
        line.lock = false;
        line.tag = tag;
        line.is_translation = (txn_kind == TRANSLATION_READ) || (txn_kind == TRANSLATION_WRITE) || (txn_kind == TRANSLATION_WRITEBACK);
        line.is_large = is_large;
        line.tid = tid;
        line.dirty = (((txn_kind == TRANSLATION_WRITE) || (txn_kind == DATA_WRITE)) && (m_cache_level == 1)) || (txn_kind == DATA_WRITEBACK) || (it->second->m_dirty);
        //If cache type is TRANSLATION_ONLY, include co-tag
        line.cotag = (m_cache_type == TRANSLATION_ONLY) ? m_core->getL3TLBAddr(addr, txn_kind, tid, is_large, false) : -1;

        //If we are in the last level cache and queue_entry has been made core agnostic, make the upstream request core agnostic.
        if(it->second->m_is_core_agnostic && m_core_id == -1)
            r->m_is_core_agnostic = true;

        delete(it->second);

        m_wb_entries.erase(it);

        //Ensure erasure in the MSHR
        assert(m_wb_entries.find(*r) == m_wb_entries.end());
    }

    //We are in L1
    if(m_cache_level == 1 && m_cache_type == DATA_ONLY)
    {
        #ifdef DEADLOCK_DEBUG
        if(r->m_addr == 0x0)
        {
            std::cout << "[RELEASE_LOCK_MEM_DONE] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << (*r) << std::dec;
        }
        #endif

        m_core->m_rob->mem_mark_done(*r);
    }

    if(m_cache_level == 1 && m_cache_type == TRANSLATION_ONLY && r->is_translation_request())
    {
        #ifdef DEADLOCK_DEBUG
        if(r->m_addr == 0x0)
        {
            std::cout << "[RELEASE_LOCK_MEM_TR_DONE] At clk = " << m_core->m_clk << ", in hier = " << m_cache_sys->get_is_translation_hier() << ", in level = " << m_cache_level << ", in core = " << m_core_id << ", request = " << std::hex << (*r) << std::dec;
        }
        #endif

        m_core->m_rob->mem_mark_translation_done(*r);
    }

    propagate_release_lock(r);
}

unsigned int Cache::get_latency_cycles()
{
    return m_latency_cycles;
}

bool Cache::handle_coherence_action(CoherenceAction coh_action, Request &r, unsigned int curr_latency, bool same_cache_sys)
{
    uint64_t addr = r.m_addr;
    uint64_t tid = r.m_tid;
    bool is_large = r.m_is_large;
    bool is_translation = (r.m_type == TRANSLATION_READ) || (r.m_type == TRANSLATION_WRITE);
    bool needs_state_correction = false;

    if(same_cache_sys && (coh_action == MEMORY_TRANSLATION_WRITEBACK || coh_action == MEMORY_DATA_WRITEBACK))
    {
    }
    
    else if(coh_action == BROADCAST_DATA_READ || coh_action == BROADCAST_DATA_WRITE || \
            coh_action == BROADCAST_TRANSLATION_READ || coh_action == BROADCAST_TRANSLATION_WRITE
            )
    {
        //Update caches in all other cache hierarchies if call is from the same hierarchy
        //Pass addr and coherence action enforced by this cache
        if(same_cache_sys && !m_cache_sys->is_last_level(m_cache_level))
        {
            for(int i = 0; i < m_cache_sys->m_other_cache_sys.size(); i++)
            {
                std::shared_ptr<Request> req = std::make_shared<Request>(r);

                if (m_core_id == -1)
                	std::cout << "Setting req core id to " << m_core_id << std::endl;

                req->m_core_id = m_core_id;

                //If TLBs are relaying coherence update, relay co-tag address
                req->m_addr = (m_cache_type == TRANSLATION_ONLY) ? m_core->getL3TLBAddr(addr, r.m_type, tid, is_large, false) : req->m_addr;

                //TODO: Apply optimization to relay coherence updates to TLBs only on translation requests here
                m_cache_sys->m_other_cache_sys[i]->m_coh_act_list.insert(std::make_pair(req, coh_action));
            }
        }
        //Coherence in data caches is enforced by address
        else if(!same_cache_sys && (m_cache_type != TRANSLATION_ONLY))
        {
            //If call came from different cache hierarchy, handle it!
            //Invalidate if we see BROADCAST_*_WRITE
            unsigned int hit_pos;
            uint64_t tag = get_tag(addr);
            uint64_t index = get_index(addr);
            std::vector<CacheLine>& set = m_tagStore[index];
            //We might still be looking at a TRANSLATION_ENTRY because L2 and L3 are DATA_AND_TRANSLATION
            bool is_translation = (coh_action == BROADCAST_TRANSLATION_WRITE) || (coh_action == BROADCAST_TRANSLATION_READ);
            
            if(is_found(set, tag, is_translation, tid, hit_pos))
            {
                CacheLine &line = set[hit_pos];
                kind coh_txn_kind = txnKindForCohAction(coh_action);
                line.m_coherence_prot->setNextCoherenceState(coh_txn_kind);
                if(coh_txn_kind == DIRECTORY_DATA_WRITE || coh_txn_kind == DIRECTORY_TRANSLATION_WRITE)
                {
                    line.valid = false;
                    assert(line.m_coherence_prot->getCoherenceState() == INVALID);
                }
                needs_state_correction = (coh_action == BROADCAST_DATA_READ) || (coh_action == BROADCAST_TRANSLATION_READ);
            }
        }
        //Translation coherence is enforced by co-tags
        else if(!same_cache_sys && (m_cache_type == TRANSLATION_ONLY) && (m_tp_ptr->config == "HATRIC"))
        {
            assert(m_cache_sys->get_is_translation_hier());
            unsigned int index, hit_pos;
            
            if(is_found_by_cotag(addr, tid, index, hit_pos))
            {
                CacheLine &line = m_tagStore[index][hit_pos];
                kind coh_txn_kind = txnKindForCohAction(coh_action);
                line.m_coherence_prot->setNextCoherenceState(coh_txn_kind);
                if(coh_txn_kind == DIRECTORY_TRANSLATION_WRITE)
                {
                    std::cout << "[SHOOTDOWN] Invalidate line via co-tag on core " << m_core_id << ", level = " << m_cache_level << " : " <<  std::hex << r << std::dec;
                    
                    line.valid = false;
                    assert(line.m_coherence_prot->getCoherenceState() == INVALID);

                    if(m_cache_sys->get_is_translation_hier() && m_cache_sys->is_penultimate_level(m_cache_level))
                    {
                        std::cout << "[COHERENCE_INVALIDATION] Removing from presence map: " << std::hex << "Addr = " << addr << std::dec << ", tid = " << tid << ", is_large = " << is_large << ", on core = " << m_core_id << "\n";
                        m_tp_ptr->remove_from_presence_map(addr, tid, is_large, m_core_id);
                    }
                }
                needs_state_correction = (coh_action == BROADCAST_TRANSLATION_READ);
            }
            (*num_data_coh_msgs) += (!is_translation);
            (*num_tr_coh_msgs) += (is_translation);
        }
        else if(!same_cache_sys && (m_cache_type == TRANSLATION_ONLY))
        {
            assert(m_cache_sys->get_is_translation_hier());
            unsigned int index, hit_pos;

            if(is_translation)
            {
                unsigned int pom_tlb_set_index = (is_large) ? (addr - m_core->m_l3_small_tlb_base - m_core->m_l3_small_tlb_size)/(16 * 4) : (addr - m_core->m_l3_small_tlb_base)/(16 * 4);
                assert(pom_tlb_set_index >= 0);
                unsigned int check_index, hit_pos;
                unsigned int index = (pom_tlb_set_index) % (m_num_sets);

                if(is_found_by_cotag(addr, tid, check_index, hit_pos))
                {
                    assert(index == check_index);
                }

                for(int j = 0; j < m_associativity; j++)
                {
                    CacheLine &line = m_tagStore[index][j];
                    kind coh_txn_kind = txnKindForCohAction(coh_action);
                    line.m_coherence_prot->setNextCoherenceState(coh_txn_kind);
                    if(coh_txn_kind == DIRECTORY_TRANSLATION_WRITE)
                    {
                        //std::cout << "[SHOOTDOWN] Invalidate line via co-tag on core " << m_core_id << ", level = " << m_cache_level << " : " <<  std::hex << r << std::dec;

                        line.valid = false;
                        assert(line.m_coherence_prot->getCoherenceState() == INVALID);

                        if(m_cache_sys->get_is_translation_hier() && m_cache_sys->is_penultimate_level(m_cache_level))
                        {
                            //std::cout << "[COHERENCE_INVALIDATION] Removing from presence map: " << std::hex << "Addr = " << addr << std::dec << ", tid = " << tid << ", is_large = " << is_large << ", on core = " << m_core_id << "\n";
                            m_tp_ptr->remove_from_presence_map(addr, tid, is_large, m_core_id);
                        }
                    }
                    needs_state_correction = (coh_action == BROADCAST_TRANSLATION_READ);
                }
            }

			(*num_data_coh_msgs) += (!is_translation);
			(*num_tr_coh_msgs) += (is_translation);
        }
    }
    else if(coh_action == STATE_CORRECTION)
    {
        if(same_cache_sys)
        {
            unsigned int originating_core = r.m_core_id;
            assert(originating_core != m_core_id);
            unsigned int index = (originating_core < m_core_id) ? originating_core : (originating_core - m_core_id - 1);
            //Since we are sending back the request that arrived, don't change the request address here
            m_cache_sys->m_other_cache_sys[index]->m_coh_act_list.insert(std::make_pair(std::make_shared<Request>(r), coh_action));
            if(!m_cache_sys->get_is_translation_hier())
            {
                m_cache_sys->m_other_cache_sys[(index + NUM_CORES - 1)]->m_coh_act_list.insert(std::make_pair(std::make_shared<Request>(r), coh_action));
            }
        }
        else
        {
            if(m_cache_type != TRANSLATION_ONLY)
            {
                unsigned int hit_pos;
                uint64_t tag = get_tag(addr);
                uint64_t index = get_index(addr);
                std::vector<CacheLine>& set = m_tagStore[index];
                
                if(is_found(set, tag, is_translation, tid, hit_pos))
                {
                    CacheLine &line = set[hit_pos];
                    line.m_coherence_prot->forceCoherenceState(SHARED);
                }
            }
            else if (m_tp_ptr->config == "HATRIC")
            {
                unsigned int index, hit_pos;
                if(is_found_by_cotag(addr, tid, index, hit_pos))
                {
                    CacheLine &line = m_tagStore[index][hit_pos];
                    line.m_coherence_prot->forceCoherenceState(SHARED);
                }
            }
            else
            {
                if(is_translation)
                {
                    unsigned int pom_tlb_set_index = (is_large) ? (addr - m_core->m_l3_small_tlb_base - m_core->m_l3_small_tlb_size)/(16 * 4) : (addr - m_core->m_l3_small_tlb_base)/(16 * 4);
                    assert(pom_tlb_set_index >= 0);
                    unsigned int check_index, hit_pos;
                    unsigned int index = (pom_tlb_set_index) % (m_num_sets);
                    for(int j = 0; j < m_associativity; j++)
                    {
                        CacheLine &line = m_tagStore[index][j];
                        line.m_coherence_prot->forceCoherenceState(SHARED);
                    }
                }
            }
        }
    }
    
    return needs_state_correction;
}

void Cache::set_cache_type(CacheType cache_type)
{
    m_cache_type = cache_type;
}

CacheType Cache::get_cache_type()
{
    return m_cache_type;
}

void Cache::set_core(std::shared_ptr<Core>& coreptr)
{
    m_core = coreptr;
}

std::shared_ptr<Cache> Cache::find_lower_cache_in_core(uint64_t addr, bool is_translation, bool is_large)
{
    std::shared_ptr<Cache> lower_cache;

    //Check if lower cache is statically determined
    try
    {
        lower_cache = m_lower_cache.lock();
    }
    catch(std::bad_weak_ptr &e)
    {
        std::cout << "Cache " << m_cache_level << "doesn't have a valid lower cache" << std::endl;
    }
    
    //Determine lower_cache dynamically based on the type of transaction
    if(lower_cache == nullptr)
    {
        lower_cache = m_core->get_lower_cache(addr, is_translation, is_large, m_cache_level, m_cache_type);
    }
    
    return lower_cache;
}

void Cache::propagate_release_lock(std::shared_ptr<Request> r)
{
    uint64_t addr_memory = 0;
    bool addr_memory_init = false;

    try
    {
        for(int i = 0; i < m_higher_caches.size(); i++)
        {
            auto higher_cache = m_higher_caches[i].lock();
            if(higher_cache != nullptr)
            {
                //[1st and 2nd condition] :
                //Process higher_cache->release_lock only for appropriate cache type [Translation = DATA_AND_TRANSLATION, TRANSLATION_ONLY], [Data = DATA_AND_TRANSLATION, DATA_ONLY]
                
                //If we are in last level cache in data hier, and request is not core agnostic, check originating core id.
                //If we are in last level cache in data hier, and request is core agnostic, dont check originating core id.
                //If we are in last level of translation hier, or any other level, process.
                bool is_last_data_level = m_cache_sys->is_last_level(m_cache_level) && !m_cache_sys->get_is_translation_hier();
                bool is_last_tr_level = m_cache_sys->is_last_level(m_cache_level) && m_cache_sys->get_is_translation_hier();
                
                if(((r->is_translation_request() && higher_cache->get_cache_type() != DATA_ONLY) ||
                    ((r->m_type == TRANSLATION_WRITE) && higher_cache->get_cache_type() == DATA_ONLY) ||
                    (!r->is_translation_request() && higher_cache->get_cache_type() != TRANSLATION_ONLY)) && \
                    ((is_last_data_level && (r->m_core_id == higher_cache->get_core_id()) && !r->m_is_core_agnostic) ||
                    (is_last_data_level && r->m_is_core_agnostic) ||
                    !m_cache_sys->is_last_level(m_cache_level) || is_last_tr_level))
                {
                    //When we go to higher cache from L3 data cache, we lose the address in r.
                    //So add some memory here and recall.
                    if(is_last_data_level && !addr_memory_init)
                    {
                        addr_memory_init = true;
                        addr_memory = r->m_addr;
                    }
                    else if(is_last_data_level)
                    {
                        r->m_addr = addr_memory;
                    }
                    
                    //Is this is data to translation boundary? i.e. from L2D$ to L2 TLB?
                    bool is_dat_to_tr_boundary = (m_cache_type == DATA_AND_TRANSLATION) && (higher_cache->get_cache_type() == TRANSLATION_ONLY);
                    
                    //Are we fully in translation hierarchy? i.e L1 TLB to L2 TLB
                    bool in_translation_hier = (m_cache_type == TRANSLATION_ONLY) && (higher_cache->get_cache_type() == TRANSLATION_ONLY);
                
                    bool is_higher_cache_small_tlb = !higher_cache->get_is_large_page_tlb();
                    
                    //If we are fully in translation hierarchy, propagate access to the right TLB i.e. small/large
                    //If we are not, propagate access anyway.
                    bool propagate_access = ((in_translation_hier && (r->m_is_large == !is_higher_cache_small_tlb)) || !in_translation_hier);
                    
                    // If data to translation boundary, obtain reverse mapping
                    if(is_dat_to_tr_boundary)
                    {
                        std::vector<uint64_t> access_addresses = m_core->retrieveAddr(r->m_addr, r->m_type, r->m_tid, r->m_is_large, is_higher_cache_small_tlb);
                        for(int i = 0; i < access_addresses.size(); i++)
                        {
                            r->m_addr = (propagate_access) ? access_addresses[i] : r->m_addr;
                            if(propagate_access)
                            {
                                higher_cache->release_lock(r);
                            }
                        }
                    }
                    else
                    {
                        //If we need to propagate access, obtain correct address [reverse mapped, or r->m_addr]
                        if(propagate_access)
                        {
                            higher_cache->release_lock(r);
                        }
                    }
                }
            }
        }
    }
    catch(std::bad_weak_ptr &e)
    {
        std::cout << e.what() << std::endl;
    }
}

bool Cache::get_is_large_page_tlb()
{
    return m_is_large_page_tlb;
}

void Cache::set_core_id(unsigned int core_id)
{
    m_core_id = core_id;
}

unsigned int Cache::get_core_id()
{
    return m_core_id;
}

void Cache::initialize_callback()
{
     m_callback = std::bind(&Cache::release_lock, this, std::placeholders::_1);
}

bool Cache::is_found_by_cotag(uint64_t pom_tlb_addr, uint64_t tid, unsigned int &index, unsigned int &hit_pos)
{
    for(int i = 0; i < m_num_sets; i++)
    {
        for(int j = 0; j < m_associativity; j++)
        {
            CacheLine &line = m_tagStore[i][j];
            if(line.cotag == pom_tlb_addr && line.tid == tid)
            {
                index = i;
                hit_pos = j;
                return true; 
            }
        }
    }
    return false;
}

void Cache::add_traceprocessor(TraceProcessor *tp)
{
    m_tp_ptr = tp;

    for(int i = 0; i < module_counters.size(); i++)
    {
    	module_counters[i]->set_tp(tp);
    }
}

void Cache::add_mem_file_ptr(std::ofstream * memFile)
{
	memFile_ptr_ = memFile;
}

void Cache::add_migration_model(std::shared_ptr <migration_model> page_migration_model)
{
	page_migration_model_ = page_migration_model;
}

std::ofstream * Cache::get_mem_file_ptr()
{
	return memFile_ptr_;
}

TraceProcessor* Cache::get_traceprocessor()
{
    return m_tp_ptr;
}
