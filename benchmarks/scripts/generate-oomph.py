#!/usr/bin/env python
# coding: utf-8

# In[1]:


from itertools import product
import math
import numpy as np
import inspect
import os
import time
from IPython.display import Image, display, HTML
import importlib
import socket
import argparse

# working dir
cwd = os.getcwd()

# name of this script
scriptname = inspect.getframeinfo(inspect.currentframe()).filename
scriptpath = os.path.dirname(os.path.abspath(scriptname))


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
    parser.add_argument('-t', '--type', default='normal', action='store', help='normal, timed or native for different test types')
    parser.add_argument('-T', '--timeout', default=120, action='store', help='executable timeout period')
    parser.add_argument('-m', '--machine', default='', action='store', help='select machine batch job config/preamble')
    if is_notebook():
        parser.add_argument('-f', help='seems to be defaulted by jupyter')
        return parser.parse_args(notebook_args)
    return parser.parse_args()

notebook_args = '--type=native --dir /home/biddisco/benchmarking-results/test'.split()
if is_notebook():
    args = get_command_line_args(notebook_args)
else:
    args = get_command_line_args()


# In[4]:


# hostname + cleanup login node 'daint101' etc
if args.machine != '':
    hostname = args.machine
elif os.environ.get('LUMI_STACK_NAME', 'oryx2') == 'LUMI':
    hostname = 'lumi'
elif socket.gethostname().startswith('daint'):
    hostname = 'daint'
else :
    hostname = 'oryx2'

# summary
print(f'CWD        : {cwd} \nScriptpath : {scriptpath} \nHostname   : {hostname}')


# In[5]:


def make_executable(path):
    mode = os.stat(path).st_mode
    mode |= (mode & 0o444) >> 2    # copy R bits to X
    os.chmod(path, mode)


# In[6]:


# strings with @xxx@ will be substituted by cmake
binary_dir = "@BIN_DIR@"

if args.dir:
    run_dir = args.dir
else:
    run_dir = "@RUN_DIR@"

print(f'Generating scripts in {run_dir}')


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
  "Run command": "mpiexec -n {total_ranks} --oversubscribe timeout {timeout} ",
  "Batch preamble": """
#!/bin/bash -l

# Env
#export OMP_NUM_THREADS={threads}
#export GOMP_CPU_AFFINITY=0-{threadsm1}

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
  "Run command": "srun --cpu-bind=cores --unbuffered --ntasks {total_ranks} --cpus-per-task {threads_per_rank} timeout {timeout} ",
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

module swap craype/2.7.10 craype/2.7.15

# alternatives : srun --cpu-bind v,mask_cpu:0xffff
# export GOMP_CPU_AFFINITY=0-{threadsm1}

# Old Env vars that might be useful
# export MPICH_MAX_THREAD_SAFETY=multiple
# export OMP_NUM_THREADS={threads}
# export MKL_NUM_THREADS={threads}
# export MPICH_GNI_NDREG_ENTRIES=1024

# Debug
module list &> modules.txt
printenv > env.txt

# Commands
"""
}

