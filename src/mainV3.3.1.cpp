// ============ CONST ============
#define DHT_PIN D5
#define BUZ_PIN D6
#define LED_PIN D7
#define LED_IN_SEGMENT 3          //  WS2811 - 1,  WS2812 - 3

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "DHT.h"
#include <GyverDS3231.h>
#include <LittleFS.h>
#include <GyverDBFile.h>
#include <FileData.h>
#include <GyverNTP.h>
#include <SettingsGyver.h>
#include <ESP8266WiFi.h>

GyverDS3231 rtc(1);

DHT dht(DHT_PIN, DHT11);

Adafruit_NeoPixel CL(29 * LED_IN_SEGMENT, LED_PIN, NEO_GRB + NEO_KHZ800);

GyverDBFile db(&LittleFS, "/WiFi.set", 1000);

SettingsGyver sett("Smart School Clock", &db);

// ============ DATA ============
DB_KEYS(sta, ssid, pass, apPass, webPass);

struct {
  uint32_t workStartTime = 8 * 3600;
  uint32_t workEndTime = 18 * 3600;
  uint32_t lessons[20];
  uint8_t bright = 10;
  uint8_t numberOfLessons = 4;
  bool buzzerStatus = 1;
  float buzzerLength = 4;
  bool activeDays[7] = {1, 1, 1, 1, 1, 0, 0};
  uint32_t colorTime = 0x0000ff;
  uint32_t colorLesson = 0xff0000;
  uint32_t colorBreak = 0x00ff00;
  uint32_t colorDht = 0xff7a00;
  uint8_t timeZome = 1;
  bool power = 1;
  uint8_t DS3231Length = 1;
  uint8_t lesLength = 2;
  bool nineNotBuz = 0;
}set;

FileData set_f(&LittleFS, "/Settings.set", 'a', &set, sizeof(set), 1000);

int8_t snakeStep = -1;
uint32_t snakePrevMillis, dataTime = 1772352000, BuzOnTime;
bool ntpSynced, update = 1, buzTest;

uint8_t digits[10] = {             //  BYTE MASKS OF DIGITS
  0b00111111, // 0
  0b00100001, // 1
  0b01110110, // 2
  0b01110011, // 3
  0b01101001, // 4
  0b01011011, // 5
  0b01011111, // 6
  0b00110001, // 7
  0b01111111, // 8
  0b01111011  // 9
};

// ============ DISPLAYING DIGITS ON INDICATORS ============
void drawDigit(uint8_t digit, uint8_t offsetDigit, uint32_t color) {
  digit = constrain(digit, 0, 9);

  CL.fill(color, offsetDigit, 7 * LED_IN_SEGMENT);
  for (uint8_t i = 0; i < 7; i++) {delay(0);
    if (!(digits[digit] & (1 << (6 - i)))) CL.fill(0, offsetDigit + i * LED_IN_SEGMENT, LED_IN_SEGMENT);
  }
}

// ============ DISPLAYING 2-DIGITAL NUMBERS ON INDICATORS ============
void drawNumber(uint8_t number, uint8_t offsetNumber, uint32_t color) {
  number = constrain(number, 0, 99);

  drawDigit(number / 10, offsetNumber, color);
  drawDigit(number % 10, offsetNumber + 7 * LED_IN_SEGMENT, color);
}

// ============ SNAKE ============
void snake() {
  if (snakeStep == -1 or millis() - snakePrevMillis < 50) return;
  snakePrevMillis = millis(); delay(0);

  randomSeed(millis());
  
  CL.fill(CL.Color(random(0, 11) * 25, random(0, 11) * 25, random(0, 11) * 25), snakeStep * LED_IN_SEGMENT, LED_IN_SEGMENT);
  CL.fill(0, (snakeStep - 3) * LED_IN_SEGMENT, LED_IN_SEGMENT);
  CL.show();
  delay(0);

  snakeStep++;
  if (snakeStep > 32) snakeStep = -1;
}

// ============ THERMOMETER ============
void Temp(uint8_t temp) {
  drawNumber(temp, 0, set.colorDht);
  CL.fill(set.colorDht, 15 * LED_IN_SEGMENT, 4 * LED_IN_SEGMENT);
  CL.fill(set.colorDht, 24 * LED_IN_SEGMENT, 4 * LED_IN_SEGMENT);
}

