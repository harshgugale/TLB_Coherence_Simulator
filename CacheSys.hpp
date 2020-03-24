//
//  CacheSys.hpp
//  TLB-Coherence-Simulator
//
//  Created by Yashwant Marathe on 12/7/17.
//  Copyright © 2017 Yashwant Marathe. All rights reserved.
//

#ifndef CacheSys_hpp
#define CacheSys_hpp

#include <iostream>
#include <vector>
#include <assert.h>
#include <map>
#include "Request.hpp"

class Cache;
class Core;

enum {
    L1_HIT_ID,
    L2_HIT_ID,
    L3_HIT_ID,
    NUM_MAX_CACHES,
    MEMORY_ACCESS_ID,
    CACHE_TO_CACHE_ID,
    NUM_POPULATED_LATENCIES
};

class CacheSys {
public:
    
    std::vector<std::shared_ptr<Cache>> m_caches;
    
    //This is where requests wait until they are served a memory access
    std::map<uint64_t, std::shared_ptr<Request>> m_wait_list;
    
    //This is where requests wait until they are served by a hit
    std::map<uint64_t, std::shared_ptr<Request>> m_hit_list;
    
    //This is where coherence actions wait until they are served
    std::map<std::shared_ptr<Request>, CoherenceAction> m_coh_act_list;
    
    uint64_t m_memory_latency;
    uint64_t m_cache_to_cache_latency;
    uint64_t m_nvm_latency;
    
    std::vector<std::shared_ptr<CacheSys>> m_other_cache_sys;
    
    int m_core_id;
    
    std::shared_ptr<Core> m_core;
    
    bool m_is_translation_hier;

    uint64_t m_clk = 0;
    
    CacheSys(bool is_translation_hier,
    		uint64_t memory_latency = 200,
			uint64_t cache_to_cache_latency = 50) :

    m_is_translation_hier(is_translation_hier),
	m_memory_latency(memory_latency),
	m_cache_to_cache_latency(cache_to_cache_latency)
    {
        m_clk = 0;
        m_core_id = 0;
        m_nvm_latency = 800;
    }
    
    void add_cache_to_hier(std::shared_ptr<Cache> c);
    
    void add_cachesys(std::shared_ptr<CacheSys> cs);
    
    void set_core(std::shared_ptr<Core>& coreptr);
    
    void tick();
    
    bool is_last_level(unsigned int cache_level);
    
    bool is_penultimate_level(unsigned int cache_level);
    
    void printContents();
    
    void set_core_id(int core_id);
    
    RequestStatus lookupAndFillCache(Request &r);
    
    bool get_is_translation_hier();
    
    unsigned int get_core_id();

    void clflush(const uint64_t addr, uint64_t tid, bool is_translation);

    void tlb_invalidate(uint64_t addr, uint64_t tid, bool is_large);

    bool is_done();
    
};

#endif /* CacheSys_hpp */
