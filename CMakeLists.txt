# Copyright (c) 2026
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.13)

set(CONFIG_CHERRYUSB 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_USB_DEVICE_CDC_ACM 1)
set(CONFIG_FREERTOS 1)

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})

project(usb2can)

sdk_compile_definitions(-DUSE_NONVECTOR_MODE=1)
sdk_compile_definitions(-DDISABLE_IRQ_PREEMPTIVE=1)

sdk_inc(config)
sdk_inc(inc)

sdk_app_src(src/main.c)
sdk_app_src(src/cdc_acm.c)
sdk_app_src(src/usb2can_app.c)
sdk_app_src(src/usb2can_bridge.c)
sdk_app_src(src/usb2can_can.c)
sdk_app_src(src/usb2can_crc.c)
sdk_app_src(src/usb2can_protocol.c)
sdk_app_src(src/usb2can_usb.c)

generate_ide_projects()
