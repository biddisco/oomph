#!/usr/bin/env python
# coding: utf-8

# In[1]:


from itertools import product
import math
import numpy as np
import inspect
import os
import time
import subprocess
from IPython.display import Image, display, HTML
import importlib
import socket
import argparse

# working dir
cwd = os.getcwd()

# hostname + cleanup login node 'daint101' etc
hostname = socket.gethostname()
if hostname.startswith('daint'):
    hostname = 'daint'
if hostname.startswith('uan'):
    hostname = 'eiger'

# name of this script
scriptname = inspect.getframeinfo(inspect.currentframe()).filename
scriptpath = os.path.dirname(os.path.abspath(scriptname))

# summary
print(f'CWD        : {cwd} \nScriptpath : {scriptpath} \nHostname   : {hostname}')


# In[2]:


def is_notebook():
    try:
        shell = get_ipython().__class__.__name__
        if shell == 'ZMQInteractiveShell':
            return True   # Jupyter notebook or qtconsole
        elif shell == 'TerminalInteractiveShell':
            return False  # Terminal running IPython
        else:
            return False  # Other type (?)
    except NameError:
        return False      # Probably standard Python interpreter

if is_notebook():
    # this makes the notebook wider on a larger screen using %x of the display
    display(HTML("<style>.container { width:100% !important; }</style>"))
    # save this notebook as a raw python file as well please
    get_ipython().system('jupyter nbconvert --to script generate-oomph.ipynb')


# In[3]:


# ------------------------------------------------------------------
# Command line params
# ------------------------------------------------------------------
def get_command_line_args(notebook_args=None):
    parser = argparse.ArgumentParser(description='Generator for oomph benchmarks')
    parser.add_argument('-d', '--dir', default=cwd, action='store', help='base directory to generate job scripts in')
    if is_notebook():
        parser.add_argument('-f', help='seems to be defaulted by jupyter')
        return parser.parse_args(notebook_args)
    return parser.parse_args()

notebook_args = '--dir /home/biddisco/benchmarking-results/test'.split()
if is_notebook():
    args = get_command_line_args(notebook_args)
else:
    args = get_command_line_args()


# In[4]:


def make_executable(path):
    mode = os.stat(path).st_mode
    mode |= (mode & 0o444) >> 2    # copy R bits to X
    os.chmod(path, mode)


# In[5]:


# strings with @xxx@ will be substituted by cmake
binary_dir = "@BIN_DIR@"

if args.dir:
    run_dir = args.dir
else:
    run_dir = "@RUN_DIR@"

print(f'Generating scripts in {run_dir}')


# In[6]:


#
# experimental code to try to generate a sensible number of messages given a message size
#
def sigmoid(x):
  return 1 / (1 + math.exp(-x))

def normalized_sigmoid_fkt(center, width, x):
   '''
   Returns array of a horizontal mirrored normalized sigmoid function
   output between 0 and 1
   '''
   s = 1/(1+np.exp(width*(x-center)))
   return s
   #return 1*(s-min(s))/(max(s)-min(s)) # normalize function to 0-1

def num_messages(vmax, vmin, center, width, x):
    s = 1 / (1 + np.exp(width*(x-center)))
    return vmin + (vmax-vmin)*s #(s-vmin)/(vmax-vmin) # normalize function to 0-1


# In[7]:


cscs = {}

# jb laptop
cscs["oryx2"] = {
  "Machine":"system76",
  "Cores": 8,
  "Threads per core": 2,
  "Allowed rpns": [1, 2],
  "Thread_array": [1,2,4],
  "Sleeptime":0,
  "Launch": "pushd {job_path} && source {job_file} && popd",
  "Run command": "mpiexec -n {total_ranks} --oversubscribe",
  "Batch preamble": """
#!/bin/bash -l

# Env
export OMP_NUM_THREADS={threads}
export GOMP_CPU_AFFINITY=0-{threadsm1}

# Commands
"""
}

