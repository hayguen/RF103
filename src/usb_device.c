/*
 * usb_device.c - FX3 functions
 *
 * Copyright (C) 2020 by Franco Venturi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libusb.h>

#include "usb_device.h"
#include "error_handling.h"


/* internal functions */
static struct libusb_device_handle *find_usb_device(int index,
                                                    int *needs_firmware);
static int load_image(struct libusb_device_handle *dev_handle,
                      const char *imagefile);
static int validate_image(const uint8_t *image, const size_t size);
static int transfer_image(const uint8_t *image,
                          libusb_device_handle *dev_handle);


struct usb_device_id {
  uint16_t vid;
  uint16_t pid;
  int needs_firmware;
};

static struct usb_device_id usb_device_ids[] = {
  { 0x04b4, 0x00f3, 1 },     /* Cypress / FX3 Boot-loader */
  { 0x04b4, 0x00f1, 0 }      /* Cypress / FX3 Streamer Example */
};
static int n_usb_device_ids = sizeof(usb_device_ids) / sizeof(usb_device_ids[0]);


int usb_device_count_devices()
{
  int ret_val = -1;

  int ret = libusb_init(0);
  if (ret < 0) {
    usb_error(ret, __func__, __FILE__, __LINE__);
    goto FAIL0;
  }
  libusb_device **list = 0;
  ssize_t nusbdevices = libusb_get_device_list(0, &list);
  if (nusbdevices < 0) {
    usb_error(nusbdevices, __func__, __FILE__, __LINE__);
    goto FAIL1;
  }
  int count = 0;
  for (ssize_t i = 0; i < nusbdevices; ++i) {
    libusb_device *dev = list[i];
    struct libusb_device_descriptor desc;
    ret = libusb_get_device_descriptor(dev, &desc);
    for (int i = 0; i < n_usb_device_ids; ++i) {
      if (desc.idVendor == usb_device_ids[i].vid &&
          desc.idProduct == usb_device_ids[i].pid) {
        count++;
      }
    }
  }
  libusb_free_device_list(list, 1);

  ret_val = count;

FAIL1:
  libusb_exit(0);
FAIL0:
  return ret_val;
}


int usb_device_get_device_list(struct usb_device_info **usb_device_infos)
{
  const int MAX_STRING_BYTES = 256;

  int ret_val = -1;

  if (usb_device_infos == 0) {
    error("argument usb_device_infos is a null pointer", __func__, __FILE__, __LINE__);
    goto FAIL0;
  }

  int ret = libusb_init(0);
  if (ret < 0) {
    usb_error(ret, __func__, __FILE__, __LINE__);
    goto FAIL0;
  }
  libusb_device **list = 0;
  ssize_t nusbdevices = libusb_get_device_list(0, &list);
  if (nusbdevices < 0) {
    usb_error(nusbdevices, __func__, __FILE__, __LINE__);
    goto FAIL1;
  }

  struct usb_device_info *device_infos = (struct usb_device_info *) malloc((nusbdevices + 1) * sizeof(struct usb_device_info));
  int count = 0;
  for (ssize_t i = 0; i < nusbdevices; ++i) {
    libusb_device *device = list[i];
    struct libusb_device_descriptor desc;
    ret = libusb_get_device_descriptor(device, &desc);
    for (int i = 0; i < n_usb_device_ids; ++i) {
      if (!(desc.idVendor == usb_device_ids[i].vid &&
            desc.idProduct == usb_device_ids[i].pid)) {
        continue;
      }

      libusb_device_handle *dev_handle = 0;
      ret = libusb_open(device, &dev_handle);
      if (ret < 0) {
        usb_error(ret, __func__, __FILE__, __LINE__);
        goto FAIL2;
      }

      device_infos[count].manufacturer = (unsigned char *) malloc(MAX_STRING_BYTES);
      device_infos[count].manufacturer[0] = '\0';
      if (desc.iManufacturer) {
        ret = libusb_get_string_descriptor_ascii(dev_handle, desc.iManufacturer,
                      device_infos[count].manufacturer, MAX_STRING_BYTES);
        if (ret < 0) {
          usb_error(ret, __func__, __FILE__, __LINE__);
          goto FAIL3;
        }
        device_infos[count].manufacturer = (unsigned char *) realloc(device_infos[count].manufacturer, ret);
      }

      device_infos[count].product = (unsigned char *) malloc(MAX_STRING_BYTES);
      device_infos[count].product[0] = '\0';
      if (desc.iProduct) {
        ret = libusb_get_string_descriptor_ascii(dev_handle, desc.iProduct,
                      device_infos[count].product, MAX_STRING_BYTES);
        if (ret < 0) {
          usb_error(ret, __func__, __FILE__, __LINE__);
          goto FAIL3;
        }
        device_infos[count].product = (unsigned char *) realloc(device_infos[count].product, ret);
      }

      device_infos[count].serial_number = (unsigned char *) malloc(MAX_STRING_BYTES);
      device_infos[count].serial_number[0] = '\0';
      if (desc.iSerialNumber) {
        ret = libusb_get_string_descriptor_ascii(dev_handle, desc.iSerialNumber,
                      device_infos[count].serial_number, MAX_STRING_BYTES);
        if (ret < 0) {
          usb_error(ret, __func__, __FILE__, __LINE__);
          goto FAIL3;
        }
        device_infos[count].serial_number = (unsigned char *) realloc(device_infos[count].serial_number, ret);
      }

      ret = 0;
FAIL3:
      libusb_close(dev_handle);
      if (ret < 0) {
        goto FAIL2;
      }
      count++;
    }
  }

  device_infos[count].manufacturer = 0;
  device_infos[count].product = 0;
  device_infos[count].serial_number = 0;

  *usb_device_infos = device_infos;
  ret_val = count;

FAIL2:
  libusb_free_device_list(list, 1);
FAIL1:
  libusb_exit(0);
FAIL0:
  return ret_val;
}


