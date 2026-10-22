#include "rocci_cam_serial.h"
#include "rocci_cam.h"
#include "Arduino.h"

#define START_CODE "\xDE\xAD\xBE\xAF"

static bool camera_streaming_flag = false;
static bool quite_mode_flag = false;
static bool send_next_image = false;

void rocci_cam_serial_init(void)
{
    Serial.begin(115200);
    while(!Serial); // When the serial monitor is turned on, the program starts to execute
}

void rocci_cam_serial_process(void)
{
    if(Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command == "capture") {
            send_next_image = true;
        } else if (command == "process") {
            camera_fb_t fb;
            fb.buf = malloc(96 * 96 * 2); // Allocate buffer for 96x96 RGB565 image
            fb.len = 96 * 96 * 2;
            fb.width = 96;
            fb.height = 96;
            fb.format = PIXFORMAT_RGB565;
            if (!fb.buf) {
                send_to_terminal("Failed to allocate memory for image processing");
                return;
            }
            Serial.readBytes((uint8_t*)fb.buf, fb.len);
            rocci_cam_process_image(&fb);
            free(fb.buf);
        } else if (command == "start") {
            send_to_terminal("Starting camera streaming");
            camera_streaming_flag = true;
        } else if (command == "stop") {
            send_to_terminal("Stopping camera streaming");
            camera_streaming_flag = false;
        } else if (command == "quiet") {
            send_to_terminal("Entering quiet mode");
            quite_mode_flag = true;
        } else if (command == "loud") {
            send_to_terminal("Exiting quiet mode");
            quite_mode_flag = false;
        } else {
            send_to_terminal("Unknown command: %s\n", command.c_str());
        }
    }
}

void rocci_cam_serial_process_image(camera_fb_t *fb)
{
    if (camera_streaming_flag) {
        send_image_to_serial(fb);
    } else if (send_next_image) {
        send_image_to_serial(fb);
        send_next_image = false;
    }
}

void send_image_to_serial(camera_fb_t *fb)
{
    Serial.write(START_CODE, 4);
    Serial.write("I", 1);
    Serial.write((uint8_t*)&fb->len, sizeof(fb->len));
    Serial.write(fb->buf, fb->len);
}

void send_to_terminal(const char *message, ...)
{
    if(quite_mode_flag) {
        return;
    }
    Serial.print(START_CODE);
    Serial.print("T");
    va_list args;
    va_start(args, message);
    Serial.printf(message, args);
    va_end(args);
}

void draw_box(int x, int y, int width, int height, uint16_t color)
{
    if(quite_mode_flag) {
        return;
    }
    Serial.write(START_CODE, 4);
    Serial.write("B", 1);
    Serial.write((uint8_t*)&x, sizeof(x));
    Serial.write((uint8_t*)&y, sizeof(y));
    Serial.write((uint8_t*)&width, sizeof(width));
    Serial.write((uint8_t*)&height, sizeof(height));
    Serial.write((uint8_t*)&color, sizeof(color));
}

void draw_point(int x, int y, uint16_t color)
{
    if(quite_mode_flag) {
        return;
    }
    Serial.write(START_CODE, 4);
    Serial.write("P", 1);
    Serial.write((uint8_t*)&x, sizeof(x));
    Serial.write((uint8_t*)&y, sizeof(y));
    Serial.write((uint8_t*)&color, sizeof(color));
}

void draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    if(quite_mode_flag) {
        return;
    }
    Serial.write(START_CODE, 4);
    Serial.write("L", 1);
    Serial.write((uint8_t*)&x0, sizeof(x0));
    Serial.write((uint8_t*)&y0, sizeof(y0));
    Serial.write((uint8_t*)&x1, sizeof(x1));
    Serial.write((uint8_t*)&y1, sizeof(y1));
    Serial.write((uint8_t*)&color, sizeof(color));
}