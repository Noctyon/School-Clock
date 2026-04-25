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

DHT dht(D4, DHT11);

Adafruit_NeoPixel CL(174, D7, NEO_GRB + NEO_KHZ800);

GyverDBFile db(&LittleFS, "/WiFi.set", 1000);

SettingsGyver sett("Smart School Clock", &db);

// ============ ДАННІ ============
DB_KEYS(sta, ssid, pass, apPass, webPass);

struct {
  uint32_t workStartTime = 8 * 3600;
  uint32_t workEndTime = 18 * 3600;
  uint32_t lessons[16];
  uint32_t lessonsJunior[16];
  uint8_t bright = 5;
  uint8_t numberOfLessons = 4;
  uint8_t numberOfLessonsJunior = 4;
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
  bool nineBuz = 1;
}set;

FileData set_f(&LittleFS, "/Settings.set", 'a', &set, sizeof(set), 1000);

int8_t snakeStep = -1;
uint32_t snakePrevMillis, dataTime = 1772352000, BuzOnTime, BuzOnTimeJunior;
bool ntpSynced, update = 1, buzTest;

uint8_t digits[10] = {             //  БАЙТОВІ МАСКИ ЦИФР
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

// ============ ФУНКЦІЯ ВИВЕДЕННЯ ЦИФР ============
void drawDigit(uint8_t digit, uint8_t offsetDigit, uint32_t color) {
  digit = constrain(digit, 0, 9);

  CL.fill(color, offsetDigit, 21);
  for (uint8_t i = 0; i < 7; i++) {delay(0);
    if (!(digits[digit] & (1 << (6 - i)))) CL.fill(0, offsetDigit + i * 3, 3);
  }
}

// ============ ФУНКЦІЯ ВИВЕДЕННЯ 2-Х ЗНАЧНИХ ЧИСЕЛ ============
void drawNumber(uint8_t number, uint8_t offsetNumber, uint32_t color) {
  number = constrain(number, 0, 99);

  drawDigit(number / 10, offsetNumber, color);
  drawDigit(number % 10, offsetNumber + 21, color);
}

// ============ РЕЖИМ ЗМІЙКИ ============
void snake() {
  if (snakeStep == -1 or millis() - snakePrevMillis < 50) return;
  snakePrevMillis = millis(); delay(0);

  randomSeed(millis());
  
  CL.fill(CL.Color(random(0, 11) * 25, random(0, 11) * 25, random(0, 11) * 25), snakeStep * 3, 3);
  CL.fill(CL.Color(random(0, 11) * 25, random(0, 11) * 25, random(0, 11) * 25), snakeStep * 3 + 87, 3);
  if(snakeStep >= 3) {CL.fill(0, (snakeStep - 3) * 3, 3); CL.fill(0, (snakeStep - 3) * 3 + 87, 3);}
  if(snakeStep > 14) CL.fill(0, 87, 16);
  CL.show();
  delay(0);

  snakeStep++;
  if (snakeStep > 32) snakeStep = -1;
}

// ============ РЕЖИМ ТЕРМОМЕТРА ============
void Temp(uint8_t temp) {
  drawNumber(temp, 0, set.colorDht); drawNumber(temp, 87, set.colorDht);
  CL.fill(set.colorDht, 45, 12); CL.fill(set.colorDht, 132, 12);
  CL.fill(set.colorDht, 72, 12); CL.fill(set.colorDht, 159, 12);
}

// ============ РЕЖИМ БЛИМАННЯ ДВОКРАПКИ ============
void blinc(uint32_t color) {
  if ((millis() / 500) % 2 == 0) CL.fill(color, 42, 3);
  else CL.fill(0, 42, 3);
  CL.setPixelColor(43, 0);
}

void blincJunior(uint32_t color) {
  if ((millis() / 500) % 2 == 0) CL.fill(color, 129, 3);
  else CL.fill(0, 129, 3);
  CL.setPixelColor(130, 0);
}

// ============ РЕЖИМ ГОДИННИКА ============
void DS3231() {
  Datime now(rtc);

  drawNumber(now.hour + set.timeZome, 0, set.colorTime);
  drawNumber(now.minute, 45, set.colorTime);
  
  blinc(set.colorTime);
}

void DS3231Junior() {
  Datime now(rtc);

  drawNumber(now.hour + set.timeZome, 87, set.colorTime);
  drawNumber(now.minute, 132, set.colorTime);
  
  blincJunior(set.colorTime);
}

// ============ ПЕРЕВІРКА ТИРИВАЛОСТІ УРОКІВ, ПЕРЕРВ ============
void lessonsTest() {
  for (int i; i <= set.numberOfLessons * 2; i++){delay(0);
    if (((set.lessons[i + 1] - set.lessons[i]) / 60) > 99) set.lessons[i + 1] = set.lessons[i] + 5940;
  }

  for (int i; i <= set.numberOfLessonsJunior * 2; i++){delay(0);
    if (((set.lessonsJunior[i + 1] - set.lessonsJunior[i]) / 60) > 99) set.lessonsJunior[i + 1] = set.lessonsJunior[i] + 5940;
  }
  set_f.updateNow();
}

// ============ РЕЖИМ УРОКІВ, ПЕРЕРВ ============
void Lessons() {
  Datime now(rtc);
  uint16_t timeNow = now.hour * 60 + now.minute + 60 * set.timeZome;
  uint8_t lessonNumber = 0, breakNumber = 0;

  if (timeNow < (set.lessons[0] / 60) or timeNow >= (set.lessons[set.numberOfLessons * 2 + 1] / 60)) {DS3231(); return;}

  for (int i = 0; i <= set.numberOfLessons; i++) {delay(0);
    if (timeNow >= (set.lessons[i * 2] / 60) and timeNow < (set.lessons[i * 2 + 1] / 60)) {lessonNumber = i + 1; break;} else lessonNumber = 0;
    if (i < set.numberOfLessons and timeNow >= (set.lessons[i * 2 + 1] / 60) and timeNow <  (set.lessons[i * 2 + 2] / 60)) {breakNumber = i + 1; break;} else breakNumber = 0;
  }

  if (lessonNumber > 0) {
    drawDigit(lessonNumber, 0, set.colorLesson);
    drawNumber((set.lessons[lessonNumber * 2 - 1] / 60) - timeNow, 45, set.colorLesson);
    blinc(set.colorLesson);
  } else if (breakNumber > 0) {
    drawDigit(breakNumber, 0, set.colorBreak);
    drawNumber((set.lessons[breakNumber * 2] / 60) - timeNow, 45, set.colorBreak);
    blinc(set.colorBreak);
  }
}

void LessonsJunior() {
  Datime now(rtc);
  uint16_t timeNow = now.hour * 60 + now.minute + 60 * set.timeZome;
  uint8_t lessonNumber = 0, breakNumber = 0;

  if (timeNow < (set.lessonsJunior[0] / 60) or timeNow >= (set.lessonsJunior[set.numberOfLessonsJunior * 2 + 1] / 60)) {DS3231Junior(); return;}

  for (int i = 0; i <= set.numberOfLessonsJunior; i++) {delay(0);
    if (timeNow >= (set.lessonsJunior[i * 2] / 60) and timeNow < (set.lessonsJunior[i * 2 + 1] / 60)) {lessonNumber = i + 1; break;} else lessonNumber = 0;
    if (i < set.numberOfLessonsJunior and timeNow >= (set.lessonsJunior[i * 2 + 1] / 60) and timeNow <  (set.lessonsJunior[i * 2 + 2] / 60)) {breakNumber = i + 1; break;} else breakNumber = 0;
  }

  if (lessonNumber > 0) {
    drawDigit(lessonNumber, 87, set.colorLesson);
    drawNumber((set.lessonsJunior[lessonNumber * 2 - 1] / 60) - timeNow, 132, set.colorLesson);
    blincJunior(set.colorLesson);
  } else if (breakNumber > 0) {
    drawDigit(breakNumber, 87, set.colorBreak);
    drawNumber((set.lessonsJunior[breakNumber * 2] / 60) - timeNow, 132, set.colorBreak);
    blincJunior(set.colorBreak);
  }
}

// ============ РЕЖИМ ДЗВІНКА ============
void Buzzer(){
  Datime now(rtc);
  uint16_t timeNow = now.hour * 60 + now.minute + 60 * set.timeZome;
  static bool flags[16], flagsJunior[16];

  if (!set.nineBuz and timeNow == 540) return;

  if (timeNow < (set.lessons[0] / 60) or timeNow > (set.lessons[1 + 2 * set.numberOfLessons] / 60) or update){update = 0; for (int i = 0; i < 16; i++) {flags[i] = 0; delay(0);}}
  if (timeNow < (set.lessonsJunior[0] / 60) or timeNow > (set.lessonsJunior[1 + 2 * set.numberOfLessonsJunior] / 60) or update){update = 0; for (int i = 0; i < 16; i++) {flagsJunior[i] = 0; delay(0);}}

  for (int i = 0; i < 2 * (set.numberOfLessons + 1); i++) {delay(0); if (timeNow == (set.lessons[i] / 60) and flags[i] == 0 and set.buzzerStatus) {digitalWrite(D6, 1); BuzOnTime = millis(); flags[i] = 1;}}
  for (int i = 0; i < 2 * (set.numberOfLessonsJunior + 1); i++) {delay(0); if (timeNow == (set.lessonsJunior[i] / 60) and flagsJunior[i] == 0 and set.buzzerStatus) {digitalWrite(D5, 1); BuzOnTimeJunior = millis(); flagsJunior[i] = 1;}}

  if (millis() - BuzOnTime >= set.buzzerLength * 1000) digitalWrite(D6, 0);
  if (millis() - BuzOnTimeJunior >= set.buzzerLength * 1000) digitalWrite(D5, 0);
}

// ============ СИНХРОНІЗАЦІЯ ЧЕРЕЗ NTP ============
void ntpSync() {
  if (NTP.online() and NTP.updateNow() and NTP.getUnix() >= 1753340400UL) {
    rtc.setUnix(NTP.getUnix() + 7210);
    ntpSynced = 1; update = 1;
  }
}

// ============ ОСНОВНА ЛОГІКА РОБОТИ ============
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
        case 1: CL.fill(0, 0, 174); DS3231(); DS3231Junior(); break;
        case 2: CL.fill(0, 0, 174); Lessons(); LessonsJunior(); break;
        case 3: CL.fill(0, 0, 174); Temp(temp); break;
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

// ============ ВЕБ-ІНТЕРФЕЙС ============
void build(sets::Builder& web) {

  web.Switch("Power", &set.power);
  if (web.wasSet()) {set_f.updateNow(); web.reload(); web.clearSet();}
  if(!set.power) return;
  {
    web.beginMenu("Розклад старші класи");delay(0);
    web.Select("Кількість уроків", "1;2;3;4;5;6;7;8", &set.numberOfLessons);
    {sets::Group g1(web, "1-й урок"); web.Time("Початок", &set.lessons[0]); web.Time("Кінець", &set.lessons[1]);}
    if (set.numberOfLessons > 0) {sets::Group g2(web, "2-й урок");web.Time("Початок", &set.lessons[2]);web.Time("Кінець", &set.lessons[3]);}
    if (set.numberOfLessons > 1) {sets::Group g3(web, "3-й урок");web.Time("Початок", &set.lessons[4]);web.Time("Кінець", &set.lessons[5]);}
    if (set.numberOfLessons > 2) {sets::Group g4(web, "4-й урок");web.Time("Початок", &set.lessons[6]);web.Time("Кінець", &set.lessons[7]);}
    if (set.numberOfLessons > 3) {sets::Group g5(web, "5-й урок");web.Time("Початок", &set.lessons[8]);web.Time("Кінець", &set.lessons[9]);}
    if (set.numberOfLessons > 4) {sets::Group g6(web, "6-й урок");web.Time("Початок", &set.lessons[10]);web.Time("Кінець", &set.lessons[11]);}
    if (set.numberOfLessons > 5) {sets::Group g7(web, "7-й урок");web.Time("Початок", &set.lessons[12]);web.Time("Кінець", &set.lessons[13]);}
    if (set.numberOfLessons > 6) {sets::Group g8(web, "8-й урок");web.Time("Початок", &set.lessons[14]);web.Time("Кінець", &set.lessons[15]);}

    web.endMenu();
    if (web.wasSet()) {set_f.updateNow(); web.clearSet(); lessonsTest(); web.reload(); update = 1;}
  }

  {
    web.beginMenu("Розклад молодші класи");delay(0);
    web.Select("Кількість уроків", "1;2;3;4;5;6;7;8", &set.numberOfLessonsJunior);
    {sets::Group g1(web, "1-й урок"); web.Time("Початок", &set.lessonsJunior[0]); web.Time("Кінець", &set.lessonsJunior[1]);}
    if (set.numberOfLessonsJunior > 0) {sets::Group g2(web, "2-й урок");web.Time("Початок", &set.lessonsJunior[2]);web.Time("Кінець", &set.lessonsJunior[3]);}
    if (set.numberOfLessonsJunior > 1) {sets::Group g3(web, "3-й урок");web.Time("Початок", &set.lessonsJunior[4]);web.Time("Кінець", &set.lessonsJunior[5]);}
    if (set.numberOfLessonsJunior > 2) {sets::Group g4(web, "4-й урок");web.Time("Початок", &set.lessonsJunior[6]);web.Time("Кінець", &set.lessonsJunior[7]);}
    if (set.numberOfLessonsJunior > 3) {sets::Group g5(web, "5-й урок");web.Time("Початок", &set.lessonsJunior[8]);web.Time("Кінець", &set.lessonsJunior[9]);}
    if (set.numberOfLessonsJunior > 4) {sets::Group g6(web, "6-й урок");web.Time("Початок", &set.lessonsJunior[10]);web.Time("Кінець", &set.lessonsJunior[11]);}
    if (set.numberOfLessonsJunior > 5) {sets::Group g7(web, "7-й урок");web.Time("Початок", &set.lessonsJunior[12]);web.Time("Кінець", &set.lessonsJunior[13]);}
    if (set.numberOfLessonsJunior > 6) {sets::Group g8(web, "8-й урок");web.Time("Початок", &set.lessonsJunior[14]);web.Time("Кінець", &set.lessonsJunior[15]);}
    web.endMenu();
    if (web.wasSet()) {set_f.updateNow(); web.clearSet(); lessonsTest(); web.reload(); update = 1;}
  }

  {
    web.beginMenu("Дата і час");delay(0);
    {
      sets::Group g(web, " ");
      web.DateTime("Дата і час", &dataTime);
      if (web.Button("Синхронізувати")){rtc.setUnix(dataTime + 5 + 3600 * (2 - set.timeZome)); update = 1;}
      web.Select("Сезонний час", "зимовий;літній", &set.timeZome);
      if (web.Button("Синхронізувати через NTP")) ntpSync();
      if(web.wasSet()) {set_f.updateNow(); web.clearSet(); web.reload();}
    }
    web.endMenu();
  }

  {
    web.beginMenu("Системні налаштування");delay(0);
    {
      sets::Group g(web, "Робочий час");
      web.Time("Початок роботи", &set.workStartTime);
      web.Time("Кінець роботи", &set.workEndTime);
      web.Switch("Понеділок", &set.activeDays[0]);
      web.Switch("Вівторок", &set.activeDays[1]);
      web.Switch("Середа", &set.activeDays[2]);
      web.Switch("Четвер", &set.activeDays[3]);
      web.Switch("П'ятниця", &set.activeDays[4]);
      web.Switch("Субота", &set.activeDays[5]);
      web.Switch("Неділя", &set.activeDays[6]);
    }
    {
      sets::Group g2(web, "");delay(0);
      if(web.Switch("Дзвінок", &set.buzzerStatus)) {set_f.updateNow(); web.reload();}
      if(set.buzzerStatus) {web.Slider("Тривалість дзвінка", 1, 7, 0.5, " Сек", &set.buzzerLength);
        web.Switch("Чи звонити дзвінок в 9:00", &set.nineBuz);
        if(web.Switch("Тест дзвінка", &buzTest)) web.reload();
        if (buzTest){
          if (web.Button("Старша ланка")){digitalWrite(D6, 1); BuzOnTime = millis();}
          if (web.Button("Молодша ланка")){digitalWrite(D5, 1); BuzOnTimeJunior = millis();}
        }
      }
    }
    {
      sets::Group g3(web, "Налаштування режимів");delay(0);
      web.Select("Тривалість годинника", "10 Сек;20 Сек;30 Сек;40 Сек;50 Сек;60 Сек", &set.DS3231Length);
      web.Select("Тривалість годинника уроків", "0 Сек;10 Сек;20 Сек;30 Сек;40 Сек;50 Сек", &set.lesLength);
      web.Color("Колір режиму годинника", &set.colorTime);
      web.Color("Колір режиму урока", &set.colorLesson);
      web.Color("Колір режиму перерви", &set.colorBreak);
      web.Color("Колір режиму термометра", &set.colorDht);
    }
    {
      sets::Group g4(web, "");
      web.Slider("Яскравість", 5, 100, 1, " %", &set.bright);
    }
    web.endMenu();
    if(web.wasSet()) {set_f.updateNow(); web.clearSet();}
  }

  {
    web.beginMenu("WiFi");delay(0);
    {
      sets::Group g(web, "");
      web.Input(sta::ssid, "SSID");
      web.Pass(sta::pass, "Пароль");
    }
    {
      sets::Group g2(web, "");
      web.Pass(sta::apPass, "Пароль точки доступу");
      web.Pass(sta::webPass, "Пароль веб-ітерфейсу");
    }
    if (web.Button("Зєднати та зберегти")) {
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
  sett.setVersion("V3.3.0_For_Kalush_Lyceum_10");
  sett.onBuild(build);

  pinMode(D6, OUTPUT);
  pinMode(D5, OUTPUT);

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