# daint mc nodes config
cscs["daint"] = {
  "Machine":"daint",
  "Cores": 128,
  "Threads per core": 2,
  "Allowed rpns": [1],
  "Thread_array": [1,2,4,8,16],
  "Sleeptime":0.25,
  "Launch": "sbatch --chdir={job_path} {job_file}",
  "Run command": "srun --unbuffered --ntasks {total_ranks} --cpus-per-task {threads_per_rank}",
  "Batch preamble": """
#!/bin/bash -l
#SBATCH --job-name={run_name}_{transport}_{nodes}_{threads}_{inflight}_{size}
#SBATCH --time={time_min}
#SBATCH --nodes={nodes}
#SBATCH --partition=normal
#SBATCH --account=csstaff
#SBATCH --constraint=mc
#SBATCH --output=output.txt
#SBATCH --error=error.txt

# Env
export MPICH_MAX_THREAD_SAFETY=multiple
export OMP_NUM_THREADS={threads}
export MKL_NUM_THREADS={threads}
export GOMP_CPU_AFFINITY=0-{threadsm1}

# Debug
module list &> modules.txt
printenv > env.txt

# Commands
"""
}

cscs['eiger'] = cscs['daint']
cscs['eiger']['Machine'] = 'eiger'
cscs['eiger']['Cores'] = 64
cscs['eiger']['Thread_array'] = [1,2,4,8,16]


# In[8]:


print(cscs['eiger'])


# In[9]:


#
# Generate Job script preamble
#
def init_job_text(system, run_name, time_min, transport, nodes, threads, inflight, size):
    return system["Batch preamble"].format(run_name=run_name,
                                           time_min=time_min,
                                           transport=transport,
                                           nodes=nodes,
                                           threads=threads,
                                           threadsm1=(threads-1),
                                           inflight=inflight,
                                           size=size).strip()
#
# create a directory name from params
#
def make_job_directory(fdir,name, transport, nodes, threads, inflight, size):
    return f'{fdir}/{name}_{transport}_{nodes}_{threads}_{inflight}_{size}'

#
# create the launch command-line
#
def run_command(system, total_ranks, cpus_per_rank):
    return system["Run command"].format(total_ranks=total_ranks, cpus_per_rank=cpus_per_rank, threads_per_rank=cpus_per_rank)

#
# create dir + write final script for sbatch/shell or other job launcher
#
def write_job_file(system, launch_file, job_dir, job_text, suffix=''):
    job_path = os.path.expanduser(job_dir)
    os.makedirs(job_path, exist_ok=True)
    job_file = f"{job_path}/job_{suffix}.sh"
    print(f"Generating : {job_path} : {job_file}")

    with open(job_file, "w") as f:
        f.write(job_text)
        make_executable(job_file)

    launchstring  = system["Launch"].format(job_path=job_path,job_file=job_file) + '\n'
    launchstring += 'sleep ' + str(system['Sleeptime']) + '\n'
    launch_file.write(launchstring)

def execution_string(env, launch_cmd, prog_cmd, output_redirect):
    full_command = f"{env} {launch_cmd} {prog_cmd}".strip()
    command_prologue  = f'printf "\\n'
    command_prologue += f'# ----- Executing \\n'
    command_prologue += f'{full_command}    \\n'
    command_prologue += f'# --------------- \\n" >> {output_redirect}'
    command_epilogue  = f'printf "\\n'
    command_epilogue += f'# ----- Finished  \\n\\n" >> {output_redirect}'
    return '\n' + command_prologue + '\n' + full_command + ' >> ' + output_redirect + '\n' + command_epilogue + '\n'

