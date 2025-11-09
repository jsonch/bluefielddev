#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <rte_bus_pci.h>  // for RTE_DEV_TO_PCI
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_dev.h>
#include <rte_flow.h>
#include <rte_mbuf.h>


// DPDK port init
// To configure the steering engine, we need to open a DPDK port. 
// To open a DPDK port, we need to set up some queues and buffers, 
// (even if we aren't going to use them)
#define RING_SIZE 1024
#define NUM_MBUFS 1024
#define MBUF_CACHE_SIZE 250


// Open a DPDK port, allocate it with a mbuf ring of the given size
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

// target mac: A0:88:C2:AB:7E:A2 -- p0, should be port # 2 on blue2
struct rte_ether_addr target_mac = { .addr_bytes = {0xA0, 0x88, 0xC2, 0xAB, 0x7E, 0xA2} }; 

// Add a rule to port dpdk_port_id to drop packets with dst mac == target_mac
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



// Helper function to get Linux interface name by MAC address
int get_linux_ifname_by_mac(struct rte_ether_addr *mac, char *ifname, size_t ifname_len) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) == -1) {
        return -1;
    }
    // Iterate through all interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        // Check if this is a packet socket (has MAC address)
        if (ifa->ifa_addr->sa_family == AF_PACKET) {
            struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;

            // Compare MAC addresses
            if (s->sll_halen == RTE_ETHER_ADDR_LEN &&
                memcmp(s->sll_addr, mac->addr_bytes, RTE_ETHER_ADDR_LEN) == 0) {
                snprintf(ifname, ifname_len, "%s", ifa->ifa_name);
                found = 1;
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
    return found ? 0 : -1;
}

void list_ports(void) {
    uint16_t port_id;
    uint16_t nb_ports;
    struct rte_eth_dev_info dev_info;
    struct rte_ether_addr addr;
    char name[RTE_ETH_NAME_MAX_LEN];
    char linux_ifname[IFNAMSIZ];
    int retval;

    nb_ports = rte_eth_dev_count_avail();
    printf("\n=== Available DPDK Ports ===\n");
    printf("Total ports available: %u\n\n", nb_ports);

    if (nb_ports == 0) {
        printf("No DPDK ports found!\n");
        return;
    }

    RTE_ETH_FOREACH_DEV(port_id) {
        // Get MAC address first
        retval = rte_eth_macaddr_get(port_id, &addr);
        if (retval != 0) {
            continue; // Skip if we can't get MAC
        }

        // Try to find Linux interface by MAC address
        if (get_linux_ifname_by_mac(&addr, linux_ifname, sizeof(linux_ifname)) != 0) {
            continue; // Skip ports without Linux interfaces
        }

        printf("Port %u:\n", port_id);

        // Get device name
        retval = rte_eth_dev_get_name_by_port(port_id, name);
        if (retval == 0) {
            printf("  DPDK Name: %s\n", name);

            // Check if this is a representor port
            if (strstr(name, "_representor_")) {
                printf("  Type: Representor Port\n");
            }
        }

        printf("  Linux Interface: %s\n", linux_ifname);

        printf("  MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
               addr.addr_bytes[0], addr.addr_bytes[1],
               addr.addr_bytes[2], addr.addr_bytes[3],
               addr.addr_bytes[4], addr.addr_bytes[5]);

        // Get device info
        retval = rte_eth_dev_info_get(port_id, &dev_info);
        if (retval != 0) {
            printf("  Error getting device info: %s\n", strerror(-retval));
            continue;
        }

        // Print driver name
        if (dev_info.driver_name) {
            printf("  Driver: %s\n", dev_info.driver_name);
        }

        // Print device name if available
        if (dev_info.device && rte_dev_name(dev_info.device)) {
            printf("  Device: %s\n", rte_dev_name(dev_info.device));
        }

        // Print capabilities
        printf("  Max RX queues: %u\n", dev_info.max_rx_queues);
        printf("  Max TX queues: %u\n", dev_info.max_tx_queues);

        // Print link status
        struct rte_eth_link link;
        retval = rte_eth_link_get_nowait(port_id, &link);
        if (retval == 0) {
            printf("  Link Status: %s\n", 
                   link.link_status ? "UP" : "DOWN");
            if (link.link_status) {
                printf("  Link Speed: %u Mbps\n", link.link_speed);
                printf("  Link Duplex: %s\n",
                       link.link_duplex == RTE_ETH_LINK_FULL_DUPLEX ? "Full" : "Half");
            }
        }

        printf("\n");
    }
    printf("============================\n\n");
}


int main(int argc, char **argv)
{
	rte_eal_init(argc, argv);
    int log_level = rte_log_get_global_level();
    printf("Current log level: %d\n", log_level);

	list_ports();    
	uint16_t selected_port_id = 2;
    port_init(selected_port_id);
	add_test_flow_rule(selected_port_id);
    printf("Flow rule is active. Press Ctrl+C to exit.\n");
    while (1) {
        sleep(1);
    }	
	return 0;
}
