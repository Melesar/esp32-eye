#ifndef NETWORK_TASKS_H
#define NETWORK_TASKS_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

typedef enum {
	CLIENTS_AVAILABLE_BIT = 1,
	CLIENT_CONNECTED_BIT = 2,
} network_bits_t;

typedef struct {
	SemaphoreHandle_t mutex;
	EventGroupHandle_t event_group;
	QueueHandle_t image_produce_queue;
	QueueHandle_t image_recycle_queue;
} task_sync_t;

void task_handle_network_messages(void* params);
void task_send_heartbeats(void* params);
void task_send_broadcasts(void* params);
void task_send_camera_image(void* params);

void task_capture_camera_image(void* params);
void task_recycle_camera_image(void* params);

#endif
