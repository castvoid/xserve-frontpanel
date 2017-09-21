//
//  main.c
//  hwmond
//
//  Created by Harry Jones on 21/08/2017.
//  Copyright © 2017 Harry Jones. All rights reserved.
//

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

#define PANEL_VENDOR 0x5ac
#define PANEL_USB_ID 0x8261
#define PANEL_CONFIG 0
#define PANEL_DATA_SIZE 32

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

    libusb_set_debug(usb_context, LIBUSB_LOG_LEVEL_INFO);

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

void loop_update_panel (libusb_device_handle *frontpanel_device_handle,
                        unsigned char frontpanel_endpoint_addr) {
    const useconds_t SLEEP_TIME = (useconds_t)(1e6/100);

    uint8_t *output_bytes = calloc(PANEL_DATA_SIZE, sizeof(uint8_t));

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-noreturn"

    float offset = 0;
    while ( 1 ) {
        do {
            while ( write_bytes_to_frontpanel(output_bytes,
                                              PANEL_DATA_SIZE,
                                              frontpanel_device_handle,
                                              frontpanel_endpoint_addr) == 0 ) {
                sleep(2);
            }
        } while ( 0 ); // TODO: while ( CPU_count == 0 );

        for (int i = 0; i < 16; i++) {
            // Just a nice, smooth function
            // You'd actually want this to use the CPU usage, but there's no
            // nice cross-platform way of doing so.
            output_bytes[i] = (uint8_t)( 50.0 * (1+sin(M_PI*fmodf(i+offset,16)/8.0f)) );
        }
        offset = fmodf(offset + 0.1f, 16);

        usleep(SLEEP_TIME);
    }

#pragma clang diagnostic pop
}

int main(int argc, const char * argv[]) {
    libusb_device_handle *device_handle = NULL;
    unsigned char endpoint_addr = 0;

    int error = setupUSB(&device_handle, &endpoint_addr);

    if (!error) {
        loop_update_panel(device_handle, endpoint_addr);
    }

    if (device_handle) {
        // TODO: close other interfaces, release config descriptors etc.
        libusb_close(device_handle);
    }

    return error;
}
