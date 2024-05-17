#include <teensy4_usb.h>
#include <cerrno>

#define USE_MASS_STORAGE
#include <usb_drivers.h>

static DMAMEM TeensyUSBHost2 usb;

void setup() {
  Serial.begin(0);
  while (!Serial);

  usb.begin();
  delay(100);
  Serial.println("Press Enter to begin testing");
}

static const char* const sense_keys[16] = {
  "0 (No Sense)",
  "1 (Recovered Error)",
  "2 (Not Ready)",
  "3 (Medium Error)",
  "4 (Hardware Error)",
  "5 (Illegal Request)",
  "6 (Unit Attention)",
  "7 (Data Protect)",
  "8 (Blank Check)",
  "9 (Vendor Specific)",
  "10 (Copy Aborted)",
  "11 (Aborted Command)",
  "12 (Reserved)",
  "13 (Volume Overflow)",
  "14 (Miscompare)",
  "15 (Completed)"
};

static void check_sense(USB_Storage *usbms, uint8_t lun) {
  basic_sense_data sense;
  if (usbms->request_sense(lun, &sense, sizeof(sense)) >= 8) {
    if (sense.response_code==0x70) {
      Serial.print("\tSENSE KEY: ");
      Serial.println(sense_keys[sense.sense_key]);
      if (sense.valid) {
        Serial.printf("\tINFORMATION %02X %02X %02X %02X\n", sense.information[0], sense.information[1], \
          sense.information[2], sense.information[3]);
      }
      Serial.printf("\tADDITIONAL SENSE LENGTH: %u\n", sense.additional_sense_length);
      if (sense.additional_sense_length >=6) {
        Serial.printf("\tCOMMAND-SPECIFIC INFORMATION: %02X %02X %02X %02X\n", sense.command_specific_information[0], \
          sense.command_specific_information[1], sense.command_specific_information[2], sense.command_specific_information[3]);
        Serial.printf("\tADDITIONAL SENSE CODE: %02Xh\n", sense.additional_sense_code);
        Serial.printf("\tADDITIONAL SENSE CODE QUALIFIER: %02Xh\n", sense.additional_sense_code_qualifier);
      }
      else Serial.println("No addition sense information available");
    }
    else Serial.printf("Unexpected sense response code: %02X\n", sense.response_code);
  }
  else Serial.printf("Failed to retrieve sense data: %d\n", errno);
}

#define TEST_SIZE 65536
static void run_test(USB_Storage *usbms, uint8_t lun, uint64_t sectors, uint32_t sector_size) {
  static DMAMEM uint8_t test_buf[TEST_SIZE] __attribute__((aligned(32)));
  const uint32_t cnt = sizeof(test_buf) / sector_size;
  const size_t test_len = cnt * sector_size;
  if (sectors < cnt) {
    Serial.println("insufficient sector count for testing");
    return;
  }

  String vendor = usbms->vendor_name(lun);
  String product = usbms->product_name(lun);

  int bytes_read = 0;
  uint64_t s = 0;
  Serial.printf("Testing %s %s LUN %u, please wait approx. 10 seconds...", vendor.c_str(), product.c_str(), lun);
  elapsedMillis timer = 0;
  do {
    int r = usbms->read(lun, s, cnt, test_buf, test_len);
    if (r != (int)test_len) {
      Serial.printf("Error while reading sector %llu cnt %u: %d %d\n", s, cnt, r, errno);
      if (errno == EBUSY) {
        check_sense(usbms, lun);
      }
      return;
    }
    bytes_read += r;
    s += cnt;
    if (s + cnt > sectors) s = 0;
  } while (timer < 10000);
  uint32_t end_tick = timer;
  Serial.printf("\nTest complete, read %d bytes in %ums (%.2f MB/s)\n\n", bytes_read, end_tick, 1000.0f * bytes_read / (1024.0f * 1024.0f * end_tick));
}

void loop() {
  if (Serial.read() == '\n') {
    size_t i=0;
    USB_Storage* usbms;
    bool did_a_test = false;
    while ((usbms = USB_Storage::open_device(i++)) != NULL) {
      for (uint8_t lun = 0; lun < usbms->get_lun_count(); lun++) {
        uint64_t sectors;
        uint32_t sector_size;
        // test twice
        if (usbms->lun_ready(lun, sectors, sector_size) < 0 || sectors==0) {
          delay(100);
          if (usbms->lun_ready(lun, sectors, sector_size) < 0 || sectors==0)
            continue;
        }
        Serial.printf("LUN %d: %llu sectors of %u bytes each\n", lun, sectors, sector_size);
        run_test(usbms, lun, sectors, sector_size);
        did_a_test = true;
      }
      usbms->close();
    }
    if (!did_a_test) Serial.println("Failed to find a USB drive to test, press Enter to try again");
  }
}
