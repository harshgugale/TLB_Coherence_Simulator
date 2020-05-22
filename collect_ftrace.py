#!/usr/bin/python3

import subprocess
from subprocess import Popen,PIPE
import shlex
import os
import time
import re

suite_to_run          =     ["parsec","phoenix","nas"]
benchmark_to_run      =     ["raytrace"]
run_benchmark         =     [1      ,1      ,1      ,1      ,1      ,1      ,1      ,1]
generate_latency_info =     [0      ,0      ,1      ,1      ,1      ,1      ,1      ,1]
freq_of_op            =     [[]      ,[]     ,[]    ,[]     ,[]     ,[]     ,[]     ,[]]

parsec_basedir = "/home/harsh/tlb_coherence/parsec-benchmark"
ftrace_basedir = "/sys/kernel/debug/tracing"
output_dir = "/home/harsh/tlb_coherence/measurements"
freq_cnt_delay = 0.5

nas_base_dir = "/home/harsh/tlb_coherence/NPB3.4/NPB3.4-OMP/bin"
rodinia_base_dir = "/home/harsh/Downloads/rodinia_3.1/openmp"
wordcount_base_dir = "/home/harsh/tlb_coherence/phoenix/sample_apps/"
rsbench_base_dir = "/home/harsh/RSBench/src"

benchmark_dir = {
    "parsec":
    {
    "dedup":parsec_basedir + "/pkgs/kernels/dedup/run",
    "vips":parsec_basedir + "/pkgs/apps/vips/run",
    "streamcluster":parsec_basedir + "/pkgs/kernels/streamcluster/run",
    "x264": parsec_basedir + "/pkgs/apps/x264/run",
    "freqmine":parsec_basedir + "/pkgs/apps/freqmine/run",
    "canneal":parsec_basedir + "/pkgs/kernels/canneal/run",
    "raytrace" : parsec_basedir + "/pkgs/apps/raytrace/run",
    "blackscholes" : parsec_basedir + "/pkgs/apps/blackscholes/run",
    "bodytrack":parsec_basedir + "/pkgs/apps/bodytrack/run",
    "swaptions": parsec_basedir + "/pkgs/apps/swaptions/run",
    "facesim": parsec_basedir + "/pkgs/apps/facesim/run",
    "ferret" : parsec_basedir + "/pkgs/apps/ferret/run"

    },
    "nas":
    {
    "ft":nas_base_dir,
    "cg":nas_base_dir,
    "cg_large":nas_base_dir,
    "dc":nas_base_dir,
    "ep":nas_base_dir,
    "is":nas_base_dir,
    "lu":nas_base_dir,
    "mg":nas_base_dir,
    "dc_large":nas_base_dir,
    "ua":nas_base_dir
    },
    "rodinia":
    {
    "bfs" : rodinia_base_dir + "/bfs",
    "kmeans" : rodinia_base_dir + "/kmeans",
    "heartwall" : rodinia_base_dir + "/heartwall",
    "leukocyte" : rodinia_base_dir + "/leukocyte",
    "lavamd" : rodinia_base_dir + "/lavaMD"
    },
    "phoenix":
    {
    "wordcount" : wordcount_base_dir + "/word_count",
    "kmeans" : wordcount_base_dir + "/kmeans",
    "matrix_multiply" : wordcount_base_dir + "/matrix_multiply",
    "pca" : wordcount_base_dir + "/pca",
    "reverse_index" : wordcount_base_dir + "/reverse_index",
    "string_match" : wordcount_base_dir + "/string_match"
    },
    "rsbench":
    {
    "default" : rsbench_base_dir
    }
} 