// ============ COLON ============
void blinc(uint32_t color) {
  if ((millis() / 500) % 2 == 0) CL.fill(color, 14 * LED_IN_SEGMENT, LED_IN_SEGMENT);
  else CL.fill(0, 14 * LED_IN_SEGMENT, LED_IN_SEGMENT);
  if (LED_IN_SEGMENT == 3) CL.setPixelColor(43, 0);
}

// ============ CLOCK ============
void DS3231() {
  Datime now(rtc);

  drawNumber(now.hour + set.timeZome, 0, set.colorTime);
  drawNumber(now.minute, 15 * LED_IN_SEGMENT, set.colorTime);
  
  blinc(set.colorTime);
}

// ============ CHECKING LESSON/BREAK DURATION ============
void lessonsTest() {
  for (int i; i <= set.numberOfLessons * 2; i++){delay(0);
    if (((set.lessons[i + 1] - set.lessons[i]) / 60) > 99) set.lessons[i + 1] = set.lessons[i] + 5940;
  }
  set_f.updateNow();
}

// ============ LESSONS/BREAKS ============
void Lessons() {
  Datime now(rtc);
  uint16_t timeNow = now.hour * 60 + now.minute;
  uint8_t lessonNumber = 0, breakNumber = 0;

  if (timeNow < (set.lessons[0] / 60) or timeNow >= (set.lessons[set.numberOfLessons * 2 + 1] / 60)) {DS3231(); return;}

  for (int i = 0; i <= set.numberOfLessons; i++) {delay(0);
    if (timeNow >= (set.lessons[i * 2] / 60) and timeNow < (set.lessons[i * 2 + 1] / 60)) {lessonNumber = i + 1; break;} else lessonNumber = 0;
    if (i < set.numberOfLessons and timeNow >= (set.lessons[i * 2 + 1] / 60) and timeNow <  (set.lessons[i * 2 + 2] / 60)) {breakNumber = i + 1; break;} else breakNumber = 0;
  }

  if (lessonNumber > 0) {
    if (lessonNumber == 10) {drawNumber(lessonNumber, 0, set.colorLesson); }else {drawDigit(lessonNumber, 0, set.colorLesson);}
    drawNumber((set.lessons[lessonNumber * 2 - 1] / 60) - timeNow, 15 * LED_IN_SEGMENT, set.colorLesson);
    blinc(set.colorLesson);
  } else if (breakNumber > 0) {
    drawDigit(breakNumber, 0, set.colorBreak);
    drawNumber((set.lessons[breakNumber * 2] / 60) - timeNow, 15 * LED_IN_SEGMENT, set.colorBreak);
    blinc(set.colorBreak);
  }
}

// ============ BUZZER ============
void Buzzer(){
  Datime now(rtc);
  uint16_t timeNow = now.hour * 60 + now.minute + 60 * set.timeZome;
  static bool flags[20];

  if (set.nineNotBuz and timeNow == 540) return;

  if (timeNow < (set.lessons[0] / 60) or timeNow > (set.lessons[1 + 2 * set.numberOfLessons] / 60) or update){update = 0; for (int i = 0; i < 20; i++) {flags[i] = 0; delay(0);}}

  for (int i = 0; i < 2 * (set.numberOfLessons + 1); i++) {delay(0); if (timeNow == (set.lessons[i] / 60) and flags[i] == 0 and set.buzzerStatus) {digitalWrite(BUZ_PIN, 1); BuzOnTime = millis(); flags[i] = 1;}}

  if (millis() - BuzOnTime >= set.buzzerLength * 1000) digitalWrite(BUZ_PIN, 0);
}

// ============ NTP SYNCHRONIZATION ============
void ntpSync() {
  if (NTP.online() and NTP.updateNow() and NTP.getUnix() >= 1753340400UL) {
    rtc.setUnix(NTP.getUnix() + 7210);
    ntpSynced = 1; update = 1;
  }
}

