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
#include "counter.hpp"
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

void print_results(TraceProcessor &tp, std::string benchmark);

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
uint64_t total_requests_from_tp = 0;
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

	assert(tp.benchname != "" || tp.config != "" || tp.env != "" || tp.skip_instructions != 0);
	assert(tp.dram_size != 0 || tp.nvm_disk_size != 0 || tp.migration_threshold != 0);

	//Set up simulation parameters by Traceprocessor
	num_dram_pages = tp.dram_size;
	num_nvm_disk_pages = tp.nvm_disk_size;
	migration_threshold = tp.migration_threshold;
	page_migration_policy = tp.mig_policy;
	tp.global_ts = tp.skip_instructions;

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

		tp.ini_penalty = initiator_penalty;
	}

	if (num_dram_pages == 0 || num_nvm_disk_pages == 0 || migration_threshold == 0)
	{
		std::cout << "[WARNING] Page Migration model incorrect" <<std::endl ;
	}

	std::cout << "\n\n*******Setup Values*********\n\n"
			<< "Benchname : " << tp.benchname
			<< "\nSimulator Config " << tp.config
			<< "\nInitiator penalty " << initiator_penalty
			<< "\nVictim penalty : " << victim_penalty
			<< "\nSkipped instructions : " << tp.skip_instructions
			<< "\nMax TS to Simulate : " << max_ts_to_simulate
			<< "\nSkip Instructions : " << tp.skip_instructions
			<< "\nNum DRAM pages : " << num_dram_pages
			<< "\nNum NVM/Disk Pages : " << num_nvm_disk_pages
			<< "\nMigration threshold : " << migration_threshold
			<< "\nMigration Policy : " << page_migration_policy
			<< "\n\n****************************\n\n";

	tp.verifyOpenTraceFiles();

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

		std::cout << "get TP PTR " << cores[i]->instructions_retired->get_tp_ptr() << "\n";

		ll_interface_complete = cores[i]->interfaceHier(ll_interface_complete);
	}

	//Add memfile ptr to LLC and POMTLB Caches
//	llc->add_mem_file_ptr(&memFile);
//	l3_tlb_small->add_mem_file_ptr(&memFile);
//	l3_tlb_large->add_mem_file_ptr(&memFile);

	page_migration_model = std::make_shared <migration_model>(num_dram_pages, num_nvm_disk_pages,
			page_migration_policy, migration_threshold);

	page_migration_model->add_traceprocessor(&tp);

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

		if (tp.global_ts > (tp.skip_instructions + tp.warmup_period))
			total_requests_from_tp++;

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

				if (tp.global_ts > (tp.skip_instructions + tp.warmup_period))
					total_requests_from_tp++;

				std::cout << *r;

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

		uint64_t instructions_simulated = 0;

		for(int i = 0; i < NUM_CORES; i++)
		{
			cores[i]->tick(tp.config, initiator_penalty, victim_penalty);

			//done = (done & cores[i]->is_done()) && (cores[i]->traceVec.size() == 0) && (tp.global_ts >= max_ts_to_simulate);

			if((cores[i]->m_clk + cores[i]->num_stall_cycles->get_val()) > max_ts_to_simulate * 10)
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
				print_results(tp, tp.benchname);
			}

			if(cores[i]->m_clk % 1000000 == 0)
			{
				std::cout << "[CYCLES] Core " << i << ": " << cores[i]->m_clk << "\n";
			}

			instructions_simulated += cores[i]->instructions_retired->get_val();
		}

		if (instructions_simulated >= tp.instrument_instructions)
			done = true;

		if(done == true)
		{
			std::cerr << "[INFO]Done is true " << std::endl;
			std::cerr << "[INFO]Migration shootdown queue size at exit is " << tp.migration_shootdown_queue.size() << std::endl;

			for(auto i = tp.migration_shootdown_queue.begin(); i != tp.migration_shootdown_queue.end(); i++)
			{
				delete (*i);
			}

			for(int i = 0; i < NUM_CORES; i++)
			{
				std::cerr << "Core num : " << i << ") cores[i]->is_done() " <<
						cores[i]->is_done() << " cores[i]->traceVec.size() " <<
						cores[i]->traceVec.size() << " tp.global_ts " <<
						tp.global_ts << std::endl;
			}

			print_results(tp, tp.benchname);

		}
	}
}