benchmark_cmdline = {
    "parsec":
    {
    "dedup" : parsec_basedir + "/pkgs/kernels/dedup/inst/amd64-linux.gcc/bin/dedup -c -p -v -t 12 -i FC-6-x86_64-disc1.iso -o output.dat.ddp",
    "vips" : parsec_basedir + "/pkgs/apps/vips/inst/amd64-linux.gcc/bin/vips im_benchmark orion_18000x18000.v output.v",
    "streamcluster": parsec_basedir +"/pkgs/kernels/streamcluster/inst/amd64-linux.gcc/bin/streamcluster 10 20 128 1000000 200000 5000 none output.txt 12",
    "x264":parsec_basedir + "/pkgs/apps/x264/inst/amd64-linux.gcc/bin/x264 --quiet --qp 20 --partitions b8x8,i4x4 --ref 5 --direct auto --b-pyramid --weightb --mixed-refs --no-fast-pskip --me umh --subme 7 --analyse b8x8,i4x4 --threads 12 -o eledream.264 eledream_1920x1080_512.y4m",
    "freqmine":parsec_basedir + "/pkgs/apps/freqmine/inst/amd64-linux.gcc/bin/freqmine webdocs_250k.dat 11000",
    "canneal" : parsec_basedir + "/pkgs/kernels/canneal/inst/amd64-linux.gcc/bin/canneal 12 15000 2000 2500000.nets 6000",
    "raytrace" : parsec_basedir + "/pkgs/apps/raytrace/inst/amd64-linux.gcc/bin/rtview thai_statue.obj -automove -nthreads 12 -frames 200 -res 1920 1080",
    "blackscholes" : parsec_basedir + "/pkgs/apps/blackscholes/inst/amd64-linux.gcc/bin/blackscholes 12 in_10M.txt prices.txt",
    "bodytrack": parsec_basedir + "/pkgs/apps/bodytrack/inst/amd64-linux.gcc/bin/bodytrack sequenceB_261 4 261 4000 5 0 12",
    "swaptions" : parsec_basedir + "/pkgs/apps/swaptions/inst/amd64-linux.gcc/bin/swaptions -ns 128 -sm 1000000 -nt 12",
    "facesim" : parsec_basedir + "/pkgs/apps/facesim/inst/amd64-linux.gcc/bin/facesim -timing -threads 16",
    "ferret" : parsec_basedir + "/pkgs/apps/ferret/inst/amd64-linux.gcc/bin/ferret corel lsh queries 50 20 12 output.txt"
    },
    "nas":
    {
    "ft" : "./ft.C.x",
    "cg" : "./cg.C.x",
    "dc" : "./dc.A.x",
    "ep" : "./ep.D.x",
    "is" : "./is.C.x",
    "lu" : "./lu.C.x",
    "mg" : "./mg.C.x",
    "dc_large" : "./dc.B.x",
    "cg_large" : "./cg.D.x",
    "ua" : "./ua.D.x"
    },
    "rodinia":
    {
    "bfs" : "./bfs 4 ../../data/bfs/graph1MW_6.txt",
    "kmeans" : "./kmeans_openmp/kmeans -n 12 -i ../../data/kmeans/kdd_cup",
    "heartwall" : "./heartwall ../../data/heartwall/test.avi 100 12",
    "leukocyte" : "./OpenMP/leukocyte 20 12 ../../data/leukocyte/testfile.avi",
    "lavamd" : "./lavaMD -cores 12 -boxes1d 20"
    },
    "phoenix":
    {
    "wordcount" : "./wordcount_pthreads ../../../../Downloads/word_count_datafiles/word_100MB.txt",
    "kmeans" : "./kmeans-pthread",
    "matrix_multiply" : "./matrix_mult_pthreads 2000",
    "pca" : "./pca-pthread -r 4000 -c 4000 -s 16000000",
    "reverse_index" : "./reverseindex-pthread reverse_index_datafiles/",
    "string_match" : "./string_match_pthreads string_match_datafiles/key_file_500MB.txt"
    },
    "rsbench":
    {
    "default" : "./rsbench -s large -m history -l 34 -p 300000 -P 100000 -W 100000 -t 16"
    }
}

