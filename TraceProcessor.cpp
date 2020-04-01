//
//  TraceProcessor.cpp
//  TLB-Coherence-Simulator
//
//  Created by Yashwant Marathe on 2/7/18.
//  Copyright © 2018 Yashwant Marathe. All rights reserved.
//

#include "TraceProcessor.hpp"

void TraceProcessor::processPair(std::string name, std::string val)
{
    if (name.find("fmt") != std::string::npos)
    {
        if (val.find("m") != std::string::npos)
            strcpy(fmt, "-m");
        else
            strcpy(fmt, "-t");

        is_multicore = (strcasecmp(fmt, "-m") == 0);
    }
    
    if (name.find("cores") != std::string::npos)
        num_cores=(int)strtoul(val.c_str(), NULL, 10);
    
    for(int i = 0; i < num_cores; i++)
    {
        if (name == ("t" + std::to_string(i)))
            strcpy(trace[i], val.c_str());
        if (name == ("shootdown"))
            strcpy(shootdown, val.c_str());
        if (name == ("i" + std::to_string(i)))
            total_instructions_in_real_run[i] = strtoul(val.c_str(),NULL, 10);
        if (name == ("c" + std::to_string(i)))
            ideal_cycles_in_real_run[i] = strtoul(val.c_str(), NULL, 10);
        if (name == ("tl" + std::to_string(i)))
            num_tlb_misses_in_real_run[i] = strtoul(val.c_str(), NULL, 10);
        if (name == ("pw" + std::to_string(i)))
            avg_pw_cycles_in_real_run[i] = strtod(val.c_str(),NULL);
    }
    
    if (name == "l2d_lat")
        l2d_lat = strtoul(val.c_str(),NULL, 10);
    if (name == "l3d_lat")
        l3d_lat = strtoul(val.c_str(),NULL, 10);
    if (name == "vl_lat")
        vl_lat = strtoul(val.c_str(),NULL, 10);
    if (name == "dram_lat")
        dram_lat = strtoul(val.c_str(),NULL, 10);
    if (name == "vl_small_size")
        l3_small_tlb_size = 33554432;
    if (name == "vl_large_size")
        l3_large_tlb_size = 8388608 / 4;
    if (name == "ini_penalty")
    	ini_penalty = strtoul(val.c_str(),NULL, 10);
    if (name == "vic_penalty")
    	vic_penalty = strtoul(val.c_str(),NULL, 10);
    if (name == "benchname")
    	benchname = val.c_str();
    if (name == "env")
    	env = val.c_str();
    if (name == "config")
    {
    	if (val == "BASELINE" ||
    		val == "POMTLB" ||
			val == "IDEAL" ||
			val == "TCAT" ||
			val == "HATRIC")
    	{
    		config = val.c_str();
    	}
    	else
    	{
    		std::cout << "Please check config param";
    		exit(0);
    	}

    }
    if (name == "dram_size")
    {
    	dram_size = strtoull(val.c_str(),NULL, 10);
    }
    if (name == "nvm_disk_size")
    {
    	nvm_disk_size = strtoull(val.c_str(),NULL, 10);
    }
    if (name == "migration_threshold")
    {
    	migration_threshold = strtoull(val.c_str(),NULL, 10);
    }
    if (name == "migration_policy")
    {
    	mig_policy = val.c_str();
    }
    if (name == "skip_instructions")
    {
    	skip_instructions = strtoull(val.c_str(),NULL, 10);
    }

}

void TraceProcessor::parseAndSetupInputs(char *input_cfg)
{
    std::ifstream file(input_cfg);
    std::string str;
    while (std::getline(file, str))
    {
        // Process str
        if ((str[0]=='/') && (str[1]=='/')) continue;
        
        // split str into arg name = arg value
        std::size_t found = str.find("=");
        if (found != std::string::npos)
        {
            std::string arg_name = str.substr(0,found);
            std::string arg_value = str.substr(found+1);
            found=arg_value.find("//");
            if (found != std::string::npos)
                arg_value = arg_value.substr(0,found);
            
            // strip leading or trailing whitespaces
            arg_name = trim(arg_name);
            arg_value = trim(arg_value);
            std::cout << " Found pair: " << arg_name << " and " << arg_value << std::endl;
            processPair(arg_name, arg_value);
        }
        else
            std::cout <<"Warning! Unknown entry: " << str << " found in cfg file. Exiting..."<< std::endl;
    }
    
    if (strcasecmp(fmt, "-m") != 0)
    {
        for (int i = 0;i < num_cores; i++)
        {
            total_instructions_in_real_run[i] = total_instructions_in_real_run[i]/num_cores;
            ideal_cycles_in_real_run[i] = ideal_cycles_in_real_run[i]/num_cores;
            num_tlb_misses_in_real_run[i] = num_tlb_misses_in_real_run[i]/num_cores;
        }
    }
}