# daint mc nodes config
cscs["lumi"] = {
  "Machine":"lumi",
  "Cores": 16,
  "Threads per core": 2,
  "Allowed rpns": [1],
  "Thread_array": [1,2,4,8,16],
  "Sleeptime":0.25,
  "Launch": "sbatch --chdir={job_path} {job_file}",
  "Run command": "srun --cpu-bind=cores --unbuffered --ntasks {total_ranks} --cpus-per-task {threads_per_rank} timeout {timeout} ",
  "Batch preamble": """
#!/bin/bash -l
#SBATCH --job-name={run_name}_{transport}_{nodes}_{threads}_{inflight}_{size}
#SBATCH --time={time_min}
#SBATCH --nodes={nodes}
#SBATCH --partition=standard
#SBATCH --account=project_465000105
#SBATCH --output=output.txt
#SBATCH --error=error.txt

module load LUMI/22.06
module load cpeGNU
module load buildtools
module load Boost

# alternatives : srun --cpu-bind v,mask_cpu:0xffff
# export GOMP_CPU_AFFINITY=0-{threadsm1}

export MPICH_MAX_THREAD_SAFETY=multiple
# export OMP_NUM_THREADS={threads}
# export MKL_NUM_THREADS={threads}
# export MPICH_GNI_NDREG_ENTRIES=1024

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
def run_command(system, total_ranks, cpus_per_rank, timeout):
    return system["Run command"].format(total_ranks=total_ranks, cpus_per_rank=cpus_per_rank, threads_per_rank=cpus_per_rank, timeout=timeout)

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

#
# generate a string that decorates and launches a single instance of the test
#
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
# generate application specific commmands/flags/options that go into the job script
#
def oomph_original(system, bin_dir, timeout, transport, progs, nodes, threads, msg, size, inflight, env):
    total_ranks = 2
    whole_cmd = ''
    suffix = ''

    # transport layers use '_libfabric', '_ucx', '_mpi', etc
    if args.type!='native':
        suffix = f'_{transport}'

    # timed version uses seconds instead of messages/iterations
    if args.type=='timed':
        msg = 30

    # always remember to add a space to the end of each env var for concatenation of many of them
    if threads==1:
        env +=  'MPICH_MAX_THREAD_SAFETY=single '
    else:
        env +=  'MPICH_MAX_THREAD_SAFETY=multiple '
        env += f'OMP_NUM_THREADS={threads} '
        env += f'GOMP_CPU_AFFINITY=0-{threads} '

    for prog in progs:
        if threads>1:
            if args.type=='normal' or args.type=='timed':
                prog = prog + '_mt'

        if transport=='native' and threads==1:
            prog = prog.replace('_mt_','_')

        # generate the name of the output file we redirect output to
        outfile = f'{prog}_N{nodes}_T{threads}_I{msg}_S{size}_F{inflight}.out'

        # generate the program commmand with all command line params needed by program
        prog_cmd = f"{bin_dir}/{prog}{suffix} {msg} {size} {inflight}"

        # get the system launch command (mpiexec, srun, etc) with options/params
        launch_cmd = run_command(system, total_ranks, threads, timeout)

        if transport=='libfabric':
            env2 = env + 'LIBFABRIC_POLL_SIZE=32 '
            #for ep in ['single', 'multiple', 'scalableTx', 'threadlocal']:
            for ep in ['threadlocal']:
                whole_cmd += execution_string(env2 + f"LIBFABRIC_ENDPOINT_TYPE={ep} ", launch_cmd, prog_cmd, outfile)
            if False: # add option to enable this?
                whole_cmd += execution_string(env2 + f"LIBFABRIC_ENDPOINT_TYPE={ep} " + f"LIBFABRIC_AUTO_PROGRESS=1 ", launch_cmd, prog_cmd, outfile)
        else:
            whole_cmd += execution_string(env, launch_cmd, prog_cmd, outfile)

    return whole_cmd


# In[9]:


system = cscs[hostname]
#
job_name       = 'oomph'
timeout        = args.timeout
time_min       = 2000*60 # total time estimate
timestr        = time.strftime('%H:%M:%S', time.gmtime(time_min))
ranks_per_node = 1
nodes_arr = [2]
thrd_arr  = system['Thread_array']
size_arr  = [1,10,100,1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000]
nmsg_lut  = {1:500000,
             10:500000,
             100:500000,
             1000:500000,
             2000:500000,
             5000:250000,
             10000:250000,
             20000:250000,
             50000:250000,
             100000:250000,
             200000:250000,
             500000:100000,
             1000000:50000,
             2000000:25000}

flight_arr = [1,10,50,100]

if args.type=='normal':
    trans_arr = ['libfabric', 'mpi']
    prog_arr  = [
        #"bench_p2p_bi_cb_avail",
        #"bench_p2p_bi_cb_wait",
        "bench_p2p_bi_ft_avail",
        #"bench_p2p_bi_ft_wait"
    ]

if args.type=='timed':
    trans_arr = ['libfabric', 'mpi']
    prog_arr  = ['bench_p2p_pp_ft_avail']

if args.type=='native':
    trans_arr = ['native']
    prog_arr  = [
        #"mpi_p2p_bi_avail_mt_test", "mpi_p2p_bi_avail_mt_testany",
        #"mpi_p2p_bi_wait_mt_wait",
        "mpi_p2p_bi_wait_mt_waitall"
    ]


# In[10]:


combos = 0

if run_dir.startswith('@'):
    print(f'Skipping creation of job launch file for {run_dir}')
else:
    job_launch = f"{run_dir}/launch.sh"
    job_launch_file = open(job_launch, "w")
    #
    job_launch_file.write("#!/bin/bash -l\n")

# create the output directory for each job
job_dir = make_job_directory(run_dir, 'oomph', "all", 2, 1, 1, 1)

# first part of boiler plate job script
job_text = init_job_text(system, job_name, timestr, "all", 2, 16, 1, 1)

# generate all combinations in one monster loop
for nodes, transport, threads, size, inflight in product(nodes_arr, trans_arr, thrd_arr, size_arr, flight_arr):

    env = ""
    msg = nmsg_lut[size]

    # create the output directory for each job
    #job_dir = make_job_directory(run_dir, 'oomph', transport, nodes, threads, inflight, size)

    # first part of boiler plate job script
    #job_text = init_job_text(system, job_name, timestr, transport, nodes, threads, inflight, size)

    env = 'MPICH_GNI_NDREG_ENTRIES=1024 '

    # application specific part of job script
    job_text += oomph_original(
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
        env
    )
    # debugging
    # print(job_dir, '\n', job_text, '\n\n\n\n')

    combos += 1

    if combos==1:
        print('Uncommment the following line to perform the job creation')

write_job_file(system, job_launch_file, job_dir, job_text)

make_executable(job_launch)
print('Combinations', combos, 'est-time', combos*4*2,'minutes')


# In[ ]:




