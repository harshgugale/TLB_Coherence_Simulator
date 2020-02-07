//
//  main.cpp
//  TLB-Coherence-Simulator
//
//  Created by Yashwant Marathe on 12/6/17.
//  Copyright © 2017 Yashwant Marathe. All rights reserved.
//

#include <iostream>
#include <fstream>
#include "Cache.hpp"
#include "CacheSys.hpp"
#include "ROB.hpp"
#include "Core.hpp"
#include "TraceProcessor.hpp"
#include <memory>
#include "utils.hpp"
#include "migration_model.hpp"

//#ifndef WARMUP_PERIOD
//	#define WARMUP_PERIOD 1000000000L
//#endif
//
//#ifndef NUM_INSTR_TO_SIMULATE
//	#define NUM_INSTR_TO_SIMULATE 20000000000L
//#endif

#define NUM_INITIAL_FILL 1000
#define STRING(s) #s

void print_results(std::ofstream &outFile, TraceProcessor &tp, std::string benchmark);

std::vector<std::shared_ptr<CacheSys>> data_hier;
std::vector<std::shared_ptr<CacheSys>> tlb_hier;
std::vector<std::shared_ptr<Cache>> l1_data_caches;
std::vector<std::shared_ptr<Cache>> l2_data_caches;
std::vector<std::shared_ptr<Cache>> l1_tlb;
std::vector<std::shared_ptr<Cache>> l2_tlb;
std::vector<std::shared_ptr<ROB>> rob_arr;
std::vector<std::shared_ptr<Core>> cores;
std::shared_ptr<Cache> llc, l3_tlb_small, l3_tlb_large;
std::shared_ptr<migration_model> page_migration_model;

uint64_t initiator_penalty = 0, victim_penalty = 0;

uint64_t num_dram_pages = 0, num_nvm_disk_pages = 0, migration_threshold = 0;
std::string page_migration_policy = "none";

std::unordered_map <Request *, int> request_queue;

