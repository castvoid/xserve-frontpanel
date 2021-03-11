/*
 This is a small program that demonstrates how to drive the Intel Xserve's front
 panel CPU activity LEDs. I've only tested it on a 2009 Dual Xeon Xserve, but it
 should work on any Intel-based Xserve.

 It's a work in progress that's currently on hold, but should serve as a good
 start point for a proper implementation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <libusb.h>
#include <math.h>
#include <pthread.h>
#include <inttypes.h>

#include "cpu_usage.h"

#define PANEL_VENDOR 0x5ac
#define PANEL_USB_ID 0x8261
#define PANEL_CONFIG 0
#define NUM_LEDS_PER_ROW 8
#define NUM_LED_ROWS 2
#define PANEL_DATA_SIZE 32
#define LED_UPDATE_INTERVAL ((useconds_t)(1e6/60))
#define LED_MOVE_RATE 0.05

/**
 Connects to the front panel over USB, and configures it to be ready to accept
 data.

 @param frontpanel_device_handle_ptr output location for the frontpanel device
 handle. Set iff the device is successfully opened.
 @param frontpanel_endpoint_addr_ptr output location for the endpoint address
 that should be used to write to the front panel
 @return An error number: 0 iff the device could be successfully configured
 */
int setupUSB(libusb_device_handle **frontpanel_device_handle_ptr,
             unsigned char *frontpanel_endpoint_addr_ptr) {
    int error = 0;
    const struct libusb_interface *interface = NULL;
    struct libusb_config_descriptor *config = NULL;
    libusb_device_handle *device_handle = NULL;

    libusb_context *usb_context;
    if ((error = libusb_init(&usb_context)) != 0) {
        printf("Failed to init USB context: %d\n", error);
        goto fail;
    }

#if LIBUSB_API_VERSION >= 0x01000106
    libusb_set_option(usb_context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO);
#else
    libusb_set_debug(usb_context, LIBUSB_LOG_LEVEL_INFO);
#endif

    // XXX: lazy - can be more robust
    device_handle = libusb_open_device_with_vid_pid(usb_context,
                                                    PANEL_VENDOR,
                                                    PANEL_USB_ID);

    if (!device_handle) {
        printf("Couldn't connect to front panel! "
               "(Are you running this on an Intel Xserve?)\n");
        error = 1;
        goto fail;
    }

    libusb_device *dev = libusb_get_device(device_handle);

    if ( (error = libusb_set_configuration(device_handle, PANEL_CONFIG)) ) {
        printf("Couldn't set configuration to %d: got %d", PANEL_CONFIG, error);
        error = 1;
        goto fail;
    }

    libusb_get_config_descriptor(dev, PANEL_CONFIG, &config);

    // We always choose the first interface
    if (config->bNumInterfaces < 1) {
        printf("Device had no interfaces\n");
        error = 1;
        goto fail;
    }
    const int interface_num = 0;
    if ( (error = libusb_claim_interface(device_handle, interface_num)) ) {
        printf("Couldn't claim interface: %d\n", error);
        error = 1;
        goto fail;
    }

    interface = &config->interface[interface_num];

    if (interface->num_altsetting != 1) {
        printf("Unexpected number of alt-settings: expected 1, got %d\n", interface->num_altsetting);
        error = 1;
        goto fail;
    }
    const struct libusb_interface_descriptor *interface_desc = &interface->altsetting[0];

    // Find correct endpoint
    bool found_endpoint = false;
    unsigned char endpoint_address = 0;
    for (uint8_t i = 0; i < interface_desc->bNumEndpoints; i++) {
        const struct libusb_endpoint_descriptor *endpoint = &interface_desc->endpoint[i];
        if ((endpoint->bmAttributes & 0b11) == LIBUSB_TRANSFER_TYPE_BULK &&
            (endpoint->bEndpointAddress & 0b1000000) == LIBUSB_ENDPOINT_OUT) {

            endpoint_address = endpoint->bEndpointAddress;
            found_endpoint = true;
            break;
        }
    }

    if (!found_endpoint) {
        printf("Couldn't find bulk output endpoint on device.\n");
        error = 1;
        goto fail;
    }

    *frontpanel_device_handle_ptr = device_handle;
    *frontpanel_endpoint_addr_ptr = endpoint_address;

    //    libusb_release_interface(device_handle, interface_num);
    libusb_free_config_descriptor(config);

    return 0;

fail:
    if (interface && device_handle && interface_num) {
        libusb_release_interface(device_handle, interface_num);
        interface = NULL;
    }

    if (config) {
        libusb_free_config_descriptor(config);
        config = NULL;
    }

    if (device_handle) {
        libusb_close(device_handle);
        device_handle = NULL;
    }

    return error;
}

