A bidirectional wire from one port to another.
Reads packets from first port, sends them to second port.
And vice versa.
Each wire is on its own thread.

Minimal dependencies and simplest possible code.

*Make sure that hugepages are configured before running. If not done already, a reasonable default is:* `sudo sysctl -w vm.nr_hugepages=2048`

#### Basic Usage

`./build.sh` -- compile the program. This is a script to show the basic way to compile a dpdk app on the bluefield with minimal toolchain dependencies.

`./wire` -- print information about available ports.


`./wire -l 0-2 -- X Y` start a bidirectional wire between ports X and Y.


#### Basic Demo


assume setup is: 

`host1 <--> bf1 <--> host2`

- host1 is a machine hosting a bluefield nic

- bf1 is the bluefield nic in host1

- host2 is another machine physically connected to bf1's first data plane eth port

- our goal is for the bf1 arm core to act as a bump in the wire, processing each packet from host1 <--> host2

##### Step 1:

run wire on bf1 to get the port information. 

```bash
ubuntu@bf1:~/proj/biwire$ sudo ./wire
```
```bash
Port 2:
  DPDK Name: 0000:03:00.0
  Linux Interface: p0
  MAC Address: 08:C0:EB:B2:3C:F0
  Driver: mlx5_pci
  Device: 0000:03:00.0
  Max RX queues: 1024
  Max TX queues: 1024
  Link Status: UP
  Link Speed: 25000 Mbps
  Link Duplex: Full

Port 3:
  DPDK Name: 0000:03:00.0_representor_vf4294967295
  Type: Representor Port
  Linux Interface: pf0hpf
  MAC Address: 06:28:FD:42:A0:EB
  Driver: mlx5_pci
  Device: 0000:03:00.0
  Max RX queues: 1024
  Max TX queues: 1024
  Link Status: UP
  Link Speed: 25000 Mbps
  Link Duplex: Full
``` 

You are looking for a pair of ports that represent a physical port and a virtual port to the host. The bluefield sets this up by default. You do not need to use any ovs configurations or SFs or containers etc. This should work from first OS install on the bluefield.

The ports will typically be named `p0` (physical port) `pf0hpf` (virtual port to host). There is also a `p1` and `pf1hpf`. Same thing, different physical port. 

On my machine, DPDK calls `p0` and `pf0hpf` ports `2` and `3`:

##### Step 2: 

start wire on appropriate ports on the bluefield

```bash
ubuntu@localhost:~/proj/biwire$ sudo ./wire -l 0-2 -- 2 3
EAL: Detected CPU lcores: 8
EAL: Detected NUMA nodes: 1
EAL: Detected shared linkage of DPDK
EAL: Multi-process socket /var/run/dpdk/rte/mp_socket
EAL: Selected IOVA mode 'PA'
EAL: Probe PCI driver: mlx5_pci (15b3:a2d6) device: 0000:03:00.0 (socket -1)
EAL: Probe PCI driver: mlx5_pci (15b3:a2d6) device: 0000:03:00.1 (socket -1)
TELEMETRY: No legacy callbacks, legacy socket not created
Port 2 MAC: 08 c0 eb b2 3c f0
Port 3 MAC: 06 28 fd 42 a0 eb
Starting bidirectional wire between ports 2 and 3
Starting packet forwarding:
  IN:  Port 2
  OUT: Port 3
Starting packet forwarding:
  IN:  Port 3
  OUT: Port 2
 ```

You should see the total forwarded packet count increment whenever a new packet comes into either end. `ctrl-c` exits.


##### Step 3: 

Run some basic tests to show connectivity between host1 and host2, going through the bluefield. e.g.:

in window 1 on host1:
`sudo tcpdump -i $HOST1PORT -e -n`


in window 2 on host2:
`sudo python3 -c 'from scapy.all import *; sendp(Ether(dst="00:11:22:33:44:55")/IP()/Raw(load="Promisc test"), iface="$HOST2PORT", count=5)'`


After a moment, you should see a burst of 5 packets come through the wire app on the bluefield, then the packets arrive in the tcpdump. 


**Note:** the interface on host 1 (the host using the bluefield) has to be the interface corresponding to the representor port on the bluefield that you found in step 1. (it is the host side of, for example `pf0hpf`)

In my setup, that port is: `ens4f0np0`.

