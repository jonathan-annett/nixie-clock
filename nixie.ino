
#include "Arduino.h"
#include "SD_MMC.h"
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include "time.h"
#include "nixies.h"
#include "pin_config.h"

#include "bootloader_random.h"

#include <FastLED.h> // https://github.com/FastLED/FastLED
#include "TFT_eSPI.h" // https://github.com/Bodmer/TFT_eSPI


#define SD_MISO         14
#define SD_MOSI         17
#define SD_SCLK         12
#define SD_CS           16

#define DIGIT_10H 5
#define DIGIT_HOUR 4
#define DIGIT_10MIN 3
#define DIGIT_MIN 2
#define DIGIT_10SEC 1
#define DIGIT_SEC 0

#ifndef DIGIT_MODE
#define DIGIT_MODE DIGIT_10H
#endif

#ifdef FIRMWARE_VERSION
String firmwareVersion = FIRMWARE_VERSION;  // current firmware version
#else
String firmwareVersion = "v1.0.0";  // current firmware version
#endif

const char* versionURL  = "https://raw.githubusercontent.com/jonathan-annett/nixie-clock/refs/heads/main/firmware/version.txt";
const char* firmwareURL = "https://raw.githubusercontent.com/jonathan-annett/nixie-clock/refs/heads/main/firmware/firmware.bin";

//#define MAX_SYNC_MILLIS (1000 * 60 * 60 * 2)
const unsigned long MAX_SYNC_MILLIS [ 6 ] = {

(unsigned long )  (1000 * 60 * 60 * 1),   // 1 secs
(unsigned long )  (1000 * 60 * 60 * 1.5),   // 10 secs

(unsigned long )  (1000 * 60 * 60 * 3),   // mins
(unsigned long )  (1000 * 60 * 60 * 6),   // 10 mins

(unsigned long )  (1000 * 60 * 60 * 7),   // hours
(unsigned long )  (1000 * 60 * 60 * 9)    // 10 hours

};

const unsigned cylonPos [6] = {
   0x0001,
   0x0002 | 0x0200,
   0x0004 | 0x0100,
   0x0008 | 0x0080,
   0x0010 | 0x0040,
   0x0020
} ;


int useDigitMode = DIGIT_MODE;
String savedSSID = "";
String savedPass = "";
String ntpServer = "pool.ntp.org";


 long  gmtOffset_sec = 60 * 60 * 10 ;
 int   daylightOffset_sec = 0;//3600;

 bool useLeds = true;

unsigned lastSync ;

int lastSec = -1;
struct tm timeinfo;

CRGB leds;

Preferences preferences;

TFT_eSPI tft = TFT_eSPI();

int currentDigit;

unsigned long millisSync = 0;

void cylonLeds() {

  struct timeval now;
  gettimeofday(&now, nullptr);

  unsigned xxx =  now.tv_usec / 100000;

  unsigned mask =  1 << (xxx % 10) ;

  if ( (cylonPos [ useDigitMode ] & mask )== 0 ) {
     FastLED.show(0);
  } else {
    FastLED.show();
  }
}

void checkForOTAUpdate() {
  
  HTTPClient http;
  u16_t r;


  char url [256];
  bootloader_random_enable();
  bootloader_fill_random(&r, sizeof(r));
  sprintf(url,"%s?%u",versionURL,r);

  Serial.printf("Hitting url: %s\n", url);

  http.begin(url);
  int httpCode = http.GET();

  Serial.printf("Installed version: %s\n", firmwareVersion.c_str());

  if (httpCode == 200) {
    String remoteVersion = http.getString();
    remoteVersion.trim();

    
    Serial.printf("Remote version: %s\n", remoteVersion.c_str());

    if (remoteVersion.compareTo( firmwareVersion ) != 0 ) {
      Serial.println("New version found, starting OTA update...");
      doFirmwareUpdate();
    } else {
      Serial.println("Firmware is up to date.");
    }
  } else {
    Serial.printf("Failed to check version, HTTP code: %d\n", httpCode);
  }
  http.end();
}


void doFirmwareUpdate() {
  static uint8_t hue = 0;
  HTTPClient http;

  uint8_t buffer [512];

  http.begin(firmwareURL);
  int httpCode = http.GET();

  if (httpCode == 200) {
    size_t contentLength = http.getSize();

    Serial.printf("HTTP Content-Length = %d\n", contentLength);
    size_t remain = contentLength;

    WiFiClient &str = http.getStream();

  
    Serial.printf("Update Starting (%u bytes)\n",contentLength);

 
    if (Update.begin(contentLength)) {

      while (remain > 0) {

        leds = CHSV(hue++, 0XFF, 100);
        FastLED.show();

        size_t toRead =  remain > sizeof buffer ? sizeof buffer : remain;
        size_t avail = str.available();
  
        while (avail == 0) {
          delay (1);
          leds = CHSV(hue++, 0XFF, 100);
          FastLED.show();
          avail = str.available();
        }
  
        size_t didRead = str.read(buffer,toRead);
       // Serial.printf("did read = %d, remain = %d\n", didRead, remain);

        if (didRead>0) {

          size_t didWrite = Update.write(buffer,didRead);

          if (Update.hasError()) {
            Serial.printf("Update error after write: %s\n", Update.errorString());
            break;
          }


       //   Serial.printf("did write = %d \n", didWrite);
          if ( didRead ==  didWrite) {
            remain -= didRead;      

          } else {
            Serial.println("(error?)");
            break; 
          }
        }

      }
  
      
      if (remain == 0) {
        if (Update.end()) {
          ESP.restart();
        } else {
          Serial.printf("Update error: %s\n", Update.errorString());
        }
      } else {
        Serial.printf("aborting update, with %d bytes left unprocessed\n", remain);
        Update.abort();
      }
          
    } else {
      Serial.println("Update failed: insufficient space");
    }
  } else {
    Serial.printf("Firmware download failed: %d\n", httpCode);
  }
  http.end();
}



void savePrefs() {
  preferences.begin("clock", false);
  preferences.putString("ssid",savedSSID);
  preferences.putString("pass",savedPass);
  preferences.putInt("digit",useDigitMode);
  preferences.putLong("tz-offset",gmtOffset_sec);
  preferences.putInt("daylight-offset",daylightOffset_sec);
  preferences.putBool("useLeds",useLeds);
  preferences.putString("ntp-server",ntpServer);
  preferences.end(); 

  Serial.printf("Saved Preferences:\nSSID: %s\nPASS: %s\nNTP Server:%s\nDigit Mode: %d\n", 
    savedSSID.c_str(), 
    savedPass.c_str(), 
    ntpServer.c_str(),
    useDigitMode
  );

}

void loadPrefs() {
  preferences.begin("clock", false);
  savedSSID = preferences.getString("ssid",savedSSID);
  savedPass = preferences.getString("pass",savedPass);
  ntpServer = preferences.getString("ntp-server",ntpServer);
  useDigitMode =  preferences.getInt("digit",useDigitMode);
  useLeds      =  preferences.getBool("useLeds",useLeds);

  gmtOffset_sec = preferences.getLong("tz-offset",gmtOffset_sec);
  daylightOffset_sec = preferences.getInt("daylight-offset",daylightOffset_sec);

  preferences.end();

  Serial.printf("Loaded Preferences:\nSSID: %s\nPASS: %s\nNTP Server:%s\nDigit Mode: %d\n", 
    savedSSID.c_str(), 
    savedPass.c_str(), 
    ntpServer.c_str(),
    useDigitMode 
  );
}

void getSettings(void) {

  Serial.println("Attempting Card Mount...");
  SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN, SD_MMC_D1_PIN, SD_MMC_D2_PIN, SD_MMC_D3_PIN);
  if (!SD_MMC.begin()) {
    Serial.println("Card Mount Failed");
    loadPrefs(); 
    return;
  }
  
  delay (5000);

  File file = SD_MMC.open("/config.json");
  if (!file) {
    Serial.println("Failed to open config.json");
    loadPrefs();
    return;
  }

  delay (5000);

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.f_str());
    loadPrefs();
    return;
  }

  const char * ssid  = doc["wifi"]["ssid"];
  const char * pass  = doc["wifi"]["password"];
  const char * ntp   = doc["settings"]["ntp-server"];

  gmtOffset_sec = doc["settings"][ "tz-offset" ];
  daylightOffset_sec = doc["settings"][ "daylight-offset" ];

  useDigitMode = doc["settings"]["digit"];

  useLeds = doc["settings"]["useLeds"];

  savedSSID = (String) ssid;
  savedPass = (String) pass;
  ntpServer = (String) ntp;

  savePrefs();

}
 
int getDigit() {


    if(!getLocalTime(&timeinfo)){
      return -1;
    }

    if (lastSec!=timeinfo.tm_sec ) {
      lastSec = timeinfo.tm_sec;
      printLocalTime(); 
      cylonLeds();
    }

   switch ( useDigitMode ) {
    
    case DIGIT_10H:
      return timeinfo.tm_hour / 10;
    
    case DIGIT_HOUR:
      return timeinfo.tm_hour % 10;

    case DIGIT_10MIN:
      return timeinfo.tm_min / 10;

    case DIGIT_MIN:
      return timeinfo.tm_min % 10;

    case DIGIT_10SEC:
      return timeinfo.tm_sec / 10;

    case DIGIT_SEC:
      return timeinfo.tm_sec % 10;
   
   default:
       return -1;
   }
}

void printLocalTime() {
  Serial.println( &timeinfo, "%A, %B %d %Y %H:%M:%S") ;
}


void setup() {

  Serial.begin(115200);

  if (useLeds) {
    FastLED.addLeds<APA102, LED_DI_PIN, LED_CI_PIN, BGR>(&leds, 1);
    leds = CHSV(0, 0XFF, 100);
    FastLED.show();
}
  

  getSettings(); 

  delay(5000);

  Serial.printf("Connecting to %s ", savedSSID);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println(" CONNECTED");

  checkForOTAUpdate();
  
  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str() );

  getLocalTime(&timeinfo);

  printLocalTime();

  //disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  pinMode(TFT_LEDA_PIN, OUTPUT);
  // Initialise TFT
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  digitalWrite(TFT_LEDA_PIN, 0);

  currentDigit = getDigit();
  if ( currentDigit >= 0 ) {
    tft.pushImage(0, 0, 160, 80, (uint16_t *)nixies[ currentDigit ]);
  } else {
    Serial.println(" BAD digit");

  }
  lastSync = millis();
}

void resync () {


  Serial.printf("Connecting to %s ", savedSSID);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());

        
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println(" CONNECTED");

//init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer.c_str() );
  printLocalTime();
 
  //disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  lastSync = millis();

}

void loop() { // Put your main code here, to run repeatedly:
  int nextDigit = getDigit() ;
  static uint8_t hue = 0;

  while( currentDigit == nextDigit ) {
 

    if ( millis() - lastSync >  MAX_SYNC_MILLIS [ useDigitMode ] ) {
      resync();
    } else {
      delay(1);
       
    }

  
    nextDigit = getDigit() ;
  }
  if ( nextDigit >= 0 ) {
    tft.pushImage(0, 0, 160, 80, (uint16_t *)nixies[ nextDigit ]);
  }
  currentDigit = nextDigit;


}
