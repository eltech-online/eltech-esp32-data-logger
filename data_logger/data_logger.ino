// ElTech-Online ESP32 Data Logger — records temperature, humidity and air
// pressure to a microSD card as a spreadsheet file, with an OLED readout and a
// web page to download the file from.
//
// BETA: this sketch compiles but has not been fully tested on hardware yet.
//
// The technique this kit teaches:
//   - microSD card module -> the SPI BUS (four wires) and FILES: creating,
//     appending to, reading and deleting a file on a memory card
// The sensor and the OLED share the I2C bus, as in the Weather Station kit.
//
// Libraries needed (Arduino IDE Library Manager):
//   Adafruit AHTX0
//   Adafruit BMP280 Library
//   Adafruit SH110X
//   Adafruit GFX Library
// (click "Install all" if it offers dependencies. SD, SPI, WiFi and WebServer
// are built into the ESP32 board package — no separate install)
// Board package: esp32 by Espressif Systems
//
// How to use:
//   1. Put a microSD card (FAT32 formatted, 32 GB or smaller) in the module.
//   2. Flash this sketch. It starts logging one row a minute to log.csv.
//   3. The OLED shows the board's WiFi name and password. Join that network
//      with your phone or laptop and open http://192.168.4.1
//   4. Opening the page also sets the board's clock from your phone, so the
//      rows get real dates and times. "Download" saves the file.
// Full source, wiring diagram and setup guide: github.com/eltech-online/eltech-esp32-data-logger
//
// ---------------------------------------------------------------------------
// New to Arduino code? How to read this file
// ---------------------------------------------------------------------------
// Lines starting with // are comments: notes for people, ignored by the board.
// The file is in this order, and you can read it top to bottom:
//   1. Settings      - pin numbers and the file name, safe to change
//   2. Clock         - getting the date and time from your phone
//   3. SD card       - starting the card and writing a row to the file
//   4. Web server    - what the board sends to your browser
//   5. setup()       - runs ONCE when the board is powered on
//   6. loop()        - then runs over and over, forever
//   7. OLED screen   - drawing the readings on the display
//   8. Self-test     - checking every part works at power-on
// A good first experiment: change DEFAULT_INTERVAL_SECONDS below and upload.

#include <WebServer.h>
#include <Preferences.h>   // saves the logging interval in flash
#include <Wire.h>          // the I2C bus (sensor + OLED)
#include <SPI.h>           // the SPI bus (SD card)
#include <SD.h>            // files on the SD card
#include <sys/time.h>      // the board's clock
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ---- WiFi Access Point settings ----
// Leave both empty ("") and every board gets its OWN network name (e.g.
// "ElTech-DL-A3F2") and its OWN random 8-character password, saved in flash.
// Or type your own: name up to 32 characters, password 8-63 characters.
const char* AP_SSID     = "";
const char* AP_PASSWORD = "";
const bool  AP_OPEN_NETWORK = false;  // true = no password at all
#define KIT_SSID_PREFIX "ElTech-DL-"
#define KIT_PREFS       "logger"      // name of this kit's flash "notebook"
#include "eltech_wifi.h"     // starts the WiFi network (second tab in the IDE)
#include "logo_bitmap.h"     // shop logo bitmap for the OLED splash screen
#include "page_template.h"   // the web page's HTML

// ---- I2C pins (sensor + OLED) ----
#define I2C_SDA 8
#define I2C_SCL 9
#define OLED_ADDR   0x3C   // try 0x3D if the screen stays blank
#define BMP280_ADDR 0x77   // the sketch also tries 0x76 by itself

// ---- SPI pins (SD card) ----
// SPI uses four wires. Three are shared by every SPI part on the bus:
//   SCK  = the clock, ticking once per bit
//   MOSI = data from the board to the card ("Master Out, Slave In")
//   MISO = data from the card to the board ("Master In, Slave Out")
// and each part has its own CS ("Chip Select") wire: the board pulls it LOW to
// say "I am talking to YOU now". These four are the ESP32-C3 SuperMini's
// labelled SPI pins.
#define SD_SCK  4   // card module CLK
#define SD_MISO 5   // card module MISO
#define SD_MOSI 6   // card module MOSI
#define SD_CS   7   // card module CS

// ---- Logging ----
const char* LOG_FILE = "/log.csv";         // file names on the card start with /
const int DEFAULT_INTERVAL_SECONDS = 60;   // one row a minute (changeable on the web page)

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);

