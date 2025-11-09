#include "doca_dpa_dev.h"
#include "doca_dpa_dev_sync_event.h"
 
__dpa_global__ void hello_world_thread_kernel(uint64_t arg)
{
	DOCA_DPA_DEV_LOG_INFO("Hello World From DPA Thread!\n");
	doca_dpa_dev_sync_event_update_set(arg, 1);
	doca_dpa_dev_thread_finish();
}
 
__dpa_rpc__ uint64_t hello_world_thread_notify_rpc(doca_dpa_dev_notification_completion_t comp_handle)
{
	DOCA_DPA_DEV_LOG_INFO("Notifying DPA Thread From RPC\n");
	doca_dpa_dev_thread_notify(comp_handle);
	return 0;
}