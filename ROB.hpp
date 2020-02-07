//
//  ROB.hpp
//  TLB-Coherence-Simulator
//
//  Created by Yashwant Marathe on 1/4/18.
//  Copyright © 2018 Yashwant Marathe. All rights reserved.
//

#ifndef ROB_hpp
#define ROB_hpp

#include<iostream>
#include<vector>
#include<list>
#include<map>
#include<deque>
#include<unordered_map>
#include "utils.hpp"
#include "Request.hpp"

class ROB {
public:
    class ROBEntry {
    public:
        bool valid;
        bool is_memory_access;
        Request *req;
        bool done;
        uint64_t clk;
        
        ROBEntry() : valid(false), is_memory_access(false), done(false), req(nullptr), clk(0) {}
        
        friend std::ostream& operator << (std::ostream& out, ROBEntry &r)
        {
            out << "|" << r.valid << "|" << r.is_memory_access << "|" << r.req << std::dec << "|" << r.done << "|" << std::dec << r.clk << "|" << std::dec << std::endl;
            return out;
        }
    };

    class ReqQueueMetaData {
        public:
            bool ready;
            uint64_t num_occ_in_req_queue;

        ReqQueueMetaData() : ready(false), num_occ_in_req_queue(1) {}
    };
    
    std::vector<ROBEntry> m_window;
    unsigned int m_issue_width = 4;
    unsigned int m_retire_width = 4;
    unsigned int m_issue_ptr = 0;
    unsigned int m_commit_ptr = 0;
    unsigned int m_num_waiting_instr = 0;
    unsigned int m_window_size = 128;
    
    std::unordered_map<Request, ReqQueueMetaData, RequestHasher> is_request_ready;
    std::deque<Request> request_queue;
    std::unordered_map <Request *, int> * request_queue_;
    
    ROB(unsigned int window_size = 128,
    		unsigned int issue_width = 4,
			unsigned int retire_width = 4)
    	: m_window_size(window_size),
        m_issue_width(issue_width),
        m_retire_width(retire_width)
    {
        for(int i = 0; i < m_window_size; i++)
        {
            m_window.push_back(ROBEntry());
        }
        
        m_issue_ptr = 0;
        m_commit_ptr = 0;
        m_num_waiting_instr = 0;
    }
    
    bool issue(bool is_memory_access, Request *r, uint64_t clk);
    bool transfer_to_data_hier(Request &r);
    unsigned int retire(uint64_t clk);
    void mem_mark_done(Request &r);
    void mem_mark_translation_done(Request &r);
    void printContents();
    bool can_issue();
    bool is_empty();
    void peek_commit_ptr();
    void peek(unsigned int ptr);
    void add_request_queue(std::unordered_map <Request *, int> * request_queue);
};

#endif /* ROB_hpp */