int main(int argc, char * argv[])
{
	int num_args = 4;
	int num_real_args = num_args + 1;
	uint64_t num_total_traces;

	TraceProcessor tp(8);
	if (argc < num_real_args)
	{
		std::cout << "Program takes " << num_args << " arguments" << std::endl;
		std::cout << "Path name of input config file" << std::endl;
		exit(0);
	}

	char* input_cfg = argv[4];

	tp.parseAndSetupInputs(input_cfg);

	//Taking simulator configs from command line
	tp.benchname = argv[1];
	tp.env = argv[2];
	tp.config = argv[3];

	assert(tp.benchname != "" || tp.config != "" || tp.env != "");

	//Set up simulation parameters by Traceprocessor
	num_dram_pages = tp.dram_size;
	num_nvm_disk_pages = tp.nvm_disk_size;
	migration_threshold = tp.migration_threshold;
	page_migration_policy = tp.mig_policy;

	uint64_t max_ts_to_simulate = tp.warmup_period + tp.skip_instructions + tp.instrument_instructions;

	if (tp.ini_penalty != 0)
	{
		initiator_penalty = tp.ini_penalty;
	}
	else
	{
		if (tp.env == "VIRTUAL")
		{
			initiator_penalty = 50000;
			victim_penalty = 10500;
		}
		else
		{
			initiator_penalty = 16000;
			victim_penalty = 3500;
		}
	}

	if (num_dram_pages == 0 || num_nvm_disk_pages == 0 || migration_threshold == 0)
	{
		std::cout << "[WARNING] Page Migration model incorrect" <<std::endl ;
	}

	std::cout << "\n\n*******Setup Values*********\n\n" << "Benchname : " << tp.benchname
			<< "\nSimulator Config " << tp.config
			<< "\nInitiator penalty " << initiator_penalty
			<< "\nVictim penalty : " << victim_penalty
			<< "\nMax TS to Simulate : " << max_ts_to_simulate
			<< "\nNum DRAM pages : " << num_dram_pages
			<< "\nNum NVM/Disk Pages : " << num_nvm_disk_pages
			<< "\nMigration threshold : " << migration_threshold
			<< "\nMigration Policy : " << page_migration_policy
			<< "\n\n****************************\n\n";

	tp.verifyOpenTraceFiles();

	std::ofstream outFile;
	//std::ofstream memFile;

	//memFile.open(tp.benchname+"_evicts.out");

	//std::shared_ptr<Cache> llc = std::make_shared<Cache>(Cache(8192, 16, 64, 38,  DATA_AND_TRANSLATION));
	llc = std::make_shared<Cache>(Cache(8192, 16, 64, 38,  DATA_AND_TRANSLATION));

	bool ll_interface_complete = false;

	//std::shared_ptr<Cache> l3_tlb_small = std::make_shared<Cache>(Cache(16384, 4, 4096, 75, TRANSLATION_ONLY));
	//std::shared_ptr<Cache> l3_tlb_large = std::make_shared<Cache>(Cache(4096, 4, 2 * 1024 * 1024, 75, TRANSLATION_ONLY, true));

	l3_tlb_small = std::make_shared<Cache>(Cache(16384, 4, 4096, 75, TRANSLATION_ONLY));
	l3_tlb_large = std::make_shared<Cache>(Cache(4096, 4, 2 * 1024 * 1024, 75, TRANSLATION_ONLY, true));

	/*
    std::shared_ptr<Cache> l3_tlb_small = std::make_shared<Cache>(Cache(8, 8, 4096, 1, TRANSLATION_ONLY));
    std::shared_ptr<Cache> l3_tlb_large = std::make_shared<Cache>(Cache(8, 8, 2 * 1024 * 1024, 1, TRANSLATION_ONLY, true));
	 */

	for(int i = 0; i < NUM_CORES; i++)
	{
		// Set Cache Sys translation variable to false for data hierarchy
		data_hier.emplace_back(std::make_shared<CacheSys>(CacheSys(false)));
		l1_data_caches.emplace_back(std::make_shared<Cache>(Cache(64, 8, 64, 4, DATA_ONLY)));

		//To provide visibility for Global TS
		l1_data_caches[i]->add_traceprocessor(&tp);

		//Since POMTLB entries only fill upto L2 cache
		l2_data_caches.emplace_back(std::make_shared<Cache>(Cache(1024, 4, 64, 12, DATA_AND_TRANSLATION)));

		//To provide visibility for Global TS
		l2_data_caches[i]->add_traceprocessor(&tp);

		data_hier[i]->add_cache_to_hier(l1_data_caches[i]);
		data_hier[i]->add_cache_to_hier(l2_data_caches[i]);
		data_hier[i]->add_cache_to_hier(llc);

		//Set Cache Sys translation variable to true for TLB hierarchy
		tlb_hier.emplace_back(std::make_shared<CacheSys>(CacheSys(true)));

		/*l1_tlb.emplace_back(std::make_shared<Cache>(Cache(2, 2, 4096, 1, TRANSLATION_ONLY)));
        l1_tlb.emplace_back(std::make_shared<Cache>(Cache(1, 2, 2 * 1024 * 1024, 1, TRANSLATION_ONLY, true)));
        l2_tlb.emplace_back(std::make_shared<Cache>(Cache(4, 4, 4096, 1, TRANSLATION_ONLY)));
        l2_tlb.emplace_back(std::make_shared<Cache>(Cache(4, 4, 2 * 1024 * 1024, 1, TRANSLATION_ONLY, true)));*/

		l1_tlb.emplace_back(std::make_shared<Cache>(Cache(16, 4, 4096, 1, TRANSLATION_ONLY)));
		l1_tlb.emplace_back(std::make_shared<Cache>(Cache(8, 4, 2 * 1024 * 1024, 1, TRANSLATION_ONLY, true)));
		l2_tlb.emplace_back(std::make_shared<Cache>(Cache(64, 16, 4096, 14, TRANSLATION_ONLY)));
		l2_tlb.emplace_back(std::make_shared<Cache>(Cache(32, 16, 2 * 1024 * 1024, 14, TRANSLATION_ONLY, true)));

		l1_tlb[2 * i]->add_traceprocessor(&tp);
		l1_tlb[2 * i + 1]->add_traceprocessor(&tp);
		l2_tlb[2 * i]->add_traceprocessor(&tp);
		l2_tlb[2 * i + 1]->add_traceprocessor(&tp);

		tlb_hier[i]->add_cache_to_hier(l1_tlb[2 * i]);
		tlb_hier[i]->add_cache_to_hier(l1_tlb[2 * i + 1]);
		tlb_hier[i]->add_cache_to_hier(l2_tlb[2 * i]);
		tlb_hier[i]->add_cache_to_hier(l2_tlb[2 * i + 1]);
		tlb_hier[i]->add_cache_to_hier(l3_tlb_small);
		tlb_hier[i]->add_cache_to_hier(l3_tlb_large);

		rob_arr.emplace_back(std::make_shared<ROB>(ROB()));

		rob_arr[i]->add_request_queue(&request_queue);

		cores.emplace_back(std::make_shared<Core>(Core(data_hier[i], tlb_hier[i], rob_arr[i])));

		data_hier[i]->set_core(cores[i]);
		tlb_hier[i]->set_core(cores[i]);

		cores[i]->set_core_id(i);

		cores[i]->add_traceprocessor(&tp);

		ll_interface_complete = cores[i]->interfaceHier(ll_interface_complete);
	}

	//Add memfile ptr to LLC and POMTLB Caches
//	llc->add_mem_file_ptr(&memFile);
//	l3_tlb_small->add_mem_file_ptr(&memFile);
//	l3_tlb_large->add_mem_file_ptr(&memFile);

	page_migration_model = std::make_shared <migration_model>(num_dram_pages, num_nvm_disk_pages,
			page_migration_policy, migration_threshold);

	llc->add_migration_model(page_migration_model);
	l3_tlb_small->add_migration_model(page_migration_model);
	l3_tlb_large->add_migration_model(page_migration_model);

	llc->add_traceprocessor(&tp);
	l3_tlb_small->add_traceprocessor(&tp);
	l3_tlb_large->add_traceprocessor(&tp);

	//Make cores aware of each other
	for(int i = 0; i < NUM_CORES; i++)
	{
		for(int j = 0; j < NUM_CORES; j++)
		{
			if(i != j)
			{
				cores[i]->add_core(cores[j]);
			}
		}
	}

	//Make cache hierarchies aware of each other
	for(int i = 0; i < NUM_CORES; i++)
	{
		for(int j = 0; j < NUM_CORES; j++)
		{
			if(i != j)
			{
				//Data caches see other data caches
				data_hier[i]->add_cachesys(data_hier[j]);

				//TLB see other data caches
				tlb_hier[i]->add_cachesys(data_hier[j]);
			}
		}
	}

	for(int i = 0; i < NUM_CORES; i++)
	{
		for(int j = 0; j < NUM_CORES; j++)
		{
			if(i != j)
			{
				//Data caches see other TLBs
				//Done this way because indexing is dependent on having all data caches first, and then all the TLBs
				data_hier[i]->add_cachesys(tlb_hier[j]);
			}
		}
	}

	uint64_t num_traces_added = 0;

	std::cout << "Initial fill\n";
	for(int i = 0; i < NUM_INITIAL_FILL; i++)
	{
		Request *r = tp.generateRequest();

		request_queue.insert(std::make_pair(r,1));

		if((r != nullptr) && r->m_core_id >=0 && r->m_core_id < NUM_CORES)
		{
			cores[r->m_core_id]->add_trace(r);
			num_traces_added += int(r->m_is_memory_acc);
		}
	}
	std::cout << "Initial fill done\n";

	bool done = false;
	bool timeout = false;
	bool used_up_req = true;
	Request *r = nullptr;
	while(!done && !timeout)
	{

		for (int i = 0; (i < NUM_CORES) && (tp.global_ts < max_ts_to_simulate); i++)
		{
			if (used_up_req == true)
			{
				r = tp.generateRequest();

				assert((r != nullptr) && r->m_core_id >= 0 && r->m_core_id < NUM_CORES);

				used_up_req = false;
			}

			if((r != nullptr) && r->m_core_id >= 0 && r->m_core_id < NUM_CORES
					&& cores[r->m_core_id]->must_add_trace())
			{
				cores[r->m_core_id]->add_trace(r);

				if(num_traces_added % 1000000 == 0)
				{
					std::cout << "[NUM_TRACES_ADDED] Count = " << num_traces_added << "\n";
				}

				num_traces_added +=  int(r->m_is_memory_acc);

				r = nullptr;
				used_up_req = true;
			}
		}

		done = true;

		for(int i = 0; i < NUM_CORES; i++)
		{
			cores[i]->tick(tp.config, initiator_penalty, victim_penalty);

			done = (done & cores[i]->is_done()) && (cores[i]->traceVec.size() == 0)
					&& (tp.global_ts >= max_ts_to_simulate);

			if((cores[i]->m_clk + cores[i]->num_stall_cycles) > max_ts_to_simulate * 10)
			{

				for(int j = 0; j < NUM_CORES; j++)
				{
					if(cores[j]->traceVec.size())
					{
						std::cout << "Core " << j << " has unserviced requests = " << cores[j]->traceVec.size() << "\n";
					}

					if(!cores[j]->is_done())
					{
						std::cout << "Core " << j << " NOT done\n";
						std::cout << "Blocking request = " ; cores[j]->m_rob->peek_commit_ptr();
						std::cout << "Core clk = " << cores[j]->m_clk << "\n";
					}

					if(cores[j]->stall)
					{
						std::cout << "Core " << j << " STALLED\n";
					}

					if(!cores[j]->m_rob->can_issue())
					{
						std::cout << "Core " << j << " can't issue\n";
					}
				}
				std::cout << "[ERROR] Setting timeout to be true" << std::endl;
				timeout = true;
			}

			if(cores[i]->m_num_retired % 1000000 == 0)
			{
				std::cout << "[NUM_INSTR_RETIRED] Core " << i << ": " << cores[i]->m_num_retired << "\n";
				print_results(outFile, tp, tp.benchname);
			}

			if(cores[i]->m_clk % 1000000 == 0)
			{
				std::cout << "[CYCLES] Core " << i << ": " << cores[i]->m_clk << "\n";
			}
		}

		if(done == true)
		{
			std::cout << "[INFO]Done is true " << std::endl;

			for(int i = 0; i < NUM_CORES; i++)
			{
				std::cout << "Core num : " << i << ") cores[i]->is_done() " <<
						cores[i]->is_done() << " cores[i]->traceVec.size() " <<
						cores[i]->traceVec.size() << " tp.global_ts " <<
						tp.global_ts << std::endl;
			}

		}
	}
}