// Whether each part answered, so the code can skip a missing one.
bool oledOK = false, ahtOK = false, bmpOK = false, sdOK = false;
bool selfTestPassed = false;
String sdDetail = "";            // card size, or why it failed

// The latest readings. NAN means "not a number": no value yet.
float g_temperature = NAN, g_humidity = NAN, g_pressure = NAN;

int intervalSeconds = DEFAULT_INTERVAL_SECONDS;
unsigned long lastLogMs = 0;     // millis() when the last row was written
unsigned long rowsLogged = 0;    // rows written since power-on
String lastRow = "";             // the last row written, shown on the web page

void centerText(const String& text, int y, int textSize);
void drawDataScreen();
bool runSelfTest();

// ---------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------
// The ESP32 can count time very well, but at power-on it has no idea what the
// date is: it has no battery-backed clock. Your phone knows, so the web page
// sends the phone's time to the board each time it is opened. Until then, rows
// are stamped with the time since power-on instead.
bool clockSet = false;

// The page sends: seconds since 1 Jan 1970 (the usual way computers count
// time, called "epoch" or "Unix time") and the phone's time zone offset in
// minutes. Adding the offset makes the board's clock show local time.
void setClock(long long epochSeconds, int offsetMinutes) {
  struct timeval tv;
  tv.tv_sec = epochSeconds + offsetMinutes * 60;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  clockSet = true;
}

// The time as text for a row of the file: "2026-10-06 14:03:20" once the clock
// is set, or "boot+00:12:45" before.
String timeStamp() {
  char text[24];
  if (clockSet) {
    time_t now = time(NULL);
    struct tm parts;
    gmtime_r(&now, &parts);   // splits the seconds into year, month, day, hour...
    strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &parts);
  } else {
    unsigned long s = millis() / 1000;
    snprintf(text, sizeof(text), "boot+%02lu:%02lu:%02lu", s / 3600, (s / 60) % 60, s % 60);
  }
  return String(text);
}

// ---------------------------------------------------------------------------
// SD card
// ---------------------------------------------------------------------------

// Starts the card and returns true if one is there and readable.
bool startCard() {
  SD.end();                                       // in case this is a retry
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    sdDetail = "no card or bad wiring";
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    sdDetail = "no card";
    return false;
  }
  // cardSize() is in bytes; dividing by 1024 twice gives megabytes.
  sdDetail = String((unsigned long)(SD.cardSize() / (1024 * 1024))) + " MB card";
  return true;
}

// Adds one row to the end of the log file. Returns true if it was written.
//
// The file is opened, written and CLOSED again for every row. Closing is what
// actually saves the data to the card, so you can pull the power at any moment
// and lose at most the row being written.
//
// CSV means "comma-separated values": plain text, one row per line, commas
// between the columns. Any spreadsheet program opens it.
bool logRow() {
  if (!sdOK) return false;
  bool isNewFile = !SD.exists(LOG_FILE);
  File file = SD.open(LOG_FILE, FILE_APPEND);     // FILE_APPEND = add to the end
  if (!file) {
    sdOK = false;                                 // card pulled out? loop() retries
    sdDetail = "write failed";
    return false;
  }
  if (isNewFile) file.println("time,temperature_c,humidity_pct,pressure_hpa");   // column titles

  lastRow = timeStamp() + "," + String(g_temperature, 1) + "," + String(g_humidity, 1) + "," + String(g_pressure, 1);
  file.println(lastRow);
  file.close();
  rowsLogged++;
  Serial.println("Logged: " + lastRow);
  return true;
}

// The log file's size in bytes (0 if there is no file).
unsigned long logFileSize() {
  if (!sdOK || !SD.exists(LOG_FILE)) return 0;
  File file = SD.open(LOG_FILE, FILE_READ);
  unsigned long size = file.size();
  file.close();
  return size;
}

// ---------------------------------------------------------------------------
// Web server
// ---------------------------------------------------------------------------
// A browser asks the board for an address, and the matching function below
// sends the answer. setup() connects each address to its function.

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "text/html", PAGE_TEMPLATE);
}

void handleStyle() {
  server.send(200, "text/css", STYLE_CSS);
}

// "/data" -> the readings and the logger's status as JSON, a simple text
// format programs can read. The page asks for it every 2 seconds.
void handleData() {
  String json = "{";
  json += "\"temp\":\"" + (ahtOK ? String(g_temperature, 1) + " °C" : String("n/a")) + "\",";
  json += "\"hum\":\""  + (ahtOK ? String(g_humidity, 1) + " %" : String("n/a")) + "\",";
  json += "\"pres\":\"" + (bmpOK ? String(g_pressure, 0) + " hPa" : String("n/a")) + "\",";
  json += "\"card\":" + String(sdOK ? "true" : "false") + ",";
  json += "\"card_detail\":\"" + sdDetail + "\",";
  json += "\"rows\":" + String(rowsLogged) + ",";
  json += "\"size\":" + String(logFileSize()) + ",";
  json += "\"interval\":" + String(intervalSeconds) + ",";
  json += "\"clock_set\":" + String(clockSet ? "true" : "false") + ",";
  json += "\"clock\":\"" + timeStamp() + "\",";
  json += "\"last\":\"" + lastRow + "\",";
  json += "\"selftest\":\"" + String(selfTestPassed ? "PASS" : "FAIL") + "\"}";
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.send(200, "application/json", json);
}

// "/download" -> the whole log file. The Content-Disposition header tells the
// browser to save it as a file instead of showing it. streamFile() sends it in
// small pieces, so even a file far bigger than the board's memory works.
void handleDownload() {
  if (!sdOK || !SD.exists(LOG_FILE)) {
    server.send(404, "text/plain", "No log file yet.");
    return;
  }
  File file = SD.open(LOG_FILE, FILE_READ);
  server.sendHeader("Content-Disposition", "attachment; filename=\"log.csv\"");
  server.streamFile(file, "text/csv");
  file.close();
}

// "/clear" -> deletes the log file. The next row starts a new one.
void handleClear() {
  if (sdOK) SD.remove(LOG_FILE);
  rowsLogged = 0;
  lastRow = "";
  handleData();
}

// "/interval?s=30" -> how often a row is written, saved in flash.
void handleInterval() {
  intervalSeconds = constrain(server.arg("s").toInt(), 2, 3600);
  Preferences prefs;
  prefs.begin(KIT_PREFS, false);
  prefs.putInt("interval", intervalSeconds);
  prefs.end();
  handleData();
}

// "/settime?epoch=1790000000&offset=60" -> sets the clock from the phone.
void handleSetTime() {
  setClock(atoll(server.arg("epoch").c_str()), server.arg("offset").toInt());
  handleData();
}

// ---------------------------------------------------------------------------
// setup() runs once at power-on, loop() then runs forever
// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL);

  oledOK = display.begin(OLED_ADDR, true);
  ahtOK = aht.begin();
  bmpOK = bmp.begin(BMP280_ADDR) || bmp.begin(0x76);
  sdOK = startCard();

  if (oledOK) {
    display.setTextColor(SH110X_WHITE);   // required, or no text is drawn
    display.setTextWrap(false);
  }

  Preferences prefs;
  prefs.begin(KIT_PREFS, true);           // true = read only
  intervalSeconds = constrain(prefs.getInt("interval", DEFAULT_INTERVAL_SECONDS), 2, 3600);
  prefs.end();

  Serial.println("========================================");
  Serial.println("           ElTech-Online");
  Serial.println("     ESP32 Data Logger (BETA)");
  Serial.println("========================================");

  wifiOK = startAccessPoint();
  selfTestPassed = runSelfTest();
  printWifiDetails();

  server.on("/", handleRoot);
  server.on("/style.css", handleStyle);
  server.on("/data", handleData);
  server.on("/download", handleDownload);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/interval", HTTP_POST, handleInterval);
  server.on("/settime", HTTP_POST, handleSetTime);
  server.begin();
  readSensors();   // so the very first row of the log has real numbers in it

  if (oledOK) {
    display.clearDisplay();
    display.drawBitmap((SCREEN_WIDTH - LOGO_WIDTH) / 2, 0, logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, SH110X_WHITE);
    centerText("ElTech-Online", 36, 1);
    centerText("Data Logger", 48, 1);
    display.display();
    delay(2000);
    if (wifiOK) {
      display.clearDisplay();
      centerText("Connect to WiFi:", 0, 1);
      centerText(apSsid, 12, 1);
      centerText(AP_OPEN_NETWORK ? String("(open network)") : "Pass: " + apPassword, 24, 1);
      centerText("then open:", 36, 1);
      centerText(apUrl, 48, 1);
      display.display();
      delay(6000);
    }
  }
}

// Takes one reading from each sensor into the g_ variables.
void readSensors() {
  if (ahtOK) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    g_temperature = temp.temperature;
    g_humidity = humidity.relative_humidity;
  }
  if (bmpOK) g_pressure = bmp.readPressure() / 100.0F;   // pascals -> hectopascals
}