#print(echo_string + ["100000",ftrace_basedir+"/buffer_size_kb"])

def main():

    subprocess.run("echo 100000 > " + ftrace_basedir+"/buffer_size_kb", shell=True)
    subprocess.run("echo funcgraph-proc > " + ftrace_basedir + "/trace_options", shell=True)
    subprocess.run("echo native_flush_tlb_others> " + ftrace_basedir + "/set_ftrace_filter", shell=True)
    subprocess.run("echo smp_call_function* >> " + ftrace_basedir + "/set_ftrace_filter", shell=True)
    subprocess.run("echo 1 > " + ftrace_basedir + "/events/tlb/tlb_flush/enable", shell=True)
    subprocess.run("echo 1 > " + ftrace_basedir + "/events/probe/smp_call_function_many/enable",shell=True)
    #subprocess.run("echo flush_tlb_func_remote >> " + ftrace_basedir + "/set_ftrace_filter", shell=True)
    #subprocess.run("echo *apic* >> " + ftrace_basedir + "/set_ftrace_filter", shell=True)

    for suite,bench_list in benchmark_dir.items():
        for bench_name,val in bench_list.items():
            if suite in suite_to_run:
                if bench_name in benchmark_to_run:

                    os.chdir(val)

                    dry_run_for_freq_mem = True

                    if generate_latency_info[benchmark_to_run.index(bench_name)] == True:
                        num_of_itr = 2
                    else:
                        num_of_itr = 1

                    if (run_benchmark[benchmark_to_run.index(bench_name)]):

                        for i in range(num_of_itr):

                            if dry_run_for_freq_mem == True:
                                print("[INFO] Dry Run for Freq and Mem measurement")
                            else:
                                print("[INFO] Actual Run")

                            print("[INFO] Running benchmark : " + bench_name)

                            subprocess.run("echo 0 > "+ftrace_basedir+"/tracing_on",shell=True)
                            subprocess.run("echo nop > "+ftrace_basedir+"/current_tracer",shell=True)
                            subprocess.run("echo function_graph > "+ftrace_basedir+"/current_tracer",shell=True)

                            subprocess.run("echo 1 > " + ftrace_basedir+"/tracing_on",shell=True)
                            subprocess.run("echo trace_start > " + ftrace_basedir + "/trace_marker",shell=True)

                            process = subprocess.Popen(shlex.split(benchmark_cmdline.get(suite).get(bench_name)))

                            time.sleep(freq_cnt_delay)

                            while(process.poll() == None and dry_run_for_freq_mem):
                                mem = subprocess.Popen("ps -aux | grep \"" + benchmark_cmdline.get(suite).get(bench_name)+"\"",stdout=PIPE,shell=True)
                                freq = subprocess.Popen("cpufreq-info | grep \'current CPU frequency\'",stdout=PIPE,shell=True)

                                itr = 0

                                freq_str = str(freq.stdout.read())

                                print(freq_str)

                                match = re.findall("current CPU frequency is ([0-9.]*) GHz",freq_str)

                                num_of_cpu = 0
                                temp = 0

                                for cpu_freq in match:
                                    temp = temp + float(cpu_freq)
                                    num_of_cpu = num_of_cpu + 1

                                temp = temp / num_of_cpu

                                print(num_of_cpu)

                                assert(num_of_cpu == 8)

                                freq_of_op[benchmark_to_run.index(bench_name)].append(temp)

                                print(freq_of_op[benchmark_to_run.index(bench_name)])

                                for i in str(mem.stdout.readline()).split(' '):

                                    if itr == 4 and i!='' and i!=' ':
                                        print("[INFO] VSZ "+str(round(float(i)/float(1024))),end='MB ')

                                    if itr == 5 and i!='' and i!=' ':
                                        print("RSS "+str(round(float(i)/float(1024)))+"MB")

                                    if i!='' and i!=' ':
                                        itr = itr + 1

                                time.sleep(freq_cnt_delay)

                            dry_run_for_freq_mem = False

                            #print(freq_of_op[benchmark_to_run.index(bench_name)])

                            freq_to_use = sum(freq_of_op[benchmark_to_run.index(bench_name)])/len(freq_of_op[benchmark_to_run.index(bench_name)])

                            print(freq_to_use)

                            process.wait()

                            subprocess.run("echo trace end > " + ftrace_basedir+"/trace_marker",shell=True)
                            #subprocess.run("echo -1 > " + ftrace_basedir + "/set_ftrace_pid",shell=True)
                            subprocess.run("echo 0 > " + ftrace_basedir + "/tracing_on",shell=True)
                            subprocess.run(["/bin/cp",ftrace_basedir+"/trace",output_dir+"/ftrace_logs/"+bench_name+"_trace"])

                    if (generate_latency_info[benchmark_to_run.index(bench_name)]):
                        print("[INFO] Generating latency logs")
                        parse_log(output_dir+"/ftrace_logs/"+bench_name+"_trace",bench_name,freq_to_use)


