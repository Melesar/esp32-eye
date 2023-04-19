# Simple camera server for ESP32-CAM development board

This program can stream the images from the onboard camera to multiple connected clients within the local network.

## Configuration

Before flashing this program onto an ESP32-CAM board, you need to configure it first. Make sure that you have the `ESP-IDF` [installed and configured](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html#manual-installation) for the current terminal session. 

In order for the board to connect to WiFi, you need to specify your WiFi credentials.

1. In the project directory run `idf.py menuconfig` 
2. Navigate to the `Project configuration menu`
3. Enter your WiFI SSID and the password
4. Press `S` to save.

There is also an option to change the `Device name`. This defines how the server will introduce itself to the clients, in case you want to have multiple of these in your home network.

## Communicating with the server

- The server will constantly send broadcasts to the port `45122` with the payload of `0xAABB1234` value. This will allow your client app to dicover its IP address.
- Once your client have the address, it should establish a TCP connection to the port `3452`
- As soon as the connection established, the server will send the "Hello message" to the client, which is defined as following:

| Data           | Value                                | Size     |
|:---------------|:------------------------------------:|:--------:|
| Message header | 0xCABFEEFD                           | 4 bytes  |
| Device name    | Value from the project configuration | 32 bytes |

> Note that the server will send multibyte integer values in the _network byte order_, which is Big Endian.
> Your client will most likely need to convert it to Little Endian to parse those values correctly

- The client will not receive any image data until it declares that it wants to. For that it needs to send the message via the established connection as following:

| Data           | Value      | Size     |
|:---------------|:----------:|:--------:|
| Message header | 0xAADCFBED | 4 bytes  |
| Is interested  | 0 or 1     | 1 byte   |

> Server will also expect that multibyte integers from the client come in the network byte order, so make sure you convert them before sending.

- After that, the server will start sending the image frames in JPEG format using the RTP protocol. To receive those, the client needs to open a UDP socket on port 45120.
- Once the client doesn't want to receive images anymore, it can send the "interest" message down the TCP connection again with the interest value of 0.
