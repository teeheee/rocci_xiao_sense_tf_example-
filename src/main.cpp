#include "rocci_cam.h"
#include "model_data.h"
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "esp_heap_caps.h"
#include <new>

namespace
{
constexpr int kImageWidth = 96;
constexpr int kImageHeight = 96;
constexpr int kImageChannels = 3;
constexpr size_t kTensorArenaSize = 256 * 1024;

uint8_t *tensor_arena = nullptr;
tflite::AllOpsResolver resolver;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input_tensor = nullptr;
TfLiteTensor *output_tensor = nullptr;
bool classifier_ready = false;

bool initialize_classifier()
{
    const tflite::Model *model = tflite::GetModel(g_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        send_to_terminal("Model schema version mismatch");
        return false;
    }

    tensor_arena = static_cast<uint8_t *>(
        heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    );
    if (tensor_arena == nullptr) {
        send_to_terminal("Failed to allocate TensorFlow Lite tensor arena");
        return false;
    }

    interpreter = new (std::nothrow) tflite::MicroInterpreter(
        model, resolver, tensor_arena, kTensorArenaSize,
        tflite::GetMicroErrorReporter()
    );
    if (interpreter == nullptr) {
        send_to_terminal("Failed to create TensorFlow Lite interpreter");
        return false;
    }
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        send_to_terminal("Failed to allocate TensorFlow Lite tensors");
        return false;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);
    if (input_tensor == nullptr || output_tensor == nullptr ||
        input_tensor->type != kTfLiteUInt8 ||
        input_tensor->dims->data[1] != kImageHeight ||
        input_tensor->dims->data[2] != kImageWidth ||
        input_tensor->dims->data[3] != kImageChannels ||
        output_tensor->type != kTfLiteUInt8 ||
        output_tensor->dims->data[1] != kClassCount) {
        send_to_terminal("TensorFlow Lite model input or output shape is unsupported");
        return false;
    }

    send_to_terminal(
        "TensorFlow Lite classifier ready with %u classes",
        static_cast<unsigned int>(kClassCount)
    );
    return true;
}

}

void rocci_cam_process_image(camera_fb_t *fb)
{
    if (!classifier_ready) {
        return;
    }
    if (fb == nullptr || fb->format != PIXFORMAT_RGB565 ||
        fb->width != kImageWidth || fb->height != kImageHeight ||
        fb->len < static_cast<size_t>(kImageWidth * kImageHeight * 2)) {
        send_to_terminal("Classifier expects a 96x96 RGB565 frame");
        return;
    }

    uint8_t *pixels = input_tensor->data.uint8;
    for (int pixel_index = 0; pixel_index < kImageWidth * kImageHeight; ++pixel_index) {
        const uint16_t pixel =
            (static_cast<uint16_t>(fb->buf[pixel_index * 2]) << 8) |
            fb->buf[pixel_index * 2 + 1];
        const uint8_t red = static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
        const uint8_t green = static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
        const uint8_t blue = static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
        pixels[pixel_index * 3] = red;
        pixels[pixel_index * 3 + 1] = green;
        pixels[pixel_index * 3 + 2] = blue;
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        send_to_terminal("TensorFlow Lite inference failed");
        return;
    }

    int best_class = 0;
    for (int class_index = 1; class_index < static_cast<int>(kClassCount); ++class_index) {
        if (output_tensor->data.uint8[class_index] >
            output_tensor->data.uint8[best_class]) {
            best_class = class_index;
        }
    }
    const float confidence =
        (output_tensor->data.uint8[best_class] - output_tensor->params.zero_point) *
        output_tensor->params.scale;
    send_to_terminal(
        "Prediction: %s (%.1f%%)",
        kClassNames[best_class],
        confidence * 100.0f
    );
}

void setup()
{
    rocci_cam_init();
    classifier_ready = initialize_classifier();
    rocci_cam_register_callback(rocci_cam_process_image);
}

void loop()
{
    rocci_cam_process();
}