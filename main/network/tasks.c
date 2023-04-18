#include "tasks.h"
#include "prelude.h"
#include "server.h"

#include <esp_camera.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#define BROADCAST_INTERVAL_MS 3000

#define TARGET_FRAMERATE 30
#define CAPTURE_INTERVAL_MS 1000 / TARGET_FRAMERATE

static void update_video_interest(int client_index, bool is_interested, task_sync_t* task_sync) {
	xSemaphoreTake(task_sync->mutex, portMAX_DELAY);
	uint16_t previous_interest = server_get_video_interest();
	uint16_t new_interest = server_update_client_video_interest(client_index, is_interested);
	xSemaphoreGive(task_sync->mutex);

	if (!previous_interest && new_interest) {
		xEventGroupSetBits(task_sync->event_group, CLIENTS_INTERESTED_IN_VIDEO_BIT);
	} else if (previous_interest && !new_interest) {
		xEventGroupClearBits(task_sync->event_group, CLIENTS_INTERESTED_IN_VIDEO_BIT);
	}

	ESP_LOGI("requests", "Received message video interest update from %d: %c", client_index, (uint8_t)is_interested);
}

void task_accept_new_clients(void* params) {
    char* tag = "network_messages";
    ESP_LOGI(tag, "Starting handling network messages");

    task_sync_t* task_sync = (task_sync_t*)params;
    while (1) {
        int new_connection_index = server_accept_connections(task_sync->mutex);
        if (new_connection_index < 0) {
            ESP_LOGE(tag, "Failed to accept connection");
			continue;
        }

        if (server_get_clients_count(task_sync->mutex) == 1) {
            xEventGroupSetBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT);
        }

        xEventGroupSetBits(task_sync->event_group, CLIENT_CONNECTED_BIT);
    }
}

void task_handle_requests(void* params) {
	task_sync_t* task_sync = (task_sync_t*) params;

	size_t served_requests;
	request_t requests_buffer[MAX_CONNECTIONS];
	while(1) {
		xEventGroupWaitBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

		server_handle_requests(requests_buffer, &served_requests, task_sync->mutex);

		for(int i = 0; i < served_requests; ++i) {
			request_t request = requests_buffer[i];
			switch(request.request_type) {
				case REQUEST_VIDEO_INTEREST:
					update_video_interest(request.client_index, *(bool*)request.request_body, task_sync);
					break;
			}
		}

		if (!server_get_clients_count_sync(task_sync->mutex)) {
			xEventGroupClearBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT);
		}

		if (!server_get_video_interest_sync(task_sync->mutex)) {
			xEventGroupClearBits(task_sync->event_group, CLIENTS_INTERESTED_IN_VIDEO_BIT);
		}
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
        xEventGroupWaitBits(task_sync->event_group, CLIENTS_AVAILABLE_BIT | CLIENTS_INTERESTED_IN_VIDEO_BIT,
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

	uint16_t sequence_number = (uint16_t)(esp_random() % 100);
	uint32_t timestamp = esp_random() % 1000;
    while (1) {
		camera_fb_t* fb;
		xQueueReceive(task_sync->image_produce_queue, &fb, portMAX_DELAY);

		uint64_t start = esp_timer_get_time();
		xSemaphoreTake(task_sync->mutex, portMAX_DELAY);
		if (!server_send_image_data(fb->buf, fb->len, sequence_number++, timestamp)) {
			ESP_LOGE("image_send", "Failed to send image to clients");
		}
		xSemaphoreGive(task_sync->mutex);
		uint64_t end = esp_timer_get_time();

		uint32_t millisecods_elapsed = (end - start) / 1000;
		ESP_LOGI("image_send", "Image sent in %zu ms", millisecods_elapsed);
		timestamp += millisecods_elapsed;

		xQueueSendToBack(task_sync->image_recycle_queue, &fb, portMAX_DELAY);
    }
}
