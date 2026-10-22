#ifndef ROCCI_CAM_SERIAL_H
#define ROCCI_CAM_SERIAL_H

#include "esp_camera.h"

void rocci_cam_serial_init(void);
void rocci_cam_serial_process(void);
void send_image_to_serial(camera_fb_t *fb);

#endif // ROCCI_CAM_SERIAL_H