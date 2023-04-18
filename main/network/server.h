#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>

#include "prelude.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define MAX_CONNECTIONS 10

typedef enum {
	REQUEST_VIDEO_INTEREST = 0xAADCFBED,
} request_type_t;

typedef struct {
	int client_index;
	request_type_t request_type;
	void* request_body;
	size_t request_body_length;
} request_t;

typedef struct client_connection client_connection_t;

status_t server_start();

int server_accept_connections(SemaphoreHandle_t semaphore);
void server_handle_requests(request_t* requests, size_t* num_requests, SemaphoreHandle_t semaphore);

char* server_get_client_address(int client_index, SemaphoreHandle_t semaphore);

int server_get_clients_count();
int server_get_clients_count_sync(SemaphoreHandle_t semaphore);

uint16_t server_get_video_interest();
uint16_t server_get_video_interest_sync(SemaphoreHandle_t semaphore);
uint16_t server_update_client_video_interest(int client_index, bool is_interested);

bool server_send_heartbeat(int client_index, SemaphoreHandle_t semaphore);
void server_send_broadcast();
bool server_send_image_data(uint8_t* framebuffer, size_t buffer_length, uint16_t sequence_number, uint32_t timestamp);

void server_disconnect_client(int client_index, SemaphoreHandle_t semaphore);

#endif
