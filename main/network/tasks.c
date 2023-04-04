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

	EventGroupHandle_t event_group = (EventGroupHandle_t) params;
	while(1) {
		int new_connection_index = server_accept_connections();
		if (server_get_clients_count() == 1) {
			xEventGroupSetBits(event_group, CLIENTS_AVAILABLE_BIT);
		}

		if(new_connection_index >= 0) {
			ESP_LOGI(tag, "Connection accepted: %s", server_get_client_address(new_connection_index));
		} else {
			ESP_LOGE(tag, "Failed to accept connection");
		}
	}
}

void task_send_heartbeats(void* params) {
	char* tag = "heartbeats";

	EventGroupHandle_t event_group = (EventGroupHandle_t) params;
	while(1) {
		xEventGroupWaitBits(event_group, CLIENTS_AVAILABLE_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

		int clients_count = server_get_clients_count();
		for(int client_index = clients_count - 1; client_index >= 0; --client_index) {
			if (!server_send_heartbeat(client_index)) {
				ESP_LOGI(tag, "Client %d disconnected", client_index);
				server_disconnect_client(client_index);
			}
		}

		if (server_get_clients_count() == 0) {
			xEventGroupClearBits(event_group, CLIENTS_AVAILABLE_BIT);
		}

		vTaskDelay(pdMS_TO_TICKS(HEARTBET_INTERVAL_MS));
	}
}
