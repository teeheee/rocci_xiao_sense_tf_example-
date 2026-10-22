#include "rocci_cam.h"

void rocci_cam_process_image(camera_fb_t *fb)
{
    terminal_printf("Processing image of size: %d bytes", fb->len);
    terminal_printf("Image dimensions: %dx%d", fb->width, fb->height);
    terminal_printf("pixel color 1: %02X %02X %02X", fb->buf[0], fb->buf[1], fb->buf[2]);

    rocci_cam_publish_i2c_result(0x00, fb->buf[0]);
}

int main()
{
    rocci_cam_init();
    rocci_cam_register_callback(rocci_cam_process_image);
    while (true) {
        rocci_cam_process();
    }
    return 0;
}