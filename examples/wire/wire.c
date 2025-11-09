#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <rte_ethdev.h>
#include <rte_dev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <rte_launch.h>
#include <rte_lcore.h>

#include <ifaddrs.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <signal.h>


/***  Helper functions to get info about available DPDK ports ***/
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


#define RING_SIZE 1024
#define NUM_MBUFS 1024
#define MBUF_CACHE_SIZE 250
// Open a DPDK port and initialized an mbuf pool for rx packets
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
    char pool_name[32];
    snprintf(pool_name, sizeof(pool_name), "MBUF_POOL_%u", port);    
    mbuf_pool = rte_pktmbuf_pool_create(pool_name, NUM_MBUFS,
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
    //  port_conf.txmode.offloads |=
    //      RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

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
// Wire packets from in_port to out_port, pulling up to MAX_PKT_BURST at a time
// from the in_port and sending them to the out_port.
static void wire_ports(uint16_t in_port, uint16_t out_port) {
    struct rte_mbuf *bufs[MAX_PKT_BURST];
    uint64_t total_forwarded = 0;
    uint64_t total_dropped = 0;
    uint16_t nb_rx, nb_tx;
    
    printf("Starting packet forwarding:\n");
    printf("  IN:  Port %u\n", in_port);
    printf("  OUT: Port %u\n", out_port);
    
    while (1) {
        // Receive burst of packets from in_port
        nb_rx = rte_eth_rx_burst(in_port, 0, bufs, MAX_PKT_BURST);
        
        if (nb_rx > 0) {
            // Send burst to out_port
            nb_tx = rte_eth_tx_burst(out_port, 0, bufs, nb_rx);
            
            total_forwarded += nb_tx;
            // print total forwarded packets if nb_tx > 0
            if (nb_tx > 0) {
                printf("Total forwarded packets: %lu\n", total_forwarded);
            }
            // Free any packets that weren't sent
            if (nb_tx < nb_rx) {
                total_dropped += (nb_rx - nb_tx);
                for (uint16_t i = nb_tx; i < nb_rx; i++) {
                    rte_pktmbuf_free(bufs[i]);
                }
            }
        }
    }
}

// Helpers to launch two wire threads on separate cores

// Thread argument structure
struct wire_thread_args {
    uint16_t in_port;
    uint16_t out_port;
};

// Lcore function wrapper (must return int and take void*)
static int wire_lcore(void *arg) {
    struct wire_thread_args *args = (struct wire_thread_args *)arg;
    wire_ports(args->in_port, args->out_port);
    return 0;
}


uint16_t PORT_A = 0;
uint16_t PORT_B = 0;

// Signal handler
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n\nSignal %d received, preparing to exit...\n", signum);
        exit(0);
    }
}


int main(int argc, char **argv)
{
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // main DPDK init - this consumes EAL arguments and returns new argc
    int ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
    // After rte_eal_init, argc/argv are adjusted to skip EAL args
    argc -= ret;
    argv += ret;

    // Parse application arguments -- the two ports to forward between
    uint16_t network_port;
    uint16_t host_port;
    if (argc == 3) {
        network_port = atoi(argv[1]);
        host_port = atoi(argv[2]);
    } else {
        list_ports();
        printf("Usage: %s [EAL options] -- <network_port> <host_port>\n", argv[0]);        
        printf("Example: sudo %s -l 0-2 -- 2 3\n", argv[0]);
        rte_exit(EXIT_FAILURE, "Error: exactly 2 port arguments required\n");
    }
     
    
    // Initialize the port
    port_init(network_port);
    port_init(host_port);
    PORT_A = network_port;
    PORT_B = host_port;

    // Create thread arguments
    struct wire_thread_args args1 = {.in_port = network_port, .out_port = host_port};
    struct wire_thread_args args2 = {.in_port = host_port, .out_port = network_port};

    printf("Starting bidirectional wire between ports %u and %u\n", network_port, host_port);
    
    // Run on first two cores that are not the main lcore (current core)
    // note: it is okay to also run a thread on the current core, after
    // all the setup is finished. For that, you don't use rte_eal_remote_launch,
    // but just call the function directly after launching other threads.
    unsigned launched_thread_ct = 0;
    unsigned lcore_id;
    unsigned lcore1 = 0, lcore2 = 0;

    // Find two available lcores (skip main lcore)
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (launched_thread_ct == 0) {
            lcore1 = lcore_id;
            launched_thread_ct++;
        } else if (launched_thread_ct == 1) {
            lcore2 = lcore_id;
            launched_thread_ct++;
            break;
        }
    }
    if (launched_thread_ct < 2) {
        rte_exit(EXIT_FAILURE, "Need at least 2 worker lcores. Run with -l 0-2\n");
    }
    // Launch on two different lcores
    rte_eal_remote_launch(wire_lcore, &args1, lcore1);
    rte_eal_remote_launch(wire_lcore, &args2, lcore2);
    // Wait for lcores to finish (this will never happen, they run forever in this program)
    rte_eal_mp_wait_lcore();

    return 0;
}
