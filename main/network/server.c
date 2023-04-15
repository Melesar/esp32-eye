#include "server.h"
#include "esp_log.h"
#include "lwip/def.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <lwip/inet.h>

#define SERVER_PORT 3452
#define BROADCAST_PORT 45122
#define MAX_CONNECTIONS 10

#define RTP_PORT 45120
#define RTP_JPEG_PAYLOAD 26

#define TAG "server"

typedef enum {
	MESSAGE_HEARTBEAT = 0xDCACBDFA,
	MESSAGE_BROADCAST = 0xAABB1234,
} message_header_t;

typedef struct {
	uint8_t version_with_flags;
	uint8_t payload_type;
	uint16_t sequence_number;
	uint32_t timestamp;
	uint32_t ssid;
	uint32_t payload_length;
} rtp_header_t;

struct client_connection{
	int control_socket;
	char address_string[20];
	struct sockaddr_in rtp_address;
};

static int server_socket;
static int rtp_socket;
static int broadcast_socket;
static int next_connection_index = 0;
static uint32_t rtp_ssid;
static client_connection_t connections[MAX_CONNECTIONS] = {0};

status_t server_start() {
	rtp_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (!rtp_socket) {
        ESP_LOGE(TAG, "RTP socket creation failed");
        return ST_SERVER_INITIALIZATION_FAILED;
	}

	rtp_ssid = esp_random();

	broadcast_socket = socket(AF_INET, SOCK_DGRAM, 0);
	if (broadcast_socket < 0) {
		ESP_LOGE(TAG, "Failed to create broadcast socket");
		return ST_SERVER_INITIALIZATION_FAILED;
	}

	int broadcast = 1;
	if (setsockopt(broadcast_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
		ESP_LOGE(TAG, "Failed to obtain broadcasting permissions");
		return ST_SERVER_INITIALIZATION_FAILED;
	}

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (!server_socket) {
        ESP_LOGE(TAG, "Server socket creation failed");
        return ST_SERVER_INITIALIZATION_FAILED;
    }

    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, NULL, 0);

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    if (bind(server_socket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        ESP_LOGE(TAG, "Server socket bind failed");
        return ST_SERVER_INITIALIZATION_FAILED;
    }

    if (listen(server_socket, MAX_CONNECTIONS) < 0) {
        ESP_LOGE(TAG, "Server socket listening failed");
        return ST_SERVER_INITIALIZATION_FAILED;
    }

    ESP_LOGI(TAG, "Server started listening on port %d", SERVER_PORT);

    return ST_SUCCESS;
}

int server_accept_connections(SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	if (next_connection_index >= MAX_CONNECTIONS) {
		xSemaphoreGive(semaphore);
		return -1;
	}

	xSemaphoreGive(semaphore);
	struct sockaddr_in incoming_address;
	int address_length = sizeof(incoming_address);
	int client_socket = accept(server_socket, (struct sockaddr*) &incoming_address, (socklen_t*) &address_length);
	
	if (client_socket < 0) {
		return -1;
	}

	struct sockaddr_in rtp_address = incoming_address;
	rtp_address.sin_port = htons(RTP_PORT);

	client_connection_t new_connection = {
		.control_socket = client_socket,
		.rtp_address =  rtp_address
	};
	strcpy(new_connection.address_string, inet_ntoa(incoming_address.sin_addr));

	xSemaphoreTake(semaphore, portMAX_DELAY);
	connections[next_connection_index++] = new_connection;
	int new_connection_index = next_connection_index - 1;
	xSemaphoreGive(semaphore);

	return new_connection_index;
}

char* server_get_client_address(int client_index, SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	if (client_index < 0 || client_index >= next_connection_index) {
		xSemaphoreGive(semaphore);
		return "";
	}

	char* address_string = connections[client_index].address_string;
	xSemaphoreGive(semaphore);

	return address_string;
}

int server_get_clients_count_sync(SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	int count = server_get_clients_count();
	xSemaphoreGive(semaphore);

	return count;
}

int server_get_clients_count() {
	return next_connection_index;
}

bool server_send_heartbeat(int client_index, SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	if (client_index < 0 || client_index >= next_connection_index) {
		xSemaphoreGive(semaphore);
		return false;
	}

	client_connection_t client = connections[client_index];

	uint32_t heartbeat = MESSAGE_HEARTBEAT;
	int sent = send(client.control_socket, &heartbeat, sizeof(heartbeat), MSG_DONTWAIT);
	bool is_success = sent >= 0 || errno == EAGAIN || errno == EWOULDBLOCK;
	xSemaphoreGive(semaphore); 

	return is_success;
}

void server_send_broadcast() {
	struct sockaddr_in address = {0};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	address.sin_port = htons(BROADCAST_PORT);

	int message = MESSAGE_BROADCAST;
	sendto(broadcast_socket, &message, sizeof(message), 0, (struct sockaddr*)&address, sizeof(address));
}

static void get_rtp_header(rtp_header_t* header, uint16_t sequence_number, uint32_t timestamp, uint32_t payload_length) {
	header->version_with_flags = ((uint8_t)2 << 6);
	header->payload_type = RTP_JPEG_PAYLOAD;
	header->sequence_number = sequence_number;
	header->timestamp = timestamp;
	header->ssid = rtp_ssid;
	header->payload_length = payload_length;
}


bool server_send_image_data(uint8_t* framebuffer, size_t buffer_length, uint16_t sequence_number, uint32_t timestamp) {
	rtp_header_t rtp_header;
	get_rtp_header(&rtp_header, sequence_number, timestamp, buffer_length);

	struct msghdr message = {0};
	struct iovec iovs[2];
	iovs[0].iov_base = &rtp_header;
	iovs[0].iov_len = sizeof(rtp_header);
	iovs[1].iov_base = framebuffer;
	iovs[1].iov_len = buffer_length;
	message.msg_iov = iovs;
	message.msg_iovlen = 2;

	for(size_t i = 0; i < next_connection_index; ++i) {
		struct sockaddr_in client_address = connections[i].rtp_address;
		message.msg_name = &client_address;
		message.msg_namelen = sizeof(client_address);

		if (sendmsg(rtp_socket, &message, 0) < 0) {
			ESP_LOGE("image_send", "Failed to send image data to client %s", inet_ntoa(client_address.sin_addr));
		}
	}

	return true;
}

void server_disconnect_client(int client_index, SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	connections[client_index] = connections[--next_connection_index];
	xSemaphoreGive(semaphore);
}

void server_shutdown() {
}
