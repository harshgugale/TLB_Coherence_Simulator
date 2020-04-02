//
//  Core.hpp
//  TLB-Coherence-Simulator
//
//  Created by Yashwant Marathe on 12/14/17.
//  Copyright © 2017 Yashwant Marathe. All rights reserved.
//

#ifndef Core_hpp
#define Core_hpp

#include <iostream>
#include "CacheSys.hpp"
#include "ROB.hpp"
#include "Request.hpp"
#include <deque>
#include <list>
#include <set>
#include "counter.hpp"

class Core {
private:
    
    class AddrMapKey {
    public:
        uint64_t m_addr;
	    uint64_t m_tid;
        kind m_type;
        bool m_is_large;
        
        AddrMapKey(uint64_t addr, kind type, uint64_t tid, bool is_large) : m_addr(addr), m_type(type), m_tid(tid), m_is_large(is_large) {}
        
        friend std::ostream& operator << (std::ostream& out, AddrMapKey &a)
        {
            out << "|" << a.m_addr << "|" << a.m_type << "|" << a.m_tid << "|" << a.m_is_large << "|" << std::endl;
            return out;
        }
    };
    
    class AddrMapComparator {
    public:
        bool operator () (const AddrMapKey &a, const AddrMapKey &b) const
        {
            if(a.m_addr < b.m_addr)
                return true;
            if(a.m_type < b.m_type)
                return true;
            if(a.m_tid < b.m_tid)
                return true; 
            if(a.m_is_large < b.m_is_large)
                return true;
            
            return false;
        }
    };
    
    std::shared_ptr<CacheSys> m_cache_hier;
    std::shared_ptr<CacheSys> m_tlb_hier;
    
    unsigned int m_core_id = 0;
    
    std::map<uint64_t, std::set<AddrMapKey, AddrMapComparator>> va2L3TLBAddr;

    unsigned int tr_coh_issue_ptr = 0;

    std::vector<std::shared_ptr<Core>> m_other_cores;

public:
    uint64_t m_l3_small_tlb_base = 0x0;
    uint64_t m_l3_small_tlb_size = 16 * 1024 * 1024;
    bool stall = false;
    bool tr_wr_in_progress = false;
    std::shared_ptr<ROB> m_rob;
    std::deque<Request*> traceVec;
    uint64_t m_num_issued  = 0;
    uint64_t m_num_retired = 0;
    uint64_t m_clk = 0;
    uint64_t tlb_shootdown_penalty = 0;
    uint64_t tlb_shootdown_addr = 0;
    uint64_t actual_shootdown_identifier = 0xffffffffffffffff;
    uint64_t tlb_shootdown_tid = 0;
    bool tlb_shootdown_is_large = false;
    uint64_t num_stall_cycles_per_shootdown = 0;
    uint64_t outstanding_shootdowns = 0;
    TraceProcessor * m_tp_ptr = nullptr;
    bool is_stall_guest_shootdown = true;

    //Counters

    std::shared_ptr <counter> instructions_retired;
    std::shared_ptr <counter> trace_vec_pops_or_instr_issued;
    std::shared_ptr <counter> num_cycles;
    std::shared_ptr <counter> num_stall_cycles;
    std::shared_ptr <counter> num_false_invalidations;
    std::shared_ptr <counter> num_tr_invalidations;
    std::shared_ptr <counter> num_shootdown;
    std::shared_ptr <counter> num_guest_shootdowns;
    std::shared_ptr <counter> num_host_shootdowns;
    std::shared_ptr <counter> page_invalidations;

    std::vector <counter *> module_counters;

    Core(std::shared_ptr<CacheSys> cache_hier, std::shared_ptr<CacheSys> tlb_hier, std::shared_ptr<ROB> rob,
    		uint64_t l3_small_tlb_base = 0xf00000000000, uint64_t l3_small_tlb_size = 1024 * 1024) :
        m_cache_hier(cache_hier),
        m_tlb_hier(tlb_hier),
        m_rob(rob),
        m_l3_small_tlb_base(l3_small_tlb_base),
        m_l3_small_tlb_size(l3_small_tlb_size)
        {
            assert(!cache_hier->get_is_translation_hier());
            assert(tlb_hier->get_is_translation_hier());
            m_clk = 0;
            stall = false;
            tr_wr_in_progress = false;

            instructions_retired = std::make_shared <counter>("Instructions",module_counters);
            trace_vec_pops_or_instr_issued = std::make_shared <counter>("Trace Vec pops/Instruction Issued",module_counters);
            num_cycles = std::make_shared <counter>("Cycles",module_counters);
            num_stall_cycles = std::make_shared <counter>("Stall Cycles",module_counters);
            num_false_invalidations = std::make_shared <counter>("Num False Invalidations",module_counters);
            num_tr_invalidations = std::make_shared <counter>("Num Translation Invalidations",module_counters);
            num_shootdown = std::make_shared <counter>("Num shootdowns",module_counters);
            num_guest_shootdowns = std::make_shared <counter>("Num Guest Shootdowns",module_counters);
            num_host_shootdowns = std::make_shared <counter>("Num Host Shootdowns",module_counters);
            page_invalidations = std::make_shared <counter>("Num Page Invalidations",module_counters);


//            module_counters.push_back(&instructions_retired);
//            module_counters.push_back(&num_cycles);
        }
    
    bool interfaceHier(bool ll_interface_complete);
    
    void set_core_id(unsigned int core_id);
    
    void add_traceprocessor(TraceProcessor * tp);

    uint64_t getL3TLBAddr(uint64_t va, kind type, uint64_t pid, bool is_large, bool insert = true);
    
    std::vector<uint64_t> retrieveAddr(uint64_t l3tlbaddr, kind type, uint64_t pid, bool is_large, bool is_higher_cache_small_tlb);
    
    std::shared_ptr<Cache> get_lower_cache(uint64_t addr, bool is_translation, bool is_large, unsigned int cache_level, CacheType cache_type);
    
    void tick(std::string config, uint64_t initiator_penalty, uint64_t victim_penalty);

    void add_trace(Request *req);

    bool is_done();

    bool must_add_trace();

    void tlb_invalidate(uint64_t addr, uint64_t tid, bool is_large);

    void add_core(std::shared_ptr<Core> other_core);
};

#endif /* Core_hpp */
