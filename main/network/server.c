#include "server.h"
#include "esp_log.h"
#include "lwip/def.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <lwip/inet.h>

#define SERVER_PORT 3452
#define BROADCAST_PORT 45122

#define RTP_PORT 45120
#define RTP_JPEG_PAYLOAD 26

#define MAX_REQUEST_SIZE 32

#define TAG "server"

typedef enum {
	MESSAGE_BROADCAST = 0xAABB1234,
	MESSAGE_HELLO = 0xCABFEEFD,
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
	bool is_active;
	int control_socket;
	char address_string[20];
	struct sockaddr_in rtp_address;
};

typedef struct {
	char device_name[32];
} hello_message_t;

static int server_socket;
static int rtp_socket;
static int broadcast_socket;
static int num_active_connections = 0;
static uint32_t rtp_ssid;
static uint16_t video_interest_mask;
static uint8_t recv_buffer[MAX_REQUEST_SIZE * MAX_CONNECTIONS];
static client_connection_t connections[MAX_CONNECTIONS] = {0};

static bool is_active_client(int client_index) {
	return client_index >= 0 && client_index < MAX_CONNECTIONS && connections[client_index].is_active;
}

static void server_disconnect_client_no_sync(int client_index) {
	if (!is_active_client(client_index)) {
		return;
	}

	memset(&connections[client_index], 0, sizeof(client_connection_t));
	video_interest_mask &= ~(1 << client_index);
	num_active_connections -= 1;
	ESP_LOGI(TAG, "Client %d disconnected. Currently active connections: %d", client_index, num_active_connections);

}


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
	if (num_active_connections >= MAX_CONNECTIONS) {
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

	hello_message_t hello_message = {0};
	strncpy(hello_message.device_name, CONFIG_DEVICE_NAME, 32);
	
	uint32_t message_header = htonl(MESSAGE_HELLO);
	struct iovec iovs[2];
	iovs[0].iov_base = &message_header;
	iovs[0].iov_len = sizeof(message_header);
	iovs[1].iov_base = &hello_message;
	iovs[1].iov_len = sizeof(hello_message);
	
	struct msghdr message = {0};
	message.msg_iov = iovs;
	message.msg_iovlen = 2;
	
	sendmsg(client_socket, &message, 0);

	struct sockaddr_in rtp_address = incoming_address;
	rtp_address.sin_port = htons(RTP_PORT);

	xSemaphoreTake(semaphore, portMAX_DELAY);
	for(int i = 0; i < MAX_CONNECTIONS; ++i) {
		if (connections[i].is_active) {
			continue;
		}

		connections[i].is_active = true;
		connections[i].control_socket = client_socket;
		connections[i].rtp_address = rtp_address;
		strcpy(connections[i].address_string, inet_ntoa(incoming_address.sin_addr));
		num_active_connections += 1;
		ESP_LOGI(TAG, "New client %s accepted at index %d. Currently %d active connections",
				connections[i].address_string,
				i,
				num_active_connections);
		xSemaphoreGive(semaphore);
		return i;
	}

	xSemaphoreGive(semaphore);
	return -1;
}

void server_handle_requests(request_t* requests, size_t* num_requests, SemaphoreHandle_t semaphore) {
	struct pollfd fds[MAX_CONNECTIONS];
	xSemaphoreTake(semaphore, portMAX_DELAY);
	for (int i = 0; i < MAX_CONNECTIONS; ++i) {
		if (!connections[i].is_active) {
			fds[i].fd = -1;
		} else {
			fds[i].fd = connections[i].control_socket;
			fds[i].events = POLLIN;
		}
	}
	xSemaphoreGive(semaphore);

	poll(fds, MAX_CONNECTIONS, -1);

	int served_requests = 0;
	xSemaphoreTake(semaphore, portMAX_DELAY);
	for (int i = 0; i < MAX_CONNECTIONS; ++i) {
		if (!connections[i].is_active || !(fds[i].revents & POLLIN)) {
			continue;
		}

		uint8_t* recv_buffer_chunk = &recv_buffer[i * MAX_REQUEST_SIZE];
		ssize_t received_bytes = recv(fds[i].fd, recv_buffer_chunk, MAX_REQUEST_SIZE, 0);
		if (received_bytes < 0) {
			ESP_LOGE(TAG, "Failed to receive data from client %s", strerror(errno));
			server_disconnect_client_no_sync(i);
			continue;
		}

		if (received_bytes == 0) {
			server_disconnect_client_no_sync(i);
			continue;
		}

		ESP_LOGI(TAG, "Received %zu bytes from %s", received_bytes, connections[i].address_string);
		if (received_bytes < 4) {
			continue;
		}

		requests[served_requests].client_index = i;
		requests[served_requests].request_type = ntohl(*(uint32_t*)recv_buffer_chunk);
		requests[served_requests].request_body = &recv_buffer_chunk[sizeof(uint32_t)];
		requests[served_requests].request_body_length = received_bytes - sizeof(uint32_t);
		served_requests += 1;
	}
	xSemaphoreGive(semaphore);

	*num_requests = served_requests;
}

uint16_t server_update_client_video_interest(int client_index, bool is_interested) {
	if (!is_active_client(client_index)) {
		return 0;
	}

	if (is_interested) {
		video_interest_mask |= (1 << client_index);
	} else {
		video_interest_mask &= ~(1 << client_index);
	}

	return video_interest_mask;
}

uint16_t server_get_video_interest_sync(SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	uint16_t interest = server_get_video_interest();
	xSemaphoreGive(semaphore);

	return interest;
}

uint16_t server_get_video_interest() {
	return video_interest_mask;
}

char* server_get_client_address(int client_index, SemaphoreHandle_t semaphore) {
	xSemaphoreTake(semaphore, portMAX_DELAY);
	if (!is_active_client(client_index)) {
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
	return num_active_connections;
}

void server_send_broadcast() {
	struct sockaddr_in address = {0};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	address.sin_port = htons(BROADCAST_PORT);

	int message = htonl(MESSAGE_BROADCAST);
	sendto(broadcast_socket, &message, sizeof(message), 0, (struct sockaddr*)&address, sizeof(address));
}

static void get_rtp_header(rtp_header_t* header, uint16_t sequence_number, uint32_t timestamp, uint32_t payload_length) {
	header->version_with_flags = ((uint8_t)2 << 6);
	header->payload_type = RTP_JPEG_PAYLOAD;
	header->sequence_number = htons(sequence_number);
	header->timestamp = htonl(timestamp);
	header->ssid = htonl(rtp_ssid);
	header->payload_length = htonl(payload_length);
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

	for(size_t i = 0; i < MAX_CONNECTIONS; ++i) {
		if (!connections[i].is_active || !(video_interest_mask & (1 << i))) {
			continue;
		}
		
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
	server_disconnect_client_no_sync(client_index);
	xSemaphoreGive(semaphore);
}