void print_results(std::ofstream &outFile, TraceProcessor &tp, std::string benchmark)
{
	if (tp.config == "IDEAL")
	{
		std::cout << ("Opening " + benchmark + "_baseline_ideal.out") << std::endl;
		outFile.open(benchmark + "_baseline_ideal.out");
	}
	else if (tp.config == "BASELINE" && tp.env == "NATIVE")
	{
		std::cout << ("Opening " + benchmark + "_baseline_native.out") << std::endl;
		outFile.open(benchmark + "_baseline_native.out");
	}
	else if (tp.config == "BASELINE" && tp.env == "VIRTUAL")
	{
		std::cout << ("Opening " + benchmark + "_baseline_virtual.out") << std::endl;
		outFile.open(benchmark + "_baseline_virtual.out");
	}
	else if (tp.config == "TCAT")
	{
		std::cout << ("Opening " + benchmark + "_tcat.out") << std::endl;
		outFile.open(benchmark + "_tcat.out");
	}
	else if (tp.config == "HATRIC")
	{
		std::cout << ("Opening " + benchmark + "_hatric.out") << std::endl;
		outFile.open(benchmark + "_hatric.out");
	} else
	{
		std::cout << "Error in config";
		exit(0);
	}

	uint64_t total_num_cycles = 0;
	uint64_t total_stall_cycles = 0;
	uint64_t total_shootdowns = 0;
	uint64_t total_instructions = 0;
	uint64_t total_num_data_coh_msgs = 0;
	uint64_t total_num_tr_coh_msgs = 0;
	double l1d_agg_mpki;
	double l2d_agg_mpki;
	double l1ts_agg_mpki;
	double l1tl_agg_mpki;
	double l2ts_agg_mpki;
	double l2tl_agg_mpki;
	double l2_tlb_misses = 0;
	uint64_t total_guest_shootdowns = 0;
	uint64_t total_host_shootdowns = 0;
	uint64_t total_tr_invalidations = 0;
	uint64_t total_false_invalidations = 0;

	for(int i = 0; i < NUM_CORES;i++)
	{
		//cores[i]->m_rob->printContents();
		//
		outFile << "--------Core " << i << " -------------" << "\n";

		outFile << "Cycles = " << cores[i]->num_cycles << "\n";
		outFile << "Instructions = " << (cores[i]->instructions_retired) << "\n";
		outFile << "Stall cycles = " << cores[i]->num_stall_cycles << "\n";
		outFile << "Num shootdowns = " << cores[i]->num_shootdown << "\n";
		total_num_cycles += cores[i]->num_cycles;
		total_stall_cycles += cores[i]->num_stall_cycles;
		total_shootdowns += cores[i]->num_shootdown;
		total_instructions += cores[i]->instructions_retired;
		total_guest_shootdowns+=cores[i]->num_guest_shootdowns;
		total_host_shootdowns+=cores[i]->num_host_shootdowns;

		outFile << "[L1 D$] data hits = " << l1_data_caches[i]->num_data_hits << "\n";
		outFile << "[L1 D$] translation hits = " << l1_data_caches[i]->num_tr_hits << "\n";
		outFile << "[L1 D$] data misses = " << l1_data_caches[i]->num_data_misses << "\n";
		outFile << "[L1 D$] translation misses = " << l1_data_caches[i]->num_tr_misses << "\n";
		outFile << "[L1 D$] MSHR data hits = " << l1_data_caches[i]->num_mshr_data_hits << "\n";
		outFile << "[L1 D$] MSHR translation hits = " << l1_data_caches[i]->num_mshr_tr_hits << "\n";
		outFile << "[L1 D$] data accesses = " << l1_data_caches[i]->num_data_accesses << "\n";
		outFile << "[L1 D$] translation accesses = " << l1_data_caches[i]->num_tr_accesses << "\n";
		if(cores[i]->m_num_retired)
		{
			double l1d_mpki = (double) (l1_data_caches[i]->num_data_misses * 1000.0)/(cores[i]->m_num_retired);
			outFile << "[L1 D$] MPKI = " << l1d_mpki << "\n";
			if(!tp.is_multicore) l1d_agg_mpki += l1d_mpki;
		}

		outFile << "[L2 D$] data hits = " << l2_data_caches[i]->num_data_hits << "\n";
		outFile << "[L2 D$] translation hits = " << l2_data_caches[i]->num_tr_hits << "\n";
		outFile << "[L2 D$] data misses = " << l2_data_caches[i]->num_data_misses << "\n";
		outFile << "[L2 D$] translation misses = " << l2_data_caches[i]->num_tr_misses << "\n";
		outFile << "[L2 D$] MSHR data hits = " << l2_data_caches[i]->num_mshr_data_hits << "\n";
		outFile << "[L2 D$] MSHR translation hits = " << l2_data_caches[i]->num_mshr_tr_hits << "\n";
		outFile << "[L2 D$] data accesses = " << l2_data_caches[i]->num_data_accesses << "\n";
		outFile << "[L2 D$] translation accesses = " << l2_data_caches[i]->num_tr_accesses << "\n";
		if(cores[i]->m_num_retired)
		{
			double l2d_mpki = (double) ((l2_data_caches[i]->num_data_misses  + l2_data_caches[i]->num_tr_misses)* 1000.0)/(cores[i]->m_num_retired);
			outFile << "[L2 D$] MPKI = " << l2d_mpki << "\n";
			if(!tp.is_multicore) l2d_agg_mpki += l2d_mpki;
		}

		outFile << "[L1 SMALL TLB] data hits = " << l1_tlb[2 * i]->num_data_hits << "\n";
		outFile << "[L1 SMALL TLB] translation hits = " << l1_tlb[2 * i]->num_tr_hits << "\n";
		outFile << "[L1 SMALL TLB] data misses = " << l1_tlb[2 * i]->num_data_misses << "\n";
		outFile << "[L1 SMALL TLB] translation misses = " << l1_tlb[2 * i]->num_tr_misses << "\n";
		outFile << "[L1 SMALL TLB] MSHR data hits = " << l1_tlb[2 * i]->num_mshr_data_hits << "\n";
		outFile << "[L1 SMALL TLB] MSHR translation hits = " << l1_tlb[2 * i]->num_mshr_tr_hits << "\n";
		outFile << "[L1 SMALL TLB] data accesses = " << l1_tlb[2 * i]->num_data_accesses << "\n";
		outFile << "[L1 SMALL TLB] translation accesses = " << l1_tlb[2 * i]->num_tr_accesses << "\n";
		outFile << "[L1 SMALL TLB] Translation invalidations = " << l1_tlb[2 * i]->num_tr_invalidations << "\n";
		outFile << "[L1 SMALL TLB] False translation invalidations = " << l1_tlb[2 * i]->num_false_invalidations << "\n";
		total_tr_invalidations+=l1_tlb[2 * i]->num_tr_invalidations;
		total_false_invalidations+=l1_tlb[2 * i]->num_false_invalidations;

		if(cores[i]->m_num_retired)
		{
			double l1ts_mpki = (double) ((l1_tlb[2 * i]->num_data_misses  + l1_tlb[2 * i]->num_tr_misses)* 1000.0)/(cores[i]->m_num_retired);
			outFile << "[L1 SMALL TLB] MPKI = " << l1ts_mpki << "\n";
			if(!tp.is_multicore) l1ts_agg_mpki += l1ts_mpki;
		}

		total_num_data_coh_msgs += l1_tlb[2 * i]->num_data_coh_msgs;
		total_num_tr_coh_msgs += l1_tlb[2 * i]->num_tr_coh_msgs;

		outFile << "[L1 LARGE TLB] data hits = " << l1_tlb[2 * i + 1]->num_data_hits << "\n";
		outFile << "[L1 LARGE TLB] translation hits = " << l1_tlb[2 * i + 1]->num_tr_hits << "\n";
		outFile << "[L1 LARGE TLB] data misses = " << l1_tlb[2 * i + 1]->num_data_misses << "\n";
		outFile << "[L1 LARGE TLB] translation misses = " << l1_tlb[2 * i + 1]->num_tr_misses << "\n";
		outFile << "[L1 LARGE TLB] MSHR data hits = " << l1_tlb[2 * i + 1]->num_mshr_data_hits << "\n";
		outFile << "[L1 LARGE TLB] MSHR translation hits = " << l1_tlb[2 * i + 1]->num_mshr_tr_hits << "\n";
		outFile << "[L1 LARGE TLB] data accesses = " << l1_tlb[2 * i + 1]->num_data_accesses << "\n";
		outFile << "[L1 LARGE TLB] translation accesses = " << l1_tlb[2 * i + 1]->num_tr_accesses << "\n";
		if(cores[i]->m_num_retired)
		{
			double l1tl_mpki = (double) ((l1_tlb[2 * i + 1]->num_data_misses  + l1_tlb[2 * i + 1]->num_tr_misses)* 1000.0)/(cores[i]->m_num_retired);
			outFile << "[L1 LARGE TLB] MPKI = " << l1tl_mpki << "\n";
			if(!tp.is_multicore) l1tl_agg_mpki += l1tl_mpki;
		}

		outFile << "[L2 SMALL TLB] data hits = " << l2_tlb[2 * i]->num_data_hits << "\n";
		outFile << "[L2 SMALL TLB] translation hits = " << l2_tlb[2 * i]->num_tr_hits << "\n";
		outFile << "[L2 SMALL TLB] data misses = " << l2_tlb[2 * i]->num_data_misses << "\n";
		outFile << "[L2 SMALL TLB] translation misses = " << l2_tlb[2 * i]->num_tr_misses << "\n";
		outFile << "[L2 SMALL TLB] MSHR data hits = " << l2_tlb[2 * i]->num_mshr_data_hits << "\n";
		outFile << "[L2 SMALL TLB] MSHR translation hits = " << l2_tlb[2 * i]->num_mshr_tr_hits << "\n";
		outFile << "[L2 SMALL TLB] data accesses = " << l2_tlb[2 * i]->num_data_accesses << "\n";
		outFile << "[L2 SMALL TLB] translation accesses = " << l2_tlb[2 * i]->num_tr_accesses << "\n";

		l2_tlb_misses+=(l2_tlb[2 * i]->num_tr_misses + l1_tlb[2 * i + 1]->num_data_misses);

		if(cores[i]->m_num_retired)
		{
			double l2ts_mpki = (double) ((l2_tlb[2 * i]->num_data_misses  + l2_tlb[2 * i]->num_tr_misses)* 1000.0)/(cores[i]->m_num_retired);
			outFile << "[L2 SMALL TLB] MPKI = " << l2ts_mpki << "\n";
			if(!tp.is_multicore) l2ts_agg_mpki += l2ts_mpki;
		}

		outFile << "[L2 LARGE TLB] data hits = " << l2_tlb[2 * i + 1]->num_data_hits << "\n";
		outFile << "[L2 LARGE TLB] translation hits = " << l2_tlb[2 * i + 1]->num_tr_hits << "\n";
		outFile << "[L2 LARGE TLB] data misses = " << l2_tlb[2 * i + 1]->num_data_misses << "\n";
		outFile << "[L2 LARGE TLB] translation misses = " << l2_tlb[2 * i + 1]->num_tr_misses << "\n";
		outFile << "[L2 LARGE TLB] MSHR data hits = " << l2_tlb[2 * i + 1]->num_mshr_data_hits << "\n";
		outFile << "[L2 LARGE TLB] MSHR translation hits = " << l2_tlb[2 * i + 1]->num_mshr_tr_hits << "\n";
		outFile << "[L2 LARGE TLB] data accesses = " << l2_tlb[2 * i + 1]->num_data_accesses << "\n";
		outFile << "[L2 LARGE TLB] translation accesses = " << l2_tlb[2 * i + 1]->num_tr_accesses << "\n";
		if(cores[i]->m_num_retired)
		{
			double l2tl_mpki = (double) ((l2_tlb[2 * i + 1]->num_data_misses  + l2_tlb[2 * i + 1]->num_tr_misses)* 1000.0)/(cores[i]->m_num_retired);
			outFile << "[L2 LARGE TLB] MPKI = " << l2tl_mpki << "\n";
			if(!tp.is_multicore) l2tl_agg_mpki += l2tl_mpki;
		}
	}

	outFile << "----------------------------------------------------------------------\n";

	if(!tp.is_multicore)
	{
		outFile << "Cycles = " << total_num_cycles << "\n";
		outFile << "Instructions = " << (total_instructions) << "\n";
		if(total_num_cycles > 0)
		{
			outFile << "IPC = " << (double) (total_instructions)/(total_num_cycles + total_stall_cycles) << "\n";
		}
		outFile << "Stall cycles = " << total_stall_cycles << "\n";
		outFile << "Total shootdowns = " << total_shootdowns << "\n";
		outFile << "Num migration shootdowns = " << page_migration_model->num_migrations << "\n";
		outFile << "Num Guest shootdowns = " << total_guest_shootdowns << "\n";
		outFile << "Num Host shootdowns = " << total_host_shootdowns << "\n";
		outFile << "Total translation invalidations = " << total_tr_invalidations << "\n";
		outFile << "Total False invalidations = " << total_false_invalidations << "\n";
		outFile << "[L1 D$] Aggregate MPKI = " << (l1d_agg_mpki/(NUM_CORES * 1000)) << "\n";
		outFile << "[L2 D$] Aggregate MPKI = " << (l2d_agg_mpki/ (NUM_CORES * 1000)) << "\n";
		outFile << "[L1 SMALL TLB] Aggregate MPKI = " << l1ts_agg_mpki << "\n";
		outFile << "[L1 LARGE TLB] Aggregate MPKI = " << l1tl_agg_mpki << "\n";
		outFile << "[L2 SMALL TLB] Aggregate MPKI = " << l2ts_agg_mpki << "\n";
		outFile << "[L2 LARGE TLB] Aggregate MPKI = " << l2tl_agg_mpki << "\n";
		outFile << "[L2 TLB] Total misses = " << l2_tlb_misses << "\n";
	}

	outFile << "----------------------------------------------------------------------\n";
	outFile << "[AGGREGATE] Number of data coherence messages = " << total_num_data_coh_msgs << "\n";
	outFile << "[AGGREGATE] Number of translation coherence messages = " << total_num_tr_coh_msgs << "\n";
	outFile << "[L3] data hits = " << llc->num_data_hits << "\n";
	outFile << "[L3] translation hits = " << llc->num_tr_hits << "\n";
	outFile << "[L3] data misses = " << llc->num_data_misses << "\n";
	outFile << "[L3] translation misses = " << llc->num_tr_misses << "\n";
	outFile << "[L3] MSHR data hits = " << llc->num_mshr_data_hits << "\n";
	outFile << "[L3] MSHR translation hits = " << llc->num_mshr_tr_hits << "\n";
	outFile << "[L3] data accesses = " << llc->num_data_accesses << "\n";
	outFile << "[L3] translation accesses = " << llc->num_tr_accesses << "\n";
	outFile << "[L3] Memory Accesses = " << llc->mem_accesses << "\n";
	if(total_instructions)
	{
		outFile << "[L3] MPKI = " << (double) ((llc->num_data_misses  + llc->num_tr_misses)* 1000.0)/(total_instructions) << "\n";
	}

	outFile << "[L3 SMALL TLB] data hits = " << l3_tlb_small->num_data_hits << "\n";
	outFile << "[L3 SMALL TLB] translation hits = " << l3_tlb_small->num_tr_hits << "\n";
	outFile << "[L3 SMALL TLB] data misses = " << l3_tlb_small->num_data_misses << "\n";
	outFile << "[L3 SMALL TLB] translation misses = " << l3_tlb_small->num_tr_misses << "\n";
	outFile << "[L3 SMALL TLB] MSHR data hits = " << l3_tlb_small->num_mshr_data_hits << "\n";
	outFile << "[L3 SMALL TLB] MSHR translation hits = " << l3_tlb_small->num_mshr_tr_hits << "\n";
	outFile << "[L3 SMALL TLB] data accesses = " << l3_tlb_small->num_data_accesses << "\n";
	outFile << "[L3 SMALL TLB] translation accesses = " << l3_tlb_small->num_tr_accesses << "\n";
	outFile << "[L3 SMALL TLB] Memory Accesses = " << l3_tlb_small->mem_accesses << "\n";

	if(total_instructions)
	{
		outFile << "[L3 SMALL TLB] MPKI = " << (double) (l3_tlb_small->num_tr_misses * 1000.0)/(total_instructions) << "\n";
	}

	outFile << "[L3 LARGE TLB] data hits = " << l3_tlb_large->num_data_hits << "\n";
	outFile << "[L3 LARGE TLB] translation hits = " << l3_tlb_large->num_tr_hits << "\n";
	outFile << "[L3 LARGE TLB] data misses = " << l3_tlb_large->num_data_misses << "\n";
	outFile << "[L3 LARGE TLB] translation misses = " << l3_tlb_large->num_tr_misses << "\n";
	outFile << "[L3 LARGE TLB] MSHR data hits = " << l3_tlb_large->num_mshr_data_hits << "\n";
	outFile << "[L3 LARGE TLB] MSHR translation hits = " << l3_tlb_large->num_mshr_tr_hits << "\n";
	outFile << "[L3 LARGE TLB] data accesses = " << l3_tlb_large->num_data_accesses << "\n";
	outFile << "[L3 LARGE TLB] translation accesses = " << l3_tlb_large->num_tr_accesses << "\n";
	outFile << "[L3 LARGE TLB] Memory Accesses = " << l3_tlb_large->mem_accesses << "\n";

	if(total_instructions)
	{
		outFile << "[L3 LARGE TLB] MPKI = " << (double) (l3_tlb_large->num_tr_misses * 1000.0)/(total_instructions) << "\n";
	}

	outFile << "----------------------------------------------------------------------\n";
	outFile.close();
}