// ============ MAIN LOGIC ============
void mainLogic() {
  delay(0);
  NTP.tick();
  rtc.tick();
  Datime now(rtc);
  static uint32_t lastUpdateTime, lastDHTTime;
  static uint8_t currentMode, snakeAllowedFor, temp, tempDHT, errorCod;
  static bool workStatus;
  if (millis() - lastUpdateTime >= 1000) {lastUpdateTime = millis(); delay(0);
    uint16_t timeNow = now.hour * 60 + now.minute + 60 * set.timeZome;
    if (now.year < 2026) {errorCod = 1;}
    if (timeNow >= set.workStartTime / 60 and timeNow + 1 <= set.workEndTime / 60 and set.activeDays[now.weekDay - 1]) workStatus = 1; else workStatus = 0;
    if (now.second == 0) {dataTime = now.getUnix(); dataTime -= 3600 * (2 - set.timeZome);}
  }
  
  if (set.power and workStatus and !errorCod) {delay(0);
	  if (now.second < (1 + set.DS3231Length) * 10) currentMode = 1;
	  else if (now.second < (set.DS3231Length + 1 + set.lesLength) * 10) currentMode = 2;
	  else currentMode = 3;

    if (snakeStep == -1 and snakeAllowedFor != currentMode) {
	    snakePrevMillis = millis();
	    snakeStep = 0;
  	  snakeAllowedFor = currentMode;
    }
    snake();
    
    if (millis() - lastDHTTime >= 1500) {delay(0);
      lastDHTTime = millis();
      tempDHT = dht.readTemperature();
      if (tempDHT < 55) temp = tempDHT;
    }

    if (snakeStep == -1) {delay(0);
      Buzzer();
      switch (currentMode) {
        case 1: CL.fill(0, 0, 29 * LED_IN_SEGMENT); DS3231(); break;
        case 2: CL.fill(0, 0, 29 * LED_IN_SEGMENT); Lessons(); break;
        case 3: CL.fill(0, 0, 29 * LED_IN_SEGMENT); Temp(temp); break;
      }
    }

    CL.setBrightness(set.bright * 2);
  }
  else if(set.power and errorCod != 0) 
  {
    switch (errorCod)
    {
      case 1:
        CL.setBrightness(set.bright * 2);
        CL.fill(0, 0, 174);
        CL.fill(0xff0000, 0, 87);
        CL.fill(0, 6, 6);
        CL.fill(0, 24, 6);
        CL.fill(0, 39, 6);
        CL.fill(0, 48, 3);
        CL.fill(0, 63, 3);
        CL.fill(0, 69, 9);
        CL.fill(0, 81, 6);
      break;
    }
  }else CL.clear();
}

