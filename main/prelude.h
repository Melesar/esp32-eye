#ifndef PRELUDE_H
#define PRELUDE_H

#include "esp_err.h"

typedef int status_t;

#define ST_SUCCESS 0
#define ST_WIFI_INITIALIZATION_FAILED 1
#define ST_CAMERA_INITIALIZATION_FAILED 2
#define ST_SERVER_INITIALIZATION_FAILED 3

typedef enum {
	PRIORITY_LOW = 5,
	PRIORITY_NORMAL = 50,
	PRIORITY_HIGH = 250,
	PRIORITY_URGENT = 1000,
} priority_t;

const char* get_error_name(esp_err_t errorCode);

#endif
