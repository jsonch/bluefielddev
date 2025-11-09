#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
// #include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_dev.h>
// #include <rte_flow.h>
#include <rte_mbuf.h>


#define RING_SIZE 1024
#define NUM_MBUFS 1024
#define MBUF_CACHE_SIZE 250
// Open a DPDK port
int port_init(uint16_t port) {
	struct rte_mempool *mbuf_pool;
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RING_SIZE;
	uint16_t nb_txd = RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	// allocate the mbuf pool
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (mbuf_pool == NULL) {
		int required_mem = (NUM_MBUFS * (2048 + sizeof(struct rte_mbuf))) * 1 + (MBUF_CACHE_SIZE * sizeof(struct rte_mbuf) * 1);
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool. the memory required was: %i\n", required_mem);
	}

	// set up the port configuration
	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	// get device info
	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	// if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
	// 	port_conf.txmode.offloads |=
	// 		RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}


#define MAX_PKT_BURST 32
// Simple packet counter
static void count_packets(uint16_t port_id) {
    struct rte_mbuf *bufs[MAX_PKT_BURST];
    uint64_t total_packets = 0;
    uint16_t nb_rx;
    
    printf("Starting packet counter on port %u\n", port_id);
    printf("Press Ctrl+C to stop\n\n");
    
    while (1) {
        // Receive packets
        nb_rx = rte_eth_rx_burst(port_id, 0, bufs, MAX_PKT_BURST);
        
        if (nb_rx > 0) {
            total_packets += nb_rx;
            
            // Free the mbufs
            for (uint16_t i = 0; i < nb_rx; i++) {
                rte_pktmbuf_free(bufs[i]);
            }
            if (nb_rx > 0) {
                printf("Total packets received: %lu\n", total_packets);
            }
        }
    }
}


int main(int argc, char **argv)
{
    // main DPDK init
	rte_eal_init(argc, argv); 
    int log_level = rte_log_get_global_level();

    // Initialize the port
    uint16_t selected_port_id = 2;
    port_init(selected_port_id);

    count_packets(selected_port_id);
	return 0;
}