/**
 Attempts to write `length` bytes from `bytes` to the front panel.

 @param bytes a buffer of length >=`length`, containing data to be written
 @param length the number of bytes to write to the USB device from the buffer
 @param frontpanel_device_handle The device handle to write to
 @param frontpanel_endpoint_addr The address of theendpoint to be used on the device
 @return The number of bytes successfully written to the device
 */
int write_bytes_to_frontpanel(void *bytes,
                              uint32_t length,
                              libusb_device_handle *frontpanel_device_handle,
                              unsigned char frontpanel_endpoint_addr) {
    int actual_length;
    const int timeout_ms = 90;

    libusb_bulk_transfer(frontpanel_device_handle,
                         frontpanel_endpoint_addr,
                         bytes,
                         length,
                         &actual_length,
                         timeout_ms);

    return actual_length;
}

/**
 Smoothly updates the LEDs with the values from `usages` every LED_UPDATE_INTERVAL

 @param frontpanel_device_handle The device handle to write to
 @param frontpanel_endpoint_addr The address of theendpoint to be used on the device
 @param usages An array of length NUM_LED_ROWS containing a usage proportion (from 0 to 1) for each LED row
 */
void loop_update_panel(libusb_device_handle *frontpanel_device_handle,
                       unsigned char frontpanel_endpoint_addr,
                       const volatile float *usages) {

    uint8_t *output_bytes = calloc(PANEL_DATA_SIZE, sizeof(uint8_t));

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"

    const float usage_bucket_size = 1.0f/NUM_LEDS_PER_ROW;
    float usage_smoothed[NUM_LED_ROWS];
    while ( 1 ) {
        bool updated = false;

        for (int row = 0; row < NUM_LED_ROWS; row++) {
            float usage_real = usages[row];

            if (fabs(usage_smoothed[row] - usage_real) < 0.001) {
                continue;
            }
            updated = true;

            if (isnan(usage_smoothed[row])) {
                usage_smoothed[row] = usage_real;
            } else {
                usage_smoothed[row] = LED_MOVE_RATE * usage_real + (1-LED_MOVE_RATE) * usage_smoothed[row];
            }

            float usage_temp = usage_smoothed[row];
            for (int i = 0; i < NUM_LEDS_PER_ROW; i++) {
                float led_lit_proportion = fmaxf( fminf(usage_temp, usage_bucket_size), 0 ) / usage_bucket_size;
                output_bytes[i + row * NUM_LEDS_PER_ROW] = (uint8_t)roundf(led_lit_proportion * UINT8_MAX);
                usage_temp -= usage_bucket_size;
            }
        }

        while ( updated &&  write_bytes_to_frontpanel(output_bytes,
                                                      PANEL_DATA_SIZE,
                                                      frontpanel_device_handle,
                                                      frontpanel_endpoint_addr) == 0 ) {
            sleep(1);
        }

        usleep(LED_UPDATE_INTERVAL);
    }

#pragma clang diagnostic pop
}

typedef struct {
    libusb_device_handle *device_handle;
    unsigned char endpoint_addr;
    const volatile float *usages;
} panel_update_loop_args;

static void *update_panel_thread_fn(void *args) {
    panel_update_loop_args* args_real = (panel_update_loop_args *)args;
    loop_update_panel(args_real->device_handle, args_real->endpoint_addr, args_real->usages);
    return NULL;
}

int main(int argc, const char * argv[]) {
    libusb_device_handle *device_handle = NULL;
    unsigned char endpoint_addr = 0;

    int error = setupUSB(&device_handle, &endpoint_addr);

    if (!error) {
        float usages[2] = {1.0f, 1.0f};

        panel_update_loop_args args = {
            .device_handle = device_handle,
            .endpoint_addr = endpoint_addr,
            .usages = usages,
        };

        pthread_t update_panel_thread;
        int r = pthread_create(&update_panel_thread, NULL, update_panel_thread_fn, &args);
        if (r != 0) printf("pthread_create failed\n");


        cpu_usage_setup();
        cpu_update_usage_loop(usages, sizeof(usages) / sizeof(usages[0]));
    }

    if (device_handle) {
        // TODO: close other interfaces, release config descriptors etc.
        libusb_close(device_handle);
    }

    return error;
}