void TraceProcessor::verifyOpenTraceFiles()
{
    for (int i = 0 ; (i < num_cores) && is_multicore; i++)
    {
        trace_fp[i] = fopen((char*)trace[i], "r");
        if (!trace_fp[i])
        {
            std::cout << "[Error] Check trace file path of core " << i << std::endl;
            std::cout << trace[i] << " does not exist" << std::endl;
            exit(0);
        }
        fread ((void*)&buf1[i], sizeof(trace_tlb_entry_t), 1, trace_fp[i]);
        used_up[i] = false;
        empty_file[i] = false;
        entry_count[i] = 1;
    }

    if(!is_multicore)
    {
        trace_fp[0] = fopen((char*)trace[0], "r");
        if (!trace_fp[0])
        {
            std::cout << "[Error] Check trace file path" << std::endl;
            std::cout << trace[0] << " does not exist" << std::endl;
            exit(0);
        }
        fread ((void*)&buf2[0], sizeof(trace_tlb_tid_entry_t), 1, trace_fp[0]);
        used_up[0] = false;
        empty_file[0] = false;
        entry_count[0] = 1;
    }

    shootdown_fp = fopen((char*) shootdown, "r");
    if(!shootdown_fp)
    {
        std::cout << "[Error] Check shootdown file path" << std::endl;
        std::cout << shootdown << " does not exist" << std::endl;
        exit(0);
    }

    //Shootdown file requires at least one entry

	fscanf(shootdown_fp,"%lu,%lx,%d,%lu\n",&buf3->ts,&buf3->va,&buf3->large,&buf3->tid);
	std::cout << "[TLB_SHOOTDOWN_NEXT] = " << buf3->ts << "," << buf3->va <<
			"," << buf3->large << "," << buf3->tid << "\n";
	used_up_shootdown = false;

}

void TraceProcessor::getShootdownEntry()
{
    if(used_up_shootdown)
    {
        if(!empty_file_shootdown && !feof(shootdown_fp))
        {
            fscanf(shootdown_fp,"%lu,%lx,%d,%lu\n",&buf3->ts,&buf3->va,&buf3->large,&buf3->tid);
            std::cout << "[TLB_SHOOTDOWN_NEXT] = " << buf3->ts << "," << buf3->va <<
            		"," << buf3->large << "," << buf3->tid << "\n";
            used_up_shootdown = false;
        }
        else
        {
            std::cout << "Done with shootdown file\n";
            empty_file_shootdown = true;
        }
    }

}

int TraceProcessor::getNextEntry()
{
    int index = -1;
    uint64_t least = 0xffffffffffffffff;

    for(int i = 0; (i < num_cores) && is_multicore; i++)
    {
        if(used_up[i])
        {
            // if not end-of-file, fill next entry
            if(!empty_file[i])
            {
                if (!feof(trace_fp[i]))
                {
                    fread ((void*)&buf1[i], sizeof(trace_tlb_entry_t), 1, trace_fp[i] ); //Changed datatypes of write and tid
                    used_up[i] = false;
                    entry_count[i]++;
                }
                else
                {
                    std::cout << " Done with core " << i << std::endl;
                    empty_file[i] = true;
                }
            }
        }
        if (!empty_file[i])
        {
            if (buf1[i].ts < least)
            {
                least = buf1[i].ts;
                index = i;
            }
        }
    }

    if(!is_multicore)
    {
        index = 0;

        if(used_up[0])
        {
            if(!empty_file[0])
            {
                if(!feof(trace_fp[0]))
                {
                    fread((void*)&buf2[0], sizeof(trace_tlb_tid_entry_t), 1, trace_fp[0]);
                    used_up[0] = false;
                    entry_count[0]++;
                }
                else
                {
                    std::cout << "Done with trace " << "\n";
                    empty_file[0] = true;
                    index = -1;
                }
            }
        }
    }
    
    return index;
}