int usb_device_free_device_list(struct usb_device_info *usb_device_infos)
{
  for (struct usb_device_info *udi = usb_device_infos;
       udi->manufacturer || udi->product || udi->serial_number;
       ++udi) {
    if (udi->manufacturer) {
      free(udi->manufacturer);
    }
    if (udi->product) {
      free(udi->product);
    }
    if (udi->serial_number) {
      free(udi->serial_number);
    }
  }
  free(usb_device_infos);
  return 0;
}


usb_device_t *usb_device_open(int index, const char* imagefile)
{
  usb_device_t *ret_val = 0;

  int ret = libusb_init(0);
  if (ret < 0) {
    usb_error(ret, __func__, __FILE__, __LINE__);
    goto FAIL0;
  }

  int needs_firmware = 0;
  struct libusb_device_handle *dev_handle = find_usb_device(index, &needs_firmware);
  if (dev_handle == 0) {
    goto FAIL1;
  }

  if (needs_firmware) {
    ret = load_image(dev_handle, imagefile);
    if (ret < 0) {
      error("load_image() failed", __func__, __FILE__, __LINE__);
      goto FAIL2;
    }

    /* rescan USB to get a new device handle */
    libusb_close(dev_handle);
    needs_firmware = 0;
    dev_handle = find_usb_device(index, &needs_firmware);
    if (dev_handle == 0) {
      goto FAIL1;
    }
    if (needs_firmware) {
      error("device is still in boot loader mode", __func__, __FILE__, __LINE__);
      goto FAIL2;
    }
  }

  usb_device_t *this = (usb_device_t *) malloc(sizeof(usb_device_t));
  this->dev_handle = dev_handle;

  ret_val = this;
  return ret_val;

FAIL2:
  libusb_close(dev_handle);
FAIL1:
  libusb_exit(0);
FAIL0:
  return ret_val;
}


void usb_device_close(usb_device_t *this)
{
  libusb_close(this->dev_handle);
  free(this);
  libusb_exit(0);
  return;
}


/* internal functions */
static struct libusb_device_handle *find_usb_device(int index, int *needs_firmware)
{
  struct libusb_device_handle *ret_val = 0;

  *needs_firmware = 0;

  libusb_device **list = 0;
  ssize_t nusbdevices = libusb_get_device_list(0, &list);
  if (nusbdevices < 0) {
    usb_error(nusbdevices, __func__, __FILE__, __LINE__);
    goto FAIL0;
  }

  int count = 0;
  libusb_device *device = 0;
  for (ssize_t i = 0; i < nusbdevices; ++i) {
    libusb_device *dev = list[i];
    struct libusb_device_descriptor desc;
    libusb_get_device_descriptor(dev, &desc);
    for (int i = 0; i < n_usb_device_ids; ++i) {
      if (desc.idVendor == usb_device_ids[i].vid &&
          desc.idProduct == usb_device_ids[i].pid) {
        if (count == index) {
          device = dev;
          *needs_firmware = usb_device_ids[i].needs_firmware;
        }
        count++;
      }
    }
  }

  if (device == 0) {
    fprintf(stderr, "ERROR - usb_device@%d not found\n", index);
    goto FAIL1;
  }

  libusb_device_handle *dev_handle = 0;
  int ret = libusb_open(device, &dev_handle);
  if (ret < 0) {
    usb_error(ret, __func__, __FILE__, __LINE__);
    goto FAIL1;
  }
  libusb_free_device_list(list, 1);

  ret = libusb_kernel_driver_active(dev_handle, 0);
  if (ret < 0) {
    usb_error(ret, __func__, __FILE__, __LINE__);
    goto FAILA;
  }
  if (ret == 1) {
    fprintf(stderr, "ERROR - device busy\n");
    goto FAILA;
  }

  ret = libusb_claim_interface(dev_handle, 0);
  if (ret < 0) {
    usb_error(ret, __func__, __FILE__, __LINE__);
    goto FAILA;
  }

  ret_val = dev_handle;
  return ret_val;

FAILA:
  libusb_close(dev_handle);
  return ret_val;

FAIL1:
  libusb_free_device_list(list, 1);
FAIL0:
  return ret_val;
}


