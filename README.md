# What is this?

**psfiles** is a simple utility to view file system activity of Linux processes.
Only regular *(p)read(v)*, *(p)write(v)*, *open(at)*, *close*, *rename(at)*, *unlink(at)* syscalls are traced.
If the file has been memory mapped, this utility will NOT show the number of bytes read or written.

# Features

* start new process or attach to existing one and trace its file system activity
* output results to standard output or save results to file
* custom results sorting and filtering

# System requirements

* 64 bit architecture
* Linux kernel >= 5.3
* Glibc >= 2.31 or Musl >= 1.2.0

# Options

* **[--output, -o]:** path to output file. Default: *stdout*.
* **[--delay, -d]:** interval (seconds) between file list updates. Default: *1*.
* **[--sort, -s]:** column name to sort by (append "-" to column name to sorting in descending order). Default: *path*.
* **[--filter, -f]:** glob to filter file paths. Default: *\**.
* **--pid, -p:** attach to existing process with specified *pid*.
* **--cmdline, -c:** spawn new process with specified *cmdline*. Incompatible with **--pid** option. It should be the last option.

# Columns

* **path** - path to file,
* **wsize** - write size in bytes,
* **rsize** - read size in bytes,
* **wcount** - (p)write(v) syscalls count,
* **rcount** - (p)read(v) syscalls count,
* **ocount** - open(at)/creat syscalls count,
* **ccount** - close syscalls count,
* **spec** - special file events indicator: memory map (m), rename (r), unlink (u),
* **lthread**, **laccess** - thread id and time of the last system call listed above.

# Usage examples

**psfiles** should be launched by a privileged user (CAP_SYS_PTRACE capability is required).

Start new process, sort descending by write size, output to file, update output every minute:
* <code>psfiles -d 60 -s wsize- -o output.txt -c emacs /home/user/cpp/main.cpp</code>

Attach to existing process, sort by path, output to stdout, update output every second, filter files from user home directory:
* <code>psfiles -f "/home/user/*" -p $(pidof gedit)</code>

# Control

If **--output** option was not specified, keyboard control is available:

* **0 - 9:** sort by specified column (0 - path, 1 - wsize, etc)
* **s:** toggle sorting order
* **n:** show next page (scroll down)
* **p:** show previous page (scroll up)
* **q:** quit

# Screencast

![psfiles screencast](.sample/screencast.gif)

# How to build?

* <code>git clone https://github.com/mukovnin/psfiles</code>
* <code>cd psfiles</code>
* <code>cmake . -B build</code>
* <code>cd build</code>
* <code>make</code>
* if needed, run <code>make install</code>

If you are an Arch Linux user, there is [AUR package](https://aur.archlinux.org/packages/psfiles).
