# bluefielddev
Scripts and tools for bluefield dev


- `./examples/wire` : A DPDK program that is a bidirectional wire between two ports. It reads packets from first port, sends them to second port, and vice versa. Each wire is on its own thread. readme includes a simple demo of using wire to make the bluefield ARM processing packets between host and network.

- `./examples/rte_rule`: simple example of how to install a flow rule into the eswitch with dpdk.

- `./examples/generator`: simple example of how to craft your own packets and send them out of an interface in dpdk.