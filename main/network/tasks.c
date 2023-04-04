#include "tasks.h"
#include "prelude.h"
#include "server.h"

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#define HEARTBET_INTERVAL_MS 1000

void task_handle_network_messages(void* params) {
	char* tag = "network_messages";
	ESP_LOGI(tag, "Starting handling network messages");

	task_sync_t* task_sync = (task_sync_t*) params;
	while(1) {
		int new_connection_index = server_accept_connections(task_sync->mutex);
		if (server_get_clients_count(task_sync->mutex) == 1) {
			xEventGroupSetBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT);
		}

		if(new_connection_index >= 0) {
			char* address_string = server_get_client_address(new_connection_index, task_sync->mutex);
			ESP_LOGI(tag, "Connection accepted: %s", address_string);
		} else {
			ESP_LOGE(tag, "Failed to accept connection");
		}
	}
}

void task_send_heartbeats(void* params) {
	char* tag = "heartbeats";

	task_sync_t* task_sync = (task_sync_t*) params;
	while(1) {
		xEventGroupWaitBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

		int clients_count = server_get_clients_count(task_sync->mutex);
		for(int client_index = clients_count - 1; client_index >= 0; --client_index) {
			if (!server_send_heartbeat(client_index, task_sync->mutex)) {
				ESP_LOGI(tag, "Client %d disconnected", client_index);
				server_disconnect_client(client_index, task_sync->mutex);
			}
		}

		if (server_get_clients_count(task_sync->mutex) == 0) {
			xEventGroupClearBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT);
		}

		vTaskDelay(pdMS_TO_TICKS(HEARTBET_INTERVAL_MS));
	}
}