int load_image(struct libusb_device_handle *dev_handle, const char *imagefile)
{
  int ret_val = -1;

  int fd = open(imagefile, O_RDONLY);
  if (fd < 0) {
    fprintf(stderr, "ERROR - open(%s) failed: %s\n", imagefile, strerror(errno));
    goto FAIL0;
  }

  /* slurp the whole fle into memory */
  struct stat statbuf;
  int ret = fstat(fd, &statbuf);
  if (ret < 0) {
    fprintf(stderr, "ERROR - fstat(%s) failed: %s\n", imagefile, strerror(errno));
    goto FAIL1;
  }
  size_t image_size = statbuf.st_size;
  uint8_t *image = (uint8_t *) malloc(image_size);
  if (image == 0) {
    fprintf(stderr, "ERROR - malloc() failed: %s\n", strerror(errno));
    goto FAIL1;
  }
  for (size_t nleft = image_size; nleft != 0; ) {
    ssize_t nr = read(fd, image, nleft);
    if (nr < 0) {
      fprintf(stderr, "ERROR - read(%s) failed: %s\n", imagefile, strerror(errno));
      goto FAIL1;
    }
    nleft -= nr;
  }

  close(fd);

  if (validate_image(image, image_size) < 0) {
    fprintf(stderr, "ERROR - validate_image() failed\n");
    goto FAILA;
  }

  if (transfer_image(image, dev_handle) < 0) {
    fprintf(stderr, "ERROR - transfer_image() failed\n");
    goto FAILA;
  }

  ret_val = 0;

FAILA:
  free(image);
  return ret_val;

FAIL1:
  close(fd);
FAIL0:
  return ret_val;
}


static int validate_image(const uint8_t *image, const size_t size)
{
  if (size < 10240) {
    fprintf(stderr, "ERROR - image file is too small\n");
    return -1;
  }
  if (!(image[0] == 'C' && image[1] == 'Y')) {
    fprintf(stderr, "ERROR - image header does not start with 'CY'\n");
    return -1;
  }
  if (!(image[2] == 0x1c)) {
    fprintf(stderr, "ERROR - I2C config is not set to 0x1C\n");
    return -1;
  }
  if (!(image[3] == 0xb0)) {
    fprintf(stderr, "ERROR - image type is not binary (0x01)\n");
    return -1;
  }

  uint32_t checksum = 0;
  uint32_t *current = (uint32_t *) image + 1;
  uint32_t *end = (uint32_t *) (image + size);

  while (1) {
    uint32_t loadSz = *current++;
    //printf("\tloadSz: %u\n", loadSz);
    if (loadSz == 0) {
      break;
    }
    uint32_t secStart __attribute__((unused)) = *current++;
    //printf("\tsecStart: 0x%08x\n", secStart);
    if (current + loadSz >= end - 2) {
      fprintf(stderr, "ERROR - loadSz is too big - loadSz=%u\n", loadSz);
      return -1;
    }
    while (loadSz--) {
      checksum += *current++;
    }
  }
  uint32_t entryAddr __attribute__((unused)) = *current++;
  //printf("entryAddr: 0x%08x\n", entryAddr);
  uint32_t expected_checksum = *current++;
  if (!(current == end)) {
    fprintf(stderr, "WARNING - image file longer than expected\n");
  }
  if (!(checksum == expected_checksum)) {
      fprintf(stderr, "ERROR - checksum does not match - actual=0x%08x expected=0x%08x\n",
              checksum, expected_checksum);
      return -1;
  }
  return 0;
}


static int transfer_image(const uint8_t *image,
                          libusb_device_handle *dev_handle)
{
  const uint8_t bmRequestType = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE;
  const uint8_t bRequest = 0xa0;            // vendor command
  const unsigned int timeout = 5000;        // timeout (in ms) for each command
  const size_t max_write_size = 2 * 1024;   // max write size in bytes
 
  // skip first word with 'CY' magic
  uint32_t *current = (uint32_t *) image + 1;

  while (1) {
    uint32_t loadSz = *current++;
    if (loadSz == 0) {
      break;
    }
    uint32_t address = *current++;

    unsigned char *data = (unsigned char *) current;
    for (size_t nleft = loadSz * 4; nleft > 0; ) {
      uint16_t wLength = nleft > max_write_size ? max_write_size : nleft;
      int ret = libusb_control_transfer(dev_handle, bmRequestType, bRequest,
                                        address & 0xffff, address >> 16,
                                        data, wLength, timeout);
      if (ret < 0) {
        usb_error(ret, __func__, __FILE__, __LINE__);
        return -1;
      }
      if (!(ret == wLength)) {
        fprintf(stderr, "ERROR - libusb_control_transfer() returned less bytes than expected - actual=%hu expected=%hu\n", ret, wLength);
        return -1;
      }
      data += wLength;
      nleft -= wLength;
    }
    current += loadSz;
  }

  uint32_t entryAddr = *current++;
  uint32_t checksum __attribute__((unused)) = *current++;

  sleep(1);

  int ret = libusb_control_transfer(dev_handle, bmRequestType, bRequest,
                                    entryAddr & 0xffff, entryAddr >> 16,
                                    0, 0, timeout);
  if (ret < 0) {
    usb_warning(ret, __func__, __FILE__, __LINE__);
  }

  return 0;
}
