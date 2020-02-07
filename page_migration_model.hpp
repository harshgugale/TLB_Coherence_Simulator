/*
 * page_migration_model.hpp
 *
 *  Created on: Jul 20, 2019
 *      Author: root
 */

#ifndef TLB_COHERENCE_SIMULATOR_PAGE_MIGRATION_MODEL_HPP_
#define TLB_COHERENCE_SIMULATOR_PAGE_MIGRATION_MODEL_HPP_


//typedef unsigned long long uint64_t;
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.hpp"

class migration_model
{
	migration_model(uint64_t dram_sz, uint64_t)

public:

};

#define SZ (16*1024*1024)
#define DSZ 10
int64_t * page_counts = 0;
int64_t * migrated_pages = 0;
int64_t * dram_page_location = 0;
bool * dram_page_references;


#define MIGRATE_THRESH 1000
int64_t num_migrations = 0;
int64_t num_dram_pages = DSZ;
int64_t num_nvm_pages = SZ;
int64_t next_free_dram_page = 0;
int64_t num_entries_in_trace = 0;

enum policies{
    lru,
    fifo,
    clock_repl,
} replacement_policy;

int num_cores = 8;
int64_t ref_ptr = 0;
int64_t last_migrated_page = 0;

void processPage(uint64_t ts, int core, int page_num, policies replacement_policy)
{
    page_counts[page_num]+=1;
    num_entries_in_trace++;
    int use_core = core % num_cores;

    if ((page_counts[page_num] >= MIGRATE_THRESH) && (dram_page_location[page_num]) < 0) {

        int64_t ref_itr = ref_ptr;

        while (dram_page_references[ref_itr] == true) {
            dram_page_references[ref_itr] = false;
            ++ref_itr;
            ref_itr = ref_itr%num_dram_pages;
        }

        if (dram_page_location[migrated_pages[ref_itr]] != -1) {

            page_counts[migrated_pages[ref_itr]] = 0;
            dram_page_location[migrated_pages[ref_itr]] = -1;
            cout << "Page : " << page_num << "returned to DRAM";
        }

        dram_page_location[page_num] = ref_itr;
        migrated_pages[ref_itr] = page_num;

        ref_ptr = (ref_itr + 1)%num_dram_pages;
        ref_itr = 0;
        num_migrations++;
    }

    if (dram_page_location[page_num] >= 0) {
        dram_page_references[dram_page_location[page_num]] = true;
    }
}

int main(int argc, char ** argv)
{
	bool use_multi=false;
	bool use_single=false;
	if (argc <=3) {
		usage();
		return 0;
	}
	if (strcasecmp(argv[1],"-fm") == 0) {
		use_multi=true;
	}
	if (strcasecmp(argv[1],"-fs") == 0) {
		use_single=true;
	}

    if (strcasecmp(argv[2],"lru") == 0) replacement_policy = lru;
    else if (strcasecmp(argv[2],"clock") == 0) replacement_policy = clock_repl;
    else replacement_policy = fifo;

	char * trace_file=argv[3];

	page_counts = new int64_t[num_nvm_pages];
    migrated_pages = new int64_t[num_dram_pages];
    dram_page_references = new bool[num_dram_pages];
    dram_page_location = new int64_t[num_nvm_pages];

    //Initialization

	for (int i=0; i < num_nvm_pages; i ++) {
        dram_page_location[i] = -1;
    }

    memset(migrated_pages,0,num_dram_pages*sizeof(int64_t));
    memset(dram_page_references,false,num_dram_pages*sizeof(bool));
    memset(page_counts,0,num_nvm_pages*sizeof(int64_t));

	FILE * trace_fp = fopen(trace_file, "rb");
	if (trace_fp == NULL) {
		cerr << "Unable to open trace file " << trace_file << " for reading" << endl;
		return 0;
	}
	//size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
	trace_tlb_tid_entry_t *buf_fm = new trace_tlb_tid_entry_t [1];
	trace_tlb_entry_t *buf_fs = new trace_tlb_entry_t [1];

	if (use_multi) {
		while (fread((void*)buf_fm, sizeof(trace_tlb_tid_entry_t), 1, trace_fp) == 1) {
			if (buf_fm[0].large == false) {
				int page_num = (buf_fm[0].va/4096) & (0xffffff);
				processPage(buf_fm[0].ts, buf_fm[0].tid, page_num, replacement_policy);
			}

		}
	}
	else
	{
		while (fread((void*)buf_fs, sizeof(trace_tlb_entry_t), 1, trace_fp) == 1) {
			if (buf_fs[0].large == false) {
				int page_num = (buf_fm[0].va/4096) & (0xffffff);
				processPage(buf_fs[0].ts, 0, page_num, replacement_policy);
			}
		}
	}

	cout << "Processed " << num_entries_in_trace << " trace entries " << endl;
	cout << "Performed " << num_migrations << " page migrations " << endl;

	return 0;
}





#endif /* TLB_COHERENCE_SIMULATOR_PAGE_MIGRATION_MODEL_HPP_ */
