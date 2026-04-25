#include <Arduino.h>

// Пример обновления running и stack графиков через запрос обновления

#define WIFI_SSID ""
#define WIFI_PASS ""

#include <SettingsGyver.h>
SettingsGyver sett("My Settings");

void build(sets::Builder& b) {
    b.PlotRunning(H(run), "kek1;kek2");
    b.PlotStack(H(stack), "kek3;kek4");
}
void update(sets::Updater u) {
    float v[] = {random(100) - 50, (random(100) - 50) / 2.0};
    u.updatePlot(H(run), v);
    u.updatePlot(H(stack), v);
}

void setup() {
    Serial.begin(115200);
    Serial.println();

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("Connected: ");
    Serial.println(WiFi.localIP());

    sett.begin();
    sett.onBuild(build);
    sett.onUpdate(update);
}

void loop() {
    sett.tick();
}