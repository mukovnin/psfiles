# What is this?

**Psfiles** is a simple utility to view file I/O activity of Linux processes.
Only regular *read(v)*, *write(v)*, *open(at)*, *close* syscalls are traced.
No mmap etc support currently.

# Features

* start new process and trace it file I/O activity
* attach to existing process and trace it
* output results to standard output or save results to file
* custom results sorting

# Options

* **[--output, -o]:** path to output file. Default: *stdout*.
* **[--delay, -d]:** interval (seconds) between file list updates. Default: *1*.
* **[--sort, -s]:** column name for sorting results (append "-" to column name to sorting in descending order).
Column names: *path wsize rsize wcount rcount ocount ccount laccess*. Default: *path*.
* **--pid, -p:** attach to existing process with specified *pid*.
* **--cmdline, -c:** spawn new process with specified *cmdline*. Incompatible with --pid option. It should be the last option.

# Usage

Start new process or attach to existing process:

* <code>psfiles -d 5 -s wsize- -c emacs /home/user/cpp/main.cpp</code>
* <code>psfiles -d 30 -s path -o output.txt -p $(pidof emacs)</code>

If --output option was not specified, keyboard control is available:

* **1 - 8:** sort by specified column (1 - path, 2 - wsize, etc)
* **s:** toggle sorting order
* **q:** quit

# Screenshot

![psfiles screenshot](.sample/screenshot.png)

# How to build?

* clone this repository: <code>git clone https://github.com/mukovnin/psfiles</code>
* change current directory to cloned repository: <code>cd psfiles</code>
* create build directory, i.e. <code>mkdir build</code>
* run <code>cmake . -B build</code>
* change current directory: <code>cd build</code>
* run <code>make</code>
* if needed, run <code>make install</code>
