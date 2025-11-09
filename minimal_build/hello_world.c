#include <stdio.h>
#include <unistd.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_sync_event.h>
#include <doca_dpa.h>
#include <doca_flow.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_dev.h>
#include <rte_flow.h> // definitions are found in the librte_ethdev library
#include <rte_mbuf.h>

// target mac: A0:88:C2:AB:7E:A2 -- p0, should be port # 2 on blue2
struct rte_ether_addr target_mac = { .addr_bytes = {0xA0, 0x88, 0xC2, 0xAB, 0x7E, 0xA2} }; 

// DPDK port init
// To configure the steering engine, we need to open a DPDK port. 
// To open a DPDK port, we need to set up some queues and buffers, 
// (even if we aren't going to use them)
#define RING_SIZE 1024
#define NUM_MBUFS 1024
#define MBUF_CACHE_SIZE 250

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

uint16_t dpdk_init(int argc, char **argv) {
	// hard code port for now
    uint16_t port_id = 2;

	rte_eal_init(argc, argv);
    int log_level = rte_log_get_global_level();
    printf("Current log level: %d\n", log_level);
	int res = port_init(port_id);
	if (res != 0) {
		printf("Error initializing port %u\n", port_id);
		return -1;
	}
	return port_id;
}

int add_test_flow_rule(uint16_t dpdk_port_id) {
    // Define the flow rule attributes
    struct rte_flow_attr attr = {
        .ingress = 1,
    };

    // Define the pattern to match destination MAC address
    struct rte_flow_item_eth eth_spec = {
        .dst = target_mac,
    };
    struct rte_flow_item_eth eth_mask = {
        .dst = {.addr_bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}},
    };
    struct rte_flow_item pattern[] = {
        {
            .type = RTE_FLOW_ITEM_TYPE_ETH,
            .spec = &eth_spec,
            .mask = &eth_mask,
        },
        {
            .type = RTE_FLOW_ITEM_TYPE_END,
        },
    };

    // Define the action to drop the packet
    struct rte_flow_action action_drop = {
        .type = RTE_FLOW_ACTION_TYPE_DROP,
    };
    struct rte_flow_action actions[] = {
        action_drop,
        {
            .type = RTE_FLOW_ACTION_TYPE_END,
        },
    };

    // Validate and create the flow rule
    struct rte_flow_error error;
    struct rte_flow *flow = rte_flow_create(dpdk_port_id, &attr, pattern, actions, &error);
    if (!flow) {
        printf("Flow creation failed: %s\n", error.message);
        return -1;
    }

    printf("Flow rule created successfully\n");
	return 0;
}


// example of using rte_flow to configure a flow in the eswitch of the connectx device
int dpdk_rte_flow_test(int argc, char **argv) {
	uint16_t dpdk_port_id = dpdk_init(argc, argv);
	printf("Port ID: %u\n", dpdk_port_id);
	add_test_flow_rule(dpdk_port_id);
	return 0;

}


int main(int argc, char **argv)
{

	dpdk_rte_flow_test(argc, argv);

	// run_doca_dpa_kernel();
	return 0;
}