Request* TraceProcessor::generateRequest()
{
    int idx = getNextEntry();

    uint64_t shootdown_ts;
    uint64_t shootdown_va, shootdown_tid,shootdown_is_large, shootdown_ini_core = 0, shootdown_victim_cores = 0;

    if (empty_file_shootdown == false)
    {
    	getShootdownEntry();
        shootdown_ts = buf3->ts;
        shootdown_va = buf3->va;
        shootdown_is_large = buf3->large; //For actual shootdown this value is the initiator core
        shootdown_tid = buf3->tid; //For actual shootdown this value is number of victim cores

        if (shootdown_va == actual_shootdown_identifier)
        {
        	shootdown_ini_core = buf3->large;
        	shootdown_victim_cores = buf3->tid;
        }
    }

    uint64_t va, tid;
    bool is_large, is_write;
    bool is_multicore = strcasecmp(fmt, "-m") == 0;

    if(idx != -1)
    {
		va = buf2[idx].va;
		is_large = buf2[idx].large;
		is_write = (bool)((buf2[idx].write != 0)? true: false);
		curr_ts[idx] = buf2[idx].ts;
		tid = buf2[idx].tid;
		unsigned int core = (tid + tid_offset) % NUM_CORES;

		//Return requests for normal memory instructions
		if(curr_ts[idx] == global_ts)
		{
			//Threads switch about every context switch interval
			//uint64_t tid = (idx + tid_offset) % NUM_CORES;
			Request *req = new Request(va, is_write ? DATA_WRITE : DATA_READ, tid, is_large, core);
			used_up[idx] = true;

			//std::cout << "TS : " << curr_ts[idx] << *req;

			last_ts[core] = global_ts;

			//add_to_presence_map(*req);

			//For every instruction added, decrement context_switch_count
			context_switch_count--;
			if(context_switch_count == 0)
			{
				tid_offset = switch_threads();
			}
			return req;
		}

		//Guest induced shootdowns
		if(shootdown_ts <= global_ts && empty_file_shootdown == false)
		{

			Request *req = nullptr;
			int num_tries = 0;

			if (shootdown_va != actual_shootdown_identifier)
				req = new Request(shootdown_va, TRANSLATION_WRITE, shootdown_tid, shootdown_is_large, core);
			else
				req = new Request(shootdown_va, TRANSLATION_WRITE, shootdown_victim_cores, shootdown_is_large, shootdown_ini_core);

			used_up_shootdown = true;
			std::cout << "[SHOOTDOWN_EVENT] Generating guest shootdown" << std::endl;
			std::cout << "Returning guest shootdown : " << *req;
			return req;

		}

		//First check if there are any migration induced shootdowns pending
		if (migration_shootdown_queue.size() > 0)
		{
			std::cout << "[SHOOTDOWN_EVENT] Generating migration shootdown" << std::endl;
			Request * req = migration_shootdown_queue.front();
			std::cout << "Returning migration shootdown : " << *req;

			if (global_ts > (skip_instructions + warmup_period))
				popped_migration_queue++;

			migration_shootdown_queue.pop_front();

			if ((global_ts - previous_shootdown_ts) > ini_penalty || req->m_core_id != previous_core_id)
			{
				previous_shootdown_ts = global_ts;
				previous_core_id = req->m_core_id;
			}
			else
			{
				if (global_ts > (skip_instructions + warmup_period))
					folded_migration_shootdowns++;
				//delete req;
			}
			return req;
		}

		if(curr_ts[idx] > global_ts)
		{
			Request *req = new Request();
			req->m_is_memory_acc = false;
			req->m_core_id = (global_ts) % NUM_CORES;
			global_ts++;

			//For every instruction added, decrement context_switch_count
			context_switch_count--;
			if(context_switch_count == 0)
			{
				tid_offset = switch_threads();
			}

			if(global_ts % 1000000 == 0)
			{
				std::cout << "[NUM_INSTR_PROCESSED] : Count = " << global_ts << "\n";
			}
			return req;
		}
		else
		{
			std::cout << "Why is it going here? curr_ts[idx] " << curr_ts[idx] << " global_ts " << global_ts << std::endl;
			exit(0);
		}

		if(context_switch_count % 100000 == 0)
		{
			std::cout << "Context switch count = " << context_switch_count << "\n";
		}

    }
    std::cout << "[ERROR] Returning nullptr" << std::endl;
    exit(0);
    return nullptr;
}

