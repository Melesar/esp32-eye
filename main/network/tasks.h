typedef enum {
	CLIENTS_AVAILABLE_BIT = 1,
} network_bits_t;

void task_handle_network_messages(void* params);
void task_send_heartbeats(void* params);
