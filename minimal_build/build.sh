gcc simple_rte_flow_rule.c -o simple_rte_flow_rule \
	-I/opt/mellanox/dpdk/include/aarch64-linux-gnu/dpdk \
	-I/opt/mellanox/dpdk/include/dpdk \
	-I/opt/mellanox/doca/include/ \
	-L/opt/mellanox/dpdk/lib/aarch64-linux-gnu \
	-lrte_eal -lrte_mempool -lrte_ring -lrte_ethdev -lrte_mbuf \
	-lstdc++ -libverbs -lmlx5