// ElTech-Online ESP32 Data Logger — SD card test
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// The smallest useful sketch for the microSD module: it starts the card, adds
// one line to a file, then reads the whole file back to Serial Monitor. No
// sensor, no display, no WiFi. Each time you press the board's RST button the
// file grows by one line, which shows that data on the card survives a restart.
//
// Libraries: none to install. SD and SPI are built into the ESP32 board package.
//
// Wiring (6 wires). This module is 3.3 V ONLY: never connect it to 5V.
//   module 3V3  -> ESP32-C3 3V3
//   module GND  -> ESP32-C3 GND
//   module CLK  -> ESP32-C3 GPIO 4
//   module MISO -> ESP32-C3 GPIO 5
//   module MOSI -> ESP32-C3 GPIO 6
//   module CS   -> ESP32-C3 GPIO 7
//
// The card must be formatted FAT32 (cards of 32 GB or smaller usually are).

#include <SPI.h>
#include <SD.h>

#define SD_SCK  4
#define SD_MISO 5
#define SD_MOSI 6
#define SD_CS   7

// setup() runs once, when the board is powered on or reset.
void setup() {
  Serial.begin(115200);
  delay(2000);   // time to open Serial Monitor

  // Tell the SPI bus which pins to use, then start the card.
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("Card NOT found. Check the wiring and that a card is pushed in.");
    return;   // leave setup() early: nothing more can be done
  }
  Serial.println("Card found.");

  // Open the file for adding to the end (it is created if it doesn't exist),
  // write one line, and close it. Closing is what saves it to the card.
  File file = SD.open("/test.txt", FILE_APPEND);
  file.println("Hello from the ESP32");
  file.close();

  // Open the same file for reading and print it one character at a time.
  // file.available() says how many characters are still to come.
  Serial.println("--- test.txt now contains ---");
  file = SD.open("/test.txt", FILE_READ);
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println("--- end of file ---");
}

// loop() runs over and over, forever. Everything happened in setup(), so it
// has nothing to do.
void loop() {
}
