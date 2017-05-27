'mockps': mock/fake a running process (Linux only)
--------------------------------------------------

#### Purpose

    When developing/configuring/testing/verifying HA systems, it may unnecessary to really start applications which need extra setups. The 'mockps' utility is such a tool to deceive ps checking with least effort.

#### Build

```
$ git clone https://github.com/peihanw/mockps.git
$ cd mockps/
$ g++ -o mockps mockps.cc
```

#### Usage

```
usage: ./mockps [-s sleep] [-u] [-v verbose] -m "mocked process with optional args"
       -s : sleep duration in seconds, default 0(infinite)
       -u : uniq mocked process, default non-uniq constraint
       -v : verbose level, 0:OFF, 1:ERO, 2:WRN, 3:INF, 4:DBG, 5:TRC, default 1
       -m : mocked process command line, quoted please
eg.    ./mockps -s 60 -u -m "nginx: master (openresty)"
```