// ============ WEB INTERFACE ============
void build(sets::Builder& web) {

  web.Switch("Power", &set.power);
  if (web.wasSet()) {set_f.updateNow(); web.reload(); web.clearSet();}
  if(!set.power) return;
  {
    web.beginMenu("Schedule");delay(0);
    web.Select("Number of lessons", "1;2;3;4;5;6;7;8;9;10", &set.numberOfLessons);
    {sets::Group g1(web, "1st lesson"); web.Time("Start", &set.lessons[0]); web.Time("End", &set.lessons[1]);}
    if (set.numberOfLessons > 0) {sets::Group g2(web, "2st lesson");web.Time("Start", &set.lessons[2]);web.Time("End", &set.lessons[3]);}
    if (set.numberOfLessons > 1) {sets::Group g3(web, "3st lesson");web.Time("Start", &set.lessons[4]);web.Time("End", &set.lessons[5]);}
    if (set.numberOfLessons > 2) {sets::Group g4(web, "4st lesson");web.Time("Start", &set.lessons[6]);web.Time("End", &set.lessons[7]);}
    if (set.numberOfLessons > 3) {sets::Group g5(web, "5st lesson");web.Time("Start", &set.lessons[8]);web.Time("End", &set.lessons[9]);}
    if (set.numberOfLessons > 4) {sets::Group g6(web, "6st lesson");web.Time("Start", &set.lessons[10]);web.Time("End", &set.lessons[11]);}
    if (set.numberOfLessons > 5) {sets::Group g7(web, "7st lesson");web.Time("Start", &set.lessons[12]);web.Time("End", &set.lessons[13]);}
    if (set.numberOfLessons > 6) {sets::Group g8(web, "8st lesson");web.Time("Start", &set.lessons[14]);web.Time("End", &set.lessons[15]);}
    if (set.numberOfLessons > 7) {sets::Group g9(web, "9st lesson");web.Time("Start", &set.lessons[16]);web.Time("End", &set.lessons[17]);}
    if (set.numberOfLessons > 8) {sets::Group g10(web, "10st lesson");web.Time("Start", &set.lessons[18]);web.Time("End", &set.lessons[19]);}
    web.endMenu();
    if (web.wasSet()) {set_f.updateNow(); web.clearSet(); lessonsTest(); web.reload(); update = 1;}
  }

  {
    web.beginMenu("Date and time");delay(0);
    {
      sets::Group g(web, " ");
      web.DateTime("Date and time", &dataTime);
      if (web.Button("Synchronize")){rtc.setUnix(dataTime + 5 + 3600 * (2 - set.timeZome)); update = 1;}
      web.Select("Seasonal time", "winter;summer", &set.timeZome);
      if (web.Button("Synchronize via NTP")) ntpSync();
      if(web.wasSet()) {set_f.updateNow(); web.clearSet(); web.reload();}
    }
    web.endMenu();
  }

  {
    web.beginMenu("System settings");delay(0);
    {
      sets::Group g(web, "Working time");
      web.Time("Start of work", &set.workStartTime);
      web.Time("End of work", &set.workEndTime);
      web.Switch("Monday", &set.activeDays[0]);
      web.Switch("Tuesday", &set.activeDays[1]);
      web.Switch("Wednesday", &set.activeDays[2]);
      web.Switch("Thursday", &set.activeDays[3]);
      web.Switch("Friday", &set.activeDays[4]);
      web.Switch("Saturday", &set.activeDays[5]);
      web.Switch("Sunday", &set.activeDays[6]);
    }
    {
      sets::Group g2(web, "");delay(0);
      if(web.Switch("Buzzer", &set.buzzerStatus)) {set_f.updateNow(); web.reload();}
      if(set.buzzerStatus) {web.Slider("Buzzer duration", 1, 7, 0.5, " Sec.", &set.buzzerLength);
        web.Switch("Don't buzz at 9:00", &set.nineNotBuz);
        if(web.Switch("Buzzer test", &buzTest)) web.reload();
        if (buzTest){
          if (web.Button("Buzzer")){digitalWrite(D6, 1); BuzOnTime = millis();}
        }
      }
    }
    {
      sets::Group g3(web, "Mode settings");delay(0);
      web.Select("Clock duration", "10 Sec.;20 Sec.;30 Sec.;40 Sec.;50 Sec.;60 Sec.", &set.DS3231Length);
      web.Select("Lesson clock duration", "0 Sec.;10 Sec.;20 Sec.;30 Sec.;40 Sec.;50 Sec.", &set.lesLength);
      web.Color("Clock color", &set.colorTime);
      web.Color("Lesson color", &set.colorLesson);
      web.Color("Break color", &set.colorBreak);
      web.Color("Thermometer color", &set.colorDht);
    }
    {
      sets::Group g4(web, "");
      web.Slider("Brightness", 5, 100, 1, " %", &set.bright);
    }
    web.endMenu();
    if(web.wasSet()) {set_f.updateNow(); web.clearSet();}
  }

  {
    web.beginMenu("WiFi");delay(0);
    {
      sets::Group g(web, "");
      web.Input(sta::ssid, "SSID");
      web.Pass(sta::pass, "Password");
    }
    {
      sets::Group g2(web, "");
      web.Pass(sta::apPass, "AP password");
      web.Pass(sta::webPass, "Web interface password");
    }
    if (web.Button("Save and connect")) {
      WiFi.begin(db[sta::ssid], db[sta::pass]);
      db.update();
      ESP.restart();
    }
    web.endMenu();
  }
}

// ============ SETUP ============
void setup() {
  LittleFS.begin();
  db.begin();
  db.init(sta::ssid, "");
  db.init(sta::pass, "");
  db.init(sta::apPass, "");
  db.init(sta::webPass, "");
  set_f.read();

  CL.begin();

  dht.begin();

  Wire.begin();
  rtc.begin();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Smart_School_Clock_AP", db[sta::apPass]);
  WiFi.begin(db[sta::ssid], db[sta::pass]);

  NTP.begin();

  sett.setPass(db[sta::webPass]);
  sett.begin();
  sett.setVersion("3.3.1");
  sett.onBuild(build);

  pinMode(BUZ_PIN, OUTPUT);

  lessonsTest();
}

// ============ LOOP ============
void loop() {
  delay(0);
  db.tick();
  set_f.tick();
  sett.tick();
  delay(0);
  static uint32_t lastShowTime, lastLogicTime;

  if (millis() - lastLogicTime >= 50) {lastLogicTime = millis(); delay(0); mainLogic();}
  if (millis() - lastShowTime >= 500) {lastShowTime = millis(); CL.show(); delay(0);}
}