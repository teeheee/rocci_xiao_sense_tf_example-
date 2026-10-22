#include "rocci_cam.h"
#include "rocci_cam_serial.h"

bool camera_running_flag = true;

void rocci_cam_process_image(camera_fb_t *fb);

void (*rocci_cam_callback)(camera_fb_t *fb) = nullptr;

void rocci_cam_init(void)
{
  rocci_cam_serial_init();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_96X96;
  config.pixel_format = PIXFORMAT_RGB565; // for streaming
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    send_to_terminal("Camera init failed with error 0x%x", err);
    return;
  }
}

void rocci_cam_process(void)
{
    rocci_cam_serial_process();     
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        send_to_terminal("Camera capture failed");
        return;
    }
    if (rocci_cam_callback != nullptr)
        rocci_cam_callback(fb);
    rocci_cam_serial_process_image(fb);
    esp_camera_fb_return(fb);
}

void process_image(camera_fb_t *fb)
{
    if(rocci_cam_callback != nullptr) {
        rocci_cam_callback(fb);
    }
}


void rocci_cam_register_callback(void (*callback)(camera_fb_t *fb))
{
  rocci_cam_callback = callback;    
}

void rocci_cam_publish_i2c_result(uint8_t reg, uint8_t value)
{
    send_to_terminal("I2C Result - Register: 0x%02X, Value: 0x%02X", reg, value);
}