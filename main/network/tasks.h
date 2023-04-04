#ifndef NETWORK_TASKS_H
#define NETWORK_TASKS_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

typedef enum {
	CLIENTS_AVAILABLE_BIT = 1,
} network_bits_t;

typedef struct {
	SemaphoreHandle_t mutex;
	EventGroupHandle_t event_group;
} task_sync_t;

void task_handle_network_messages(void* params);
void task_send_heartbeats(void* params);

#endif