void loop() {
  server.handleClient();   // answer any browser that's waiting

  // Read the sensors every 2 seconds WITHOUT stopping the web server.
  // millis() is the number of milliseconds since the board started; we note
  // when we last read and only read again once 2000 ms have gone by.
  // ("static" makes lastRead keep its value between runs of loop().)
  static unsigned long lastRead = 0;
  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    readSensors();
    if (oledOK) drawDataScreen();
  }

  // Write a row every intervalSeconds. The same millis() pattern, with a
  // different gap. The "|| lastLogMs == 0" makes the first row happen at once.
  if (millis() - lastLogMs >= (unsigned long)intervalSeconds * 1000 || lastLogMs == 0) {
    lastLogMs = millis();
    if (lastLogMs == 0) lastLogMs = 1;
    logRow();
  }

  // No card (or it was pulled out)? Try to start it again every 5 seconds, so
  // a card can be swapped without restarting the board.
  static unsigned long lastCardTry = 0;
  if (!sdOK && millis() - lastCardTry >= 5000) {
    lastCardTry = millis();
    sdOK = startCard();
    if (sdOK) Serial.println("SD card found: " + sdDetail);
  }
}

// The live data screen:
//
//   y=0   WiFi: ElTech-DL-A3F2      <- network name
//   y=9   Pass: abcd2345            <- password
//   y=20      21.4C                 <- big temperature (text size 2)
//   y=38  48%          1013hPa      <- humidity and pressure
//   y=47  SD: 128 rows  next 42s    <- the logger
//   y=56  http://192.168.4.1        <- the address to open in your browser
void drawDataScreen() {
  display.clearDisplay();

  if (wifiOK) {
    String nameLine = "WiFi: " + apSsid;
    centerText(nameLine.length() <= 21 ? nameLine : apSsid, 0, 1);
    String passLine = AP_OPEN_NETWORK ? String("Open network") : "Pass: " + apPassword;
    centerText(passLine.length() <= 21 ? passLine : apPassword, 9, 1);
  } else {
    centerText("WiFi FAILED", 0, 1);
    centerText(wifiError, 9, 1);
  }
  display.drawLine(0, 18, SCREEN_WIDTH, 18, SH110X_WHITE);

  centerText(ahtOK ? String(g_temperature, 1) + "C" : String("n/a"), 21, 2);

  display.setTextSize(1);
  display.setCursor(0, 38);
  display.print(ahtOK ? String(g_humidity, 0) + "%" : String("--"));
  String pressure = bmpOK ? String(g_pressure, 0) + "hPa" : String("--");
  display.setCursor(SCREEN_WIDTH - pressure.length() * 6, 38);   // 6 pixels per character
  display.print(pressure);

  display.setCursor(0, 47);
  if (sdOK) {
    long nextIn = intervalSeconds - (long)((millis() - lastLogMs) / 1000);
    display.print("SD: " + String(rowsLogged) + " rows");
    String next = "next " + String(max(nextIn, 0L)) + "s";
    display.setCursor(SCREEN_WIDTH - next.length() * 6, 47);
    display.print(next);
  } else {
    display.print("SD: NO CARD");
  }

  if (wifiOK) centerText(apUrl, 56, 1);
  display.display();
}

// Draws `text` horizontally centered at the given y, for the given text size.
// Each character of the default font is 6 pixels wide at size 1.
void centerText(const String& text, int y, int textSize) {
  display.setTextSize(textSize);
  int x = (SCREEN_WIDTH - (int)text.length() * 6 * textSize) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

// Checks each part is connected, prints the result to Serial, and returns true
// only if everything passed.
bool runSelfTest() {
  Serial.println("--- Self-test ---");
  Serial.print("OLED (SH1106): "); Serial.println(oledOK ? "OK" : "NOT FOUND");
  Serial.print("AHT20:         "); Serial.println(ahtOK ? "OK" : "NOT FOUND");
  Serial.print("BMP280:        "); Serial.println(bmpOK ? "OK" : "NOT FOUND");
  Serial.print("SD card:       ");
  if (sdOK) { Serial.print("OK ("); } else { Serial.print("NOT FOUND ("); }
  Serial.print(sdDetail); Serial.println(")");
  Serial.print("WiFi AP:       ");
  if (wifiOK) { Serial.println("OK"); } else { Serial.print("FAILED ("); Serial.print(wifiError); Serial.println(")"); }

  bool passed = oledOK && ahtOK && bmpOK && sdOK && wifiOK;
  Serial.print("RESULT:        "); Serial.println(passed ? "PASS" : "FAIL");
  return passed;
}
