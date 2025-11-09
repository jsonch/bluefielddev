/opt/mellanox/doca/tools/dpacc \
	kernel.c \
	-o dpa_program.a \
	-hostcc=gcc \
	-hostcc-options="-Wno-deprecated-declarations -Werror -Wall -Wextra -W" \
	--devicecc-options="-D__linux__ -Wno-deprecated-declarations -Werror -Wall -Wextra -W" \
	--app-name="dpa_hello_world_app" \
	-ldpa \
	-I/opt/mellanox/doca/include/

gcc hello_world.c -o hello_world \
	dpa_program.a \
	-I/opt/mellanox/dpdk/include/aarch64-linux-gnu/dpdk \
	-I/opt/mellanox/dpdk/include/dpdk \
	-I/opt/mellanox/doca/include/ \
	-DDOCA_ALLOW_EXPERIMENTAL_API \
	-L/opt/mellanox/dpdk/lib/aarch64-linux-gnu \
	-lrte_eal -lrte_mempool -lrte_ring -lrte_ethdev -lrte_mbuf \
	-L/opt/mellanox/doca/lib/aarch64-linux-gnu/ -ldoca_dpa -ldoca_common -ldoca_flow \
	-L/opt/mellanox/flexio/lib/ -lflexio \
	-lstdc++ -libverbs -lmlx5