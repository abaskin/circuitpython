/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Dan Halbert for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "genhdr/autogen_usb_descriptor.h"
#include "py/gc.h"
#include "py/obj.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "py/objtuple.h"
#include "shared-bindings/usb_cdc/__init__.h"
#include "shared-bindings/usb_cdc/Serial.h"
#include "tusb.h"

#if CFG_TUD_CDC != 2
#error CFG_TUD_CDC must be exactly 2
#endif

bool usb_cdc_repl_enabled;
bool usb_cdc_data_enabled;

static const uint8_t usb_cdc_descriptor_template[] = {
    // CDC IAD Descriptor
    0x08,        //  0 bLength
    0x0B,        //  1 bDescriptorType: IAD Descriptor
    0xFF,        //  2 bFirstInterface  [SET AT RUNTIME]
#define CDC_FIRST_INTERFACE_INDEX 2
    0x02,        //  3 bInterfaceCount: 2
    0x02,        //  4 bFunctionClass: COMM
    0x02,        //  5 bFunctionSubclass: ACM
    0x00,        //  6 bFunctionProtocol: NONE
    0x00,        //  7 iFunction

    // CDC Comm Interface Descriptor
    0x09,        //  8 bLength
    0x04,        //  9 bDescriptorType (Interface)
    0xFF,        // 10 bInterfaceNumber  [SET AT RUNTIME]
#define CDC_COMM_INTERFACE_INDEX 10
    0x00,        // 11 bAlternateSetting
    0x01,        // 12 bNumEndpoints 1
    0x02,        // 13 bInterfaceClass: COMM
    0x02,        // 14 bInterfaceSubClass: ACM
    0x00,        // 15 bInterfaceProtocol: NONE
    0xFF,        // 16 iInterface (String Index)
#define CDC_COMM_INTERFACE_STRING_INDEX 16

    // CDC Header Descriptor
    0x05,        // 17 bLength
    0x24,        // 18 bDescriptorType: CLASS SPECIFIC INTERFACE
    0x00,        // 19 bDescriptorSubtype: NONE
    0x10, 0x01,  // 20,21 bcdCDC: 1.10

    // CDC Call Management Descriptor
    0x05,        // 22 bLength
    0x24,        // 23 bDescriptorType: CLASS SPECIFIC INTERFACE
    0x01,        // 24 bDescriptorSubtype: CALL MANAGEMENT
    0x01,        // 25 bmCapabilities
    0xFF,        // 26 bDataInterface  [SET AT RUNTIME]
#define CDC_CALL_MANAGEMENT_DATA_INTERFACE_INDEX 26

    // CDC Abstract Control Management Descriptor
    0x04,        // 27 bLength
    0x24,        // 28 bDescriptorType: CLASS SPECIFIC INTERFACE
    0x02,        // 29 bDescriptorSubtype: ABSTRACT CONTROL MANAGEMENT
    0x02,        // 30 bmCapabilities

    // CDC Union Descriptor
    0x05,        // 31 bLength
    0x24,        // 32 bDescriptorType: CLASS SPECIFIC INTERFACE
    0x06,        // 33 bDescriptorSubtype: CDC
    0xFF,        // 34 bMasterInterface  [SET AT RUNTIME]
#define CDC_UNION_MASTER_INTERFACE_INDEX 34
    0xFF,        // 35 bSlaveInterface_list (1 item)
#define CDC_UNION_SLAVE_INTERFACE_INDEX 35

    // CDC Control IN Endpoint Descriptor
    0x07,        // 36 bLength
    0x05,        // 37 bDescriptorType (Endpoint)
    0xFF,        // 38 bEndpointAddress (IN/D2H) [SET AT RUNTIME: 0x8 | number]
#define CDC_CONTROL_IN_ENDPOINT_INDEX 38
    0x03,        // 39 bmAttributes (Interrupt)
    0x40, 0x00,  // 40, 41 wMaxPacketSize 64
    0x10,        // 42 bInterval 16 (unit depends on device speed)

    // CDC Data Interface
    0x09,        // 43 bLength
    0x04,        // 44 bDescriptorType (Interface)
    0xFF,        // 45 bInterfaceNumber  [SET AT RUNTIME]
#define CDC_DATA_INTERFACE_INDEX 45
    0x00,        // 46 bAlternateSetting
    0x02,        // 47 bNumEndpoints 2
    0x0A,        // 48 bInterfaceClass: DATA
    0x00,        // 49 bInterfaceSubClass: NONE
    0x00,        // 50 bInterfaceProtocol
    0x05,        // 51 iInterface (String Index)
#define CDC_DATA_INTERFACE_STRING_INDEX 51

    // CDC Data OUT Endpoint Descriptor
    0x07,        // 52 bLength
    0x05,        // 53 bDescriptorType (Endpoint)
    0xFF,        // 54 bEndpointAddress (OUT/H2D) [SET AT RUNTIME]
#define CDC_DATA_OUT_ENDPOINT_INDEX 54
    0x02,        // 55 bmAttributes (Bulk)
    0x40, 0x00,  // 56,57  wMaxPacketSize 64
    0x00,        // 58 bInterval 0 (unit depends on device speed)

    // CDC Data IN Endpoint Descriptor
    0x07,        // 59 bLength
    0x05,        // 60 bDescriptorType (Endpoint)
    0xFF,        // 61 bEndpointAddress (IN/D2H) [SET AT RUNTIME: 0x8 | number]
#define CDC_DATA_IN_ENDPOINT_INDEX 61
    0x02,        // 62 bmAttributes (Bulk)
    0x40, 0x00,  // 63,64 wMaxPacketSize 64
    0x00,        // 65 bInterval 0 (unit depends on device speed)
};

size_t usb_cdc_descriptor_length(void) {
    return sizeof(usb_cdc_descriptor_template);
}

size_t usb_cdc_add_descriptor(uint8_t *descriptor_buf, uint8 comm_interface, uint8_t data_interface, uint8_t control_in_endpoint, uint8_t data_in_endpoint, uint8_t data_out_endpoint, uint8_t comm_interface_string, uint8_t data_interface_string) {
    memcpy(descriptor_buf, usb_midi_descriptor_template, sizeof(usb_midi_descriptor_template));
    descriptor_buf[CDC_FIRST_INTERFACE_INDEX] = comm_interface;
    descriptor_buf[CDC_COMM_INTERFACE_INDEX] = comm_interface;
    descriptor_buf[CDC_CALL_MANAGEMENT_DATA_INTERFACE_INDEX] = data_interface;
    descriptor_buf[CDC_UNION_MASTER_INTERFACE_INDEX] = comm_interface;
    descriptor_buf[CDC_UNION_SLAVE_INTERFACE_INDEX] = data_interface;
    descriptor_buf[CDC_DATA_INTERFACE_INDEX] = data_interface;

    descriptor_buf[CDC_CONTROL_IN_ENDPOINT_INDEX] = control_in_endpoint;
    descriptor_buf[CDC_DATA_OUT_ENDPOINT_INDEX] = data_out_endpoint;
    descriptor_buf[CDC_DATA_IN_ENDPOINT_INDEX] = data_in_endpoint;

    descriptor_buf[CDC_COMM_INTERFACE_STRING_INDEX] = comm_interface_string;
    descriptor_buf[CDC_DATA_INTERFACE_STRING_INDEX] = data_interface_string;

    return sizeof(usb_midi_descriptor_template);
}

static usb_cdc_serial_obj_t usb_cdc_repl_obj = {
    .base.type = &usb_cdc_serial_type,
    .timeout = -1.0f,
    .write_timeout = -1.0f,
    .idx = 0,
};

static usb_cdc_serial_obj_t usb_cdc_data_obj = {
    .base.type = &usb_cdc_serial_type,
    .timeout = -1.0f,
    .write_timeout = -1.0f,
    .idx = 1,
};

void usb_cdc_init(void) {
    usb_cdc_repl_enabled = true;
    usb_cdc_data_enabled = false;
}

bool common_hal_usb_cdc_configure_usb(bool repl_enabled, bool data_enabled) {
    // We can't change the descriptors once we're connected.
    if (tud_connected()) {
        return false;
    }

    usb_cdc_repl_enabled = repl_enabled;
    usb_cdc_set_repl(repl_enabled ? MP_OBJ_FROM_PTR(&usb_cdc_repl_obj) : mp_const_none);

    usb_cdc_data_enabled = data_enabled;
    usb_cdc_set_data(data_enabled ? MP_OBJ_FROM_PTR(&usb_cdc_data_obj) : mp_const_none);

    return true;
}