def parse_log(file_name,benchname,freq_to_use):
    f = open(file_name,"r")

    if f=='':
        return
    
    benchmark = benchname
    #print("Freq_to_use" + str(freq_to_use))
    outfile = open( output_dir+"/ftrace_latencies/"+benchname+".log","w")
    initiators = []
    pattern_shootdown_detected =  re.compile("\s+(\d)\)\s+(.*)-.*\s+\|[ +!#]+[0-9a-z. ]*\s+\|\s+native_flush_tlb_others\(\)")
    pattern_victim_interrupt = re.compile("\s+(\d)\)\s+(.*)-.*\s+\|[ +!#]+[0-9a-z. ]*\s+\|\s+(smp_call_function_single_interrupt|smp_call_function_interrupt)\(\)")
    pattern_victim_interrupt_without_shootdown = re.compile("\s+(\d)\)\s+(.*)-.*\s+\|[ +!#]*([0-9.]*)\s*\w*\s+\|\s+(smp_call_function_single_interrupt|smp_call_function_interrupt)\(\)\s*;")
    pattern_time = re.compile("\s+(\d)\)\s+(.*)-.*\s+\|[ +!#]*([0-9.]*)\s*\w*\s+\|\s+\}")
    tracer_start = re.compile(".*/\* trace_start \*/")
    smp_flush_tlb_probe = re.compile(".*smp_call_function_many:.*bits=0x([0-9a-z]*)")
    tlb_flush_victim_probe =re.compile("\s+(\d)\)\s+.*-.*\s+\|\s+\|.*tlb_flush: pages:([-0-9]*) reason:remote shootdown")

    victims = []
    initiator_latency = 0
    victim_latency = 0
    found_shootdown = 0
    found_trace_start = False;

    cnt = []
    victim_flush_pages = {"0":0,"1":0,">1":0,"-1":0}
    victim_flush_page_latencies = {"0":0,"1":0,">1":0,"-1":0}
    initiator_procs = {}
    victim_procs = {}

    ini_count = 0
    vic_count = 0
    vic_count_without_shootdown = 0
    
    for line in f:

        if (tracer_start.match(line)):
            found_trace_start = True;

        outfile.write(repr(line)+"\n")
    
        if found_shootdown == 1:
            match = pattern_victim_interrupt_without_shootdown.match(line)

            if match:
                outfile.write("[INFO] Found victim without shootdown\n")            
                victim_latency = victim_latency + float(match.group(3))
                vic_count = vic_count + 1 
                vic_count_without_shootdown = vic_count_without_shootdown + 1

            else:
                match = pattern_victim_interrupt.match(line) 

                if match:

                    if match.group(2) not in victim_procs:
                        victim_procs[match.group(2)] = 1
                    else:
                        victim_procs[match.group(2)] = victim_procs[match.group(2)] + 1

                    victims.append(int(match.group(1)))

                #---------------------

                match = tlb_flush_victim_probe.match(line)

                if match:
                    outfile.write("[INFO] Victim flush core : "+match.group(1)+" pages : "+match.group(2)+"\n")

                    pages = int(match.group(2))

                    if pages == 0:
                        victim_flush_pages["0"] = victim_flush_pages["0"]+1
                        outfile.write("[INFO] Found 0 victim page invalidation\n")

                    elif pages == 1:
                        victim_flush_pages["1"] = victim_flush_pages["1"]+1
                        outfile.write("[INFO] Found 1 victim page invalidation\n")

                    elif pages >1:
                        victim_flush_pages[">1"] = victim_flush_pages[">1"]+1
                        outfile.write("[INFO] Found >1 victim page invalidation\n")

                    elif pages == -1:
                        victim_flush_pages["-1"] = victim_flush_pages["-1"]+1
                        outfile.write("[INFO] Found -1 victim page invalidation\n")

                    else:
                        raise ValueError


        		#---------------------

                match = pattern_time.match(line)
            
                if match:
                    outfile.write("[INFO] Core number: "+match.group(1))
                    if int(match.group(1)) in victims:
                        victim_latency = victim_latency + float(match.group(3))
                        vic_count = vic_count + 1
                        outfile.write(" "+str(victim_latency)+" ")
                        outfile.write(" Victims " + str(victims))
                        victims.remove(int(match.group(1)))
                        outfile.write(" Victims after removing " + str(victims)+"\n")

            
                    elif int(match.group(1)) in initiators:
                        initiator_latency = initiator_latency + float(match.group(3))
                        ini_count = ini_count + 1
                        outfile.write(" "+str(initiator_latency)+" ")
                        outfile.write(" Initiators" + str(initiators))
                        initiators.remove(int(match.group(1))) 
                        outfile.write(" Initiators after removing" + str(initiators)+"\n")  


                    if victims == [] and initiators == []:
                        found_shootdown = 0

                    if initiators == [] and victims != []:
                        outfile.write("[WARN] Victims active without initiators\n")

                    if len(initiators) > 2:
                        outfile.write("[WARN] More than 2 initiators active\n")

        match = smp_flush_tlb_probe.match(line)

        if match:
        	cnt.append(bin(int(match.group(1),16)).count("1"))
        	
        match = pattern_shootdown_detected.match(line)
    
        if match:
            if (found_trace_start == False):
                print("Trace overflow")
                exit(0)

            if match.group(2) not in initiator_procs:
                initiator_procs[match.group(2)] = 1
            else:
                initiator_procs[match.group(2)] = initiator_procs[match.group(2)] + 1


            outfile.write("Initiator found\n") 
            initiators.append(int(match.group(1)))
            found_shootdown = 1   

    if ini_count != 0 or vic_count != 0:
        outfile.write("Freq_to_use : {}\n".format(freq_to_use))
        outfile.write("Average number of victims : {}\n".format(sum(cnt)/len(cnt)))
        outfile.write("Page invalidation histogram : "+str(victim_flush_pages) + "\n")
        outfile.write("Initiator Latency : {}\n".format(initiator_latency))
        outfile.write("Total number of Initiator shootdowns : {}\n".format(ini_count))
        outfile.write("Victim Latency : {}\n".format(victim_latency))
        outfile.write("Total number of Victim shootdowns : {}\n".format(vic_count))
        outfile.write("Total number of Victim ISR entry without shootdown : {}\n".format(vic_count_without_shootdown))
        outfile.write("Avg Initiator : {}\n".format(initiator_latency*freq_to_use*1000/ini_count ))
        outfile.write("Avg Victim : {}\n".format(victim_latency*freq_to_use*1000/vic_count ))     
    else:
        outfile.write("\n")
        outfile.write("No shootdown found")

    print("Init " + str(initiator_procs))
    print("Vict " + str(victim_procs))


if __name__ == '__main__':
    main()
