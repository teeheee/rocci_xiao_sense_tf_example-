#ifndef ROCCI_CAM_H
#define ROCCI_CAM_H

#include "esp_camera.h"

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM


void rocci_cam_init(void);
void rocci_cam_process(void);
void rocci_cam_register_callback(void (*callback)(camera_fb_t *fb));
void rocci_cam_publish_i2c_result(uint8_t reg, uint8_t value);
void send_to_terminal(const char *message, ...);
void draw_box(int x, int y, int width, int height, uint16_t color);
void draw_point(int x, int y, uint16_t color);
void draw_line(int x0, int y0, int x1, int y1, uint16_t color);


#endif // ROCCI_CAM_H