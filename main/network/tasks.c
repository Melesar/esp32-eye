#include "tasks.h"
#include "prelude.h"
#include "server.h"

#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#define TAG "server_tasks"
#define HEARTBET_INTERVAL_MS 1000

void task_handle_network_messages(void* params) {
	ESP_LOGI(TAG, "Starting handling network messages");

	EventGroupHandle_t event_group = (EventGroupHandle_t) params;
	while(1) {
		int new_connection_index = server_accept_connections();
		ESP_LOGI(TAG, "Index: %d", new_connection_index);

		if (new_connection_index == 1) {
			xEventGroupSetBits(event_group, CLIENTS_AVAILABLE_BIT);
		}

		if(new_connection_index >= 0) {
			ESP_LOGI(TAG, "Connection accepted: %s", server_get_client_address(new_connection_index));
		} else {
			ESP_LOGE(TAG, "Failed to accept connection");
		}
	}
}

void task_send_heartbeats(void* params) {
	EventGroupHandle_t event_group = (EventGroupHandle_t) params;
	while(1) {
		ESP_LOGI(TAG, "Waiting for heartbeats...");

		xEventGroupWaitBits(event_group, CLIENTS_AVAILABLE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
		ESP_LOGI(TAG, "Sending heartbeats");

		int clients_count = server_get_clients_count();
		for(int client_index = clients_count - 1; client_index >= 0; --client_index) {
			if (!server_send_heartbeat(client_index)) {
				ESP_LOGI(TAG, "Client %d disconnected", client_index);
				server_disconnect_client(client_index);
			}
		}

		if (server_get_clients_count() == 0) {
			xEventGroupClearBits(event_group, CLIENTS_AVAILABLE_BIT);
		}

		vTaskDelay(pdMS_TO_TICKS(HEARTBET_INTERVAL_MS));
	}
}
