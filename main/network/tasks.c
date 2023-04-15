#include "tasks.h"
#include "prelude.h"
#include "server.h"

#include <esp_camera.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#define HEARTBET_INTERVAL_MS 1000
#define BROADCAST_INTERVAL_MS 3000

#define TARGET_FRAMERATE 30
#define CAPTURE_INTERVAL_MS 1000 / TARGET_FRAMERATE

void task_handle_network_messages(void* params) {
    char* tag = "network_messages";
    ESP_LOGI(tag, "Starting handling network messages");

    task_sync_t* task_sync = (task_sync_t*)params;
    while (1) {
        int new_connection_index = server_accept_connections(task_sync->mutex);
        if (server_get_clients_count(task_sync->mutex) == 1) {
            xEventGroupSetBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT);
        }

        xEventGroupSetBits(task_sync->event_group, CLIENT_CONNECTED_BIT);

        if (new_connection_index >= 0) {
            char* address_string = server_get_client_address(
                new_connection_index, task_sync->mutex);
            ESP_LOGI(tag, "Connection accepted: %s", address_string);
        } else {
            ESP_LOGE(tag, "Failed to accept connection");
        }
    }
}

void task_send_heartbeats(void* params) {
    char* tag = "heartbeats";

    task_sync_t* task_sync = (task_sync_t*)params;
    while (1) {
        xEventGroupWaitBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT,
                            pdFALSE, pdFALSE, portMAX_DELAY);

        int clients_count = server_get_clients_count(task_sync->mutex);
        for (int client_index = clients_count - 1; client_index >= 0;
             --client_index) {
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

void task_send_broadcasts(void* params) {
	while(1) {
		server_send_broadcast();
		vTaskDelay(pdMS_TO_TICKS(BROADCAST_INTERVAL_MS));
	}
}

void task_capture_camera_image(void* params) {
	task_sync_t* task_sync = (task_sync_t*) params;

	while(1) {
        xEventGroupWaitBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT,
                            pdFALSE, pdTRUE, portMAX_DELAY);

		uint64_t time_to_wait_ms = CAPTURE_INTERVAL_MS;
		if (uxQueueMessagesWaiting(task_sync->image_produce_queue) == 0) {
			uint64_t start = esp_timer_get_time();
			camera_fb_t* fb = esp_camera_fb_get();
			if (fb) {
				xQueueSendToBack(task_sync->image_produce_queue, &fb, portMAX_DELAY);
				uint64_t end = esp_timer_get_time();

				uint64_t elapsed_ms = (end - start) / 1000;
				ESP_LOGI("image_capture", "Captured frame in %llu ms (%zu bytes)", elapsed_ms, fb->len);
				time_to_wait_ms = elapsed_ms <= time_to_wait_ms ? time_to_wait_ms - elapsed_ms : 0;
			} else {
				ESP_LOGE("image_capture", "Failed to capture frame");
			}
			
		} else {
			ESP_LOGI("image_capture", "Skipping a frame");
		}

		vTaskDelay(pdMS_TO_TICKS(time_to_wait_ms));
	}
}

void task_recycle_camera_image(void* params) {
	task_sync_t* task_sync = (task_sync_t*) params;

	while(1) {
		camera_fb_t* fb;
		xQueueReceive(task_sync->image_recycle_queue, &fb, portMAX_DELAY);
		esp_camera_fb_return(fb);
	}

}

void task_send_camera_image(void* params) {
    task_sync_t* task_sync = (task_sync_t*)params;
    while (1) {
		camera_fb_t* fb;
		xQueueReceive(task_sync->image_produce_queue, &fb, portMAX_DELAY);

		uint64_t start = esp_timer_get_time();
		xSemaphoreTake(task_sync->mutex, portMAX_DELAY);
		if (!server_send_image_data(fb->buf, fb->len, 0, 0)) {
			ESP_LOGE("image_send", "Failed to send image to clients");
		}
		xSemaphoreGive(task_sync->mutex);
		uint64_t end = esp_timer_get_time();

		ESP_LOGI("image_send", "Image sent in %llu ms", (end - start) / 1000);


		xQueueSendToBack(task_sync->image_recycle_queue, &fb, portMAX_DELAY);
    }
}