void print_results(TraceProcessor &tp, std::string benchmark)
{
	std::ofstream outFile;

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
	uint64_t total_page_invalidations = 0; //This excludes the guest/host shootdowns
	uint64_t total_tr_invalidations = 0;
	uint64_t total_false_invalidations = 0;

	outFile << "***************Configurations*******************" << "\n\n";
	outFile << "Benchname : " << tp.benchname;
	outFile << "\nSimulator Config " << tp.config;
	outFile << "\nInitiator penalty " << initiator_penalty;
	outFile << "\nVictim penalty : " << victim_penalty;
	outFile << "\nMax TS to Simulate : " << tp.warmup_period + tp.skip_instructions + tp.instrument_instructions;
	outFile << "\nSkipped instructions : " << tp.skip_instructions;
	outFile << "\nNum DRAM pages : " << num_dram_pages;
	outFile << "\nNum NVM/Disk Pages : " << num_nvm_disk_pages;
	outFile << "\nMigration threshold : " << migration_threshold;
	outFile << "\nMigration Policy : " << page_migration_policy;
	outFile << "\n\n**********************************************\n\n";

	for(int i = 0; i < NUM_CORES;i++)
	{
		//cores[i]->m_rob->printContents();
		//
		outFile << "--------Core " << i << " -------------" << "\n";

		//outFile << "Cycles = " << cores[i]->num_cycles << "\n";
		//outFile << "Instructions = " << cores[i]->instructions_retired << "\n";

		for (auto const& c: cores[i]->module_counters)
		{
			outFile << (*c);
		}

		total_num_cycles += cores[i]->num_cycles->get_val();
		total_stall_cycles += cores[i]->num_stall_cycles->get_val();
		total_shootdowns += cores[i]->num_shootdown->get_val();
		total_instructions += cores[i]->instructions_retired->get_val();
		total_guest_shootdowns+=cores[i]->num_guest_shootdowns->get_val();
		total_host_shootdowns+=cores[i]->num_host_shootdowns->get_val();
		total_page_invalidations+=cores[i]->page_invalidations->get_val();
		total_tr_invalidations+=cores[i]->num_tr_invalidations->get_val();
		total_false_invalidations+=cores[i]->num_false_invalidations->get_val();

		for (auto const& c: l1_data_caches[i]->module_counters)
		{
			outFile << "[L1 D$] " << (*c);
		}

		if(cores[i]->instructions_retired->get_val())
		{
			double l1d_mpki = (double) (l1_data_caches[i]->num_data_misses->get_val() * 1000.0)/(cores[i]->instructions_retired->get_val());
			outFile << "[L1 D$] MPKI = " << l1d_mpki << "\n";
			if(!tp.is_multicore) l1d_agg_mpki += l1d_mpki;
		}

		for (auto const& c: l2_data_caches[i]->module_counters)
		{
			outFile << "[L2 D$] " << (*c);
		}

		if(cores[i]->instructions_retired->get_val())
		{
			double l2d_mpki = (double) ((l2_data_caches[i]->num_data_misses->get_val()  +
					l2_data_caches[i]->num_tr_misses->get_val())* 1000.0)/(cores[i]->instructions_retired->get_val());
			outFile << "[L2 D$] MPKI = " << l2d_mpki << "\n";
			if(!tp.is_multicore) l2d_agg_mpki += l2d_mpki;
		}

		for (auto const& c: l1_tlb[2 * i]->module_counters)
		{
			outFile << "[L1 SMALL TLB] " << (*c);
		}

		if(cores[i]->instructions_retired->get_val())
		{
			double l1ts_mpki = (double) ((l1_tlb[2 * i]->num_data_misses->get_val()  +
					l1_tlb[2 * i]->num_tr_misses->get_val())* 1000.0)/(cores[i]->instructions_retired->get_val());
			outFile << "[L1 SMALL TLB] MPKI = " << l1ts_mpki << "\n";
			if(!tp.is_multicore) l1ts_agg_mpki += l1ts_mpki;
		}

		total_num_data_coh_msgs += l1_tlb[2 * i]->num_data_coh_msgs->get_val();
		total_num_tr_coh_msgs += l1_tlb[2 * i]->num_tr_coh_msgs->get_val();

		for (auto const& c: l1_tlb[2 * i + 1]->module_counters)
		{
			outFile << "[L1 LARGE TLB] " << (*c);
		}

		if(cores[i]->instructions_retired->get_val())
		{
			double l1tl_mpki = (double) ((l1_tlb[2 * i + 1]->num_data_misses->get_val()
					+ l1_tlb[2 * i + 1]->num_tr_misses->get_val())* 1000.0)/(cores[i]->instructions_retired->get_val());
			outFile << "[L1 LARGE TLB] MPKI = " << l1tl_mpki << "\n";
			if(!tp.is_multicore) l1tl_agg_mpki += l1tl_mpki;
		}

		for (auto const& c: l2_tlb[2 * i]->module_counters)
		{
			outFile << "[L2 SMALL TLB] " << (*c);
		}

		l2_tlb_misses+=(l2_tlb[2 * i]->num_tr_misses->get_val() + l2_tlb[2 * i + 1]->num_data_misses->get_val());

		if(cores[i]->instructions_retired->get_val())
		{
			double l2ts_mpki = (double) ((l2_tlb[2 * i]->num_data_misses->get_val()  +
					l2_tlb[2 * i]->num_tr_misses->get_val())* 1000.0)/(cores[i]->instructions_retired->get_val());
			outFile << "[L2 SMALL TLB] MPKI = " << l2ts_mpki << "\n";
			if(!tp.is_multicore) l2ts_agg_mpki += l2ts_mpki;
		}

		for (auto const& c: l2_tlb[2 * i + 1]->module_counters)
		{
			outFile << "[L2 LARGE TLB] " << (*c);
		}

		if(cores[i]->instructions_retired->get_val())
		{
			double l2tl_mpki = (double) ((l2_tlb[2 * i + 1]->num_data_misses->get_val()  +
					l2_tlb[2 * i + 1]->num_tr_misses->get_val())* 1000.0)/(cores[i]->instructions_retired->get_val());
			outFile << "[L2 LARGE TLB] MPKI = " << l2tl_mpki << "\n";
			if(!tp.is_multicore) l2tl_agg_mpki += l2tl_mpki;
		}
	}

	uint64_t migration_guest_shootdown = (total_host_shootdowns/3);
	uint64_t guest_shootdown_program = (total_guest_shootdowns - migration_guest_shootdown);
	uint64_t total_migration_shootdowns = migration_guest_shootdown + total_host_shootdowns;
	assert(guest_shootdown_program >= 0);

	outFile << "----------------------------------------------------------------------\n";

	if(!tp.is_multicore)
	{
		outFile << "Cycles = " << total_num_cycles << "\n";
		outFile << "Instructions retired = " << (total_instructions) << "\n";
		outFile << "Requests returned from TP = " << total_requests_from_tp << "\n";
		if(total_num_cycles > 0)
		{
			outFile << "IPC = " << (double) (total_instructions)/(total_num_cycles + total_stall_cycles) << "\n";
		}
		outFile << "Stall cycles = " << total_stall_cycles << "\n";
		outFile << "Total shootdowns = " << total_shootdowns << "\n";
		outFile << "Total additions to the Migration Request Queue = " << tp.addition_to_migration_queue << "\n";
		outFile << "Total Pops of the Migration Request Queue = " << tp.popped_migration_queue << "\n";
		outFile << "Potential foldable migration requests = " << tp.folded_migration_shootdowns << "\n";

		for (auto const& c: page_migration_model->module_counters)
		{
			outFile << "[MIGRATION MODEL] " << (*c);
		}

		outFile << "Total Num Guest shootdowns = " << total_guest_shootdowns << "\n";
		outFile << "Num Guest shootdowns (program) = " << guest_shootdown_program << "\n";
		outFile << "Num Guest shootdowns (migration) = " << migration_guest_shootdown << "\n";
		outFile << "Num Host shootdowns = " << total_host_shootdowns << "\n";
		outFile << "Total Num of Actual Migration shootdowns = " << total_migration_shootdowns << "\n";
		outFile << "Total page invalidations (Excludes guest/host shootdowns) = " << total_page_invalidations << "\n";
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

	for (auto const& c: llc->module_counters)
	{
		outFile << "[LLC] " << (*c);
	}

	if(total_instructions)
	{
		outFile << "[LLC] MPKI = " << (double) ((llc->num_data_misses->get_val()  +
				llc->num_tr_misses->get_val())* 1000.0)/(total_instructions) << "\n";
	}

	for (auto const& c: l3_tlb_small->module_counters)
	{
		outFile << "[L3 SMALL TLB] " << (*c);
	}

	if(total_instructions)
	{
		outFile << "[L3 SMALL TLB] MPKI = " << (double) (l3_tlb_small->num_tr_misses->get_val() * 1000.0)/(total_instructions) << "\n";
		outFile << "[L3 SMALL TLB] ATLB Hit Rate = " << (double) (1 - (l3_tlb_small->num_tr_misses->get_val()/l2_tlb_misses)) << "\n";
	}

	for (auto const& c: l3_tlb_large->module_counters)
	{
		outFile << "[L3 LARGE TLB] " << (*c);
	}

	if(total_instructions)
	{
		outFile << "[L3 LARGE TLB] MPKI = " << (double) (l2_tlb_misses/l3_tlb_large->num_tr_misses->get_val()) << "\n";
	}

	if(total_num_cycles)
	{
		assert(l2_tlb_misses >= l3_tlb_small->num_tr_misses->get_val());

		outFile << "Native Baseline IPC : "
				<< (double) (total_instructions)/(total_num_cycles + total_stall_cycles +
																	((l2_tlb_misses - l3_tlb_small->num_tr_misses->get_val())*35))
				<< std::endl;

		outFile << "Native POMTLB IPC : "
				<< (double) (total_instructions)/(total_num_cycles + total_stall_cycles) << std::endl;

		outFile << "Native HATRIC IPC : "
				<< (double) (total_instructions)/(total_num_cycles + (total_shootdowns*100))
				<< std::endl;

		outFile << "Native TCAT IPC : "
				<< (double) (total_instructions)/(total_num_cycles +
											((total_page_invalidations + migration_guest_shootdown + total_host_shootdowns)*200))
				<< std::endl;

		outFile << "Native Ideal IPC : "
				<< (double) (total_instructions)/(total_num_cycles)
				<< std::endl;


		//Virtual Stats

		uint32_t virtual_stall_cycles = (uint64_t) (total_stall_cycles*3.2);

		outFile << "Virtual Baseline IPC : "
				<< (double) (total_instructions)/(total_num_cycles + virtual_stall_cycles +
																	((l2_tlb_misses - l3_tlb_small->num_tr_misses->get_val())*75))
				<< std::endl;

		outFile << "Virtual POMTLB IPC : "
				<< (double) (total_instructions)/(total_num_cycles + virtual_stall_cycles) << std::endl;

		outFile << "Virtual HATRIC IPC : "
				<< (double) (total_instructions)/(total_num_cycles + (total_guest_shootdowns*50000) + (total_host_shootdowns*100))
				<< std::endl;

		outFile << "Virtual TCAT IPC : "
				<< (double) (total_instructions)/(total_num_cycles +
							((total_page_invalidations + migration_guest_shootdown)*200) +
							((total_host_shootdowns)*500) +
							((l3_tlb_small->num_tr_misses->get_val() + l3_tlb_large->num_tr_misses->get_val())*200))
				<< std::endl;

		outFile << "Virtual TCAT IPC (With Lock) : "
				<< (double) (total_instructions)/(total_num_cycles +
							((total_page_invalidations + migration_guest_shootdown)*200) +
							((total_host_shootdowns)*700*8) +
							((l3_tlb_small->num_tr_misses->get_val() + l3_tlb_large->num_tr_misses->get_val())*200))
				<< std::endl;

		outFile << "Virtual Ideal IPC : "
				<< (double) (total_instructions)/(total_num_cycles)
				<< std::endl;

		outFile << "Virtual TCAT 1K : "
				<< (double) (total_instructions)/(total_num_cycles +
						((total_page_invalidations + migration_guest_shootdown)*2000) +
						((total_host_shootdowns)*5000) +
						((l3_tlb_small->num_tr_misses->get_val() + l3_tlb_large->num_tr_misses->get_val())*2000))
				<< std::endl;

		outFile << "Virtual TCAT 2K : "
				<< (double) (total_instructions)/(total_num_cycles +
						((total_page_invalidations + migration_guest_shootdown)*4000) +
						((total_host_shootdowns)*10000) +
						((l3_tlb_small->num_tr_misses->get_val() + l3_tlb_large->num_tr_misses->get_val())*4000))
				<< std::endl;

		outFile << "Virtual TCAT 5K : "
				<< (double) (total_instructions)/(total_num_cycles +
						((total_page_invalidations + migration_guest_shootdown)*10000) +
						((total_host_shootdowns)*25000) +
						((l3_tlb_small->num_tr_misses->get_val() + l3_tlb_large->num_tr_misses->get_val())*20000))
				<< std::endl;

		outFile << "Virtual Baseline DRAM Only IPC : "
				<< (double) (total_instructions)/(total_num_cycles + (guest_shootdown_program*50000) +
																	((l2_tlb_misses - l3_tlb_small->num_tr_misses->get_val())*75))
				<< std::endl;

		outFile << "Virtual TCAT DRAM Only IPC : "
				<< (double) (total_instructions)/(total_num_cycles +
							((total_page_invalidations)*200) +
							((l3_tlb_small->num_tr_misses->get_val() + l3_tlb_large->num_tr_misses->get_val())*200))
				<< std::endl;

		outFile << "Virtual HATRIC (50 - 50) IPC : "
				<< (double) (total_instructions)/(total_num_cycles + (total_migration_shootdowns*25000) + (total_migration_shootdowns*50))
				<< std::endl;

		outFile << "Virtual TCAT (50 - 50) IPC : "
				<< (double) (total_instructions)/(total_num_cycles +
							((total_page_invalidations + (total_migration_shootdowns/2))*200) +
							((total_migration_shootdowns)*250) +
							((l3_tlb_small->num_tr_misses->get_val() + l3_tlb_large->num_tr_misses->get_val())*200))
				<< std::endl;

		outFile << "Virtual HATRIC (100 - 0) IPC : "
				<< (double) (total_instructions)/(total_num_cycles + (total_migration_shootdowns*100))
				<< std::endl;

		outFile << "Virtual TCAT (100 - 50) IPC : "
				<< (double) (total_instructions)/(total_num_cycles +
							((total_page_invalidations)*200) +
							((total_migration_shootdowns)*500) +
							((l3_tlb_small->num_tr_misses->get_val() + l3_tlb_large->num_tr_misses->get_val())*200))
				<< std::endl;
	}

	outFile << "----------------------------------------------------------------------\n";
	outFile.close();
}
