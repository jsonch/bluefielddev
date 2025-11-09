A bidirectional wire from one port to another.
Reads packets from first port, sends them to second port.
And vice versa.
Each wire is on its own thread.

`build.sh` has a build command that works on bluefield dpu (outside of any docker containers, etc)

- Remember to set hugepages to at least 2048: `sudo sysctl -w vm.nr_hugepages=2048`

#### Usage

`./build.sh`

`./wire -l 0-2` (because it needs 3 cores -- main, rx, tx)