uint64_t TraceProcessor::switch_threads()
{
    //When context switch count is 0, reinitialize tid offset
    context_switch_count = (5000000000 - 3000000000) * (rand()/(double) RAND_MAX);
    uint64_t tid_offset = (NUM_CORES) * (rand()/(double) RAND_MAX);
    std::cout << "Switching threads\n";

    for(int i = 0; i < NUM_CORES; i++)
    {
        uint64_t tid = (i + tid_offset) % NUM_CORES;
        std::cout << "Core " << i << " now running thread = " << tid << "\n";
    }

    return tid_offset;
}

void TraceProcessor::add_to_migration_shootdown_queue(Request * r)
{
	if (migration_shootdown_queue.size() > 1000)
	{
		std::cout << "[ERROR] migration shootdown file full";
		exit(0);
	}

	migration_shootdown_queue.push_back(r);
}

void TraceProcessor::add_to_presence_map(Request &r)
{
    RequestDesc rdesc(r.m_addr, r.m_tid, r.m_is_large);

    if(rdesc.m_is_large)
    {
        rdesc.m_addr = (rdesc.m_addr) & ~((1 << 21) - 1);
        if(presence_map_large_page.find(rdesc) != presence_map_large_page.end())
        {
            presence_map_large_page[rdesc].insert(r.m_core_id);
        }
        else
        {
            //presence_map_large_page[rdesc] = std::set<uint64_t>();
            presence_map_large_page.insert(std::pair<RequestDesc, std::set<uint64_t>>(rdesc, std::set<uint64_t>()));
            presence_map_large_page[rdesc].insert(r.m_core_id);
        }
    }
    else
    {
        rdesc.m_addr = (rdesc.m_addr) & ~((1 << 12) - 1);
        if(presence_map_small_page.find(rdesc) != presence_map_small_page.end())
        {
            presence_map_small_page[rdesc].insert(r.m_core_id);
        }
        else
        {
            //presence_map_small_page[rdesc] = std::set<uint64_t>();
            presence_map_small_page.insert(std::pair<RequestDesc, std::set<uint64_t>>(rdesc, std::set<uint64_t>()));
            presence_map_small_page[rdesc].insert(r.m_core_id);
        }
    }
}

void TraceProcessor::remove_from_presence_map(uint64_t addr, uint64_t tid, bool is_large, unsigned int core_id)
{
    RequestDesc rdesc(addr, tid, is_large);

    if(is_large)
    {
        //assert(presence_map_large_page.find(rdesc) != presence_map_large_page.end());
        if(presence_map_large_page.find(rdesc) != presence_map_large_page.end())
        {
            auto it = presence_map_large_page[rdesc].find(core_id);
            //assert(it != presence_map_large_page[rdesc].end());
            if(it != presence_map_large_page[rdesc].end())
            {
                presence_map_large_page[rdesc].erase(it);
                if(presence_map_large_page[rdesc].size() == 0)
                {
                    presence_map_large_page.erase(rdesc);
                }
            }
        }
    }
    else
    {
        //assert(presence_map_small_page.find(rdesc) != presence_map_small_page.end());
        if(presence_map_small_page.find(rdesc) != presence_map_small_page.end())
        {
            auto it = presence_map_small_page[rdesc].find(core_id);
            //assert(it != presence_map_small_page[rdesc].end());
            if(it != presence_map_small_page[rdesc].end())
            {
                presence_map_small_page[rdesc].erase(it);
                if(presence_map_small_page[rdesc].size() == 0)
                {
                    presence_map_small_page.erase(rdesc);
                }
            }
        }
    }
}