#
# application specific commmands/flags/options that go into the job script
#
def oomph(system, bin_dir, timeout, transport, progs, nodes, threads, msg, size, inflight, extra_flags="", env=""):
    total_ranks = 2
    whole_cmd = ''

    # transport layers use '_libfabric', '_ucx', '_mpi', etc
    suffix = f'_{transport}'
    for prog in progs:
        if threads>1:
            prog = prog + '_mt'
        # generate the name of the output file we redirect output to
        outfile = f'{prog}_{msg}_{size}_{inflight}.out'

        # generate the program commmand with all command line params needed by program
        # this test has a different command line from others - not used on OOMPH yet
        if prog=='bench_p2p_pp_ft_avail':
            # test will run for 30s
            prog_cmd = f"{bin_dir}/{prog}{suffix} 30 {size} {inflight}"
        else:
            prog_cmd = f"{bin_dir}/{prog}{suffix} {msg} {size} {inflight}"

        # get the system launch command (mpiexec, srun, etc) with options/params
        launch_cmd = run_command(system, total_ranks, threads)

        # basic version of benchmark
        # generate a string that decorates and launches a singe instance of the test
        whole_cmd += execution_string(env, launch_cmd, prog_cmd, outfile)

        # for libfabric, run benchmark again with extra environment options to control execution
        if transport=='libfabric':
            #whole_cmd += execution_string(env + 'LIBFABRIC_AUTO_PROGRESS=1', launch_cmd, prog_cmd, outfile)
            whole_cmd += execution_string(env + 'LIBFABRIC_ENDPOINT_TYPE=multiple', launch_cmd, prog_cmd, outfile)
            whole_cmd += execution_string(env + 'LIBFABRIC_ENDPOINT_TYPE=scalable', launch_cmd, prog_cmd, outfile)
            whole_cmd += execution_string(env + 'LIBFABRIC_ENDPOINT_TYPE=threadlocal', launch_cmd, prog_cmd, outfile)

    return whole_cmd


# In[10]:


system = cscs[hostname]
#
job_name       = 'oomph'
timeout        = 400        # seconds per benchmark
time_min       = timeout*6 # total time estimate
timestr        = time.strftime('%H:%M:%S', time.gmtime(time_min))
ranks_per_node = 1
nodes_arr = [2]
trans_arr = ['libfabric', 'mpi']
thrd_arr  = system['Thread_array']
size_arr  = [1,10,100,1000,10000,100000,1000000]
nmsg_lut  = {1:500000,
             10:500000,
             100:500000,
             1000:500000,
             5000:250000,
             10000:250000,
             50000:250000,
             100000:250000,
             200000:250000,
             500000:100000,
             1000000:50000,
             2000000:25000}

#for i in size_arr:
#    print(int(num_messages(1E6, 25E3, 1E5, 1E-5, i)))

flight_arr= [1,4,16,64]
prog_arr  = ["bench_p2p_bi_cb_avail", "bench_p2p_bi_cb_wait", "bench_p2p_bi_ft_avail", "bench_p2p_bi_ft_wait"]
#prog_arr  = ["bench_p2p_pp_ft_avail", "bench_p2p_bi_ft_avail"]


# In[11]:


combos = 0

if run_dir.startswith('@'):
    print(f'Skipping creation of job launch file for {run_dir}')
else:
    job_launch = f"{run_dir}/launch.sh"
    job_launch_file = open(job_launch, "w")
    #
    job_launch_file.write("#!/bin/bash -l\n")

# generate all combinations in one monster loop
for nodes, transport, threads, size, inflight in product(
    nodes_arr, trans_arr, thrd_arr, size_arr, flight_arr):

    extra_flags = ""
    suffix = ""
    # number of messages (niter)
    msg = int(num_messages(1E6, 25E3, 1E5, 1E-5, size))
    msg = nmsg_lut[size]

    # create the output directory for each job
    job_dir = make_job_directory(run_dir, 'oomph', transport, nodes, threads, inflight, size)

    # first part of boiler plate job script
    job_text = init_job_text(system, job_name, timestr, transport, nodes, threads, inflight, size)

    # application specific part of job script
    job_text += oomph(
        system,
        binary_dir,
        timeout,
        transport,
        prog_arr,
        nodes,
        threads,
        msg,
        size,
        inflight,
        suffix,
        extra_flags,
    )
    # debugging
    # print(job_dir, '\n', job_text, '\n\n\n\n')

    combos += 1

    if combos==1:
        print('Uncommment the following line to perform the job creation')
    write_job_file(system, job_launch_file, job_dir, job_text)

make_executable(job_launch)
print(combos)


# In[ ]:




