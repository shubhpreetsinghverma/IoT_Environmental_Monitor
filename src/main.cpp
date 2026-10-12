#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID "TMPL3PRIx5S_e"
#define BLYNK_TEMPLATE_NAME "IoT Environmental Monitor"
#include "secrets.h"  // BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASS (gitignored)

#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <DHTesp.h>

// ================= WIFI =================

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;
// =====================================================
// DHT22
// =====================================================

#define DHT_PIN 15

// DHTesp instead of Adafruit DHT: Adafruit's ESP32 path disables
// interrupts for an unbounded time and trips the interrupt watchdog.
DHTesp dht;

// =====================================================
// SENSORS
// =====================================================

#define WATER_PIN 34
#define SOIL_PIN 35
#define RAIN_DO 27

// =====================================================
// OUTPUTS
// =====================================================

#define RED_LED 25
#define YELLOW_LED 26
#define GREEN_LED 33
#define BUZZER 32

// =====================================================
// IRRIGATION PUMP (RELAY)
// =====================================================
//
// Controlled by a Blynk Switch widget on V5 (integer, 0/1).
// V5 must NOT be used by any sensor datastream (V0-V4), otherwise
// the periodic sensor writes would flip the switch back off.
//
// Most relay modules are active-LOW. Set to HIGH if yours is active-HIGH.

#define PUMP_PIN 13
#define PUMP_SWITCH_VPIN V5
#define RELAY_ON LOW
#define RELAY_OFF HIGH

bool pumpOn = false;

// =====================================================
// BUZZER SETTINGS
// =====================================================

#define BUZZER_CHANNEL 0
#define BUZZER_FREQUENCY 2000
#define BUZZER_RESOLUTION 8

// =====================================================
// BLYNK TIMER
// =====================================================

BlynkTimer timer;

// =====================================================
// BUZZER FUNCTIONS
// =====================================================

void buzzerOn()
{
    ledcWriteTone(BUZZER_CHANNEL, BUZZER_FREQUENCY);
}

void buzzerOff()
{
    ledcWriteTone(BUZZER_CHANNEL, 0);
}

// =====================================================
// PUMP CONTROL
// =====================================================

void setPump(bool on)
{
    pumpOn = on;
    digitalWrite(PUMP_PIN, on ? RELAY_ON : RELAY_OFF);

    Serial.print("Pump: ");
    Serial.println(on ? "ON" : "OFF");
}

// Called whenever the switch is toggled in the Blynk app.
BLYNK_WRITE(PUMP_SWITCH_VPIN)
{
    Serial.print("Blynk V5 received: ");
    Serial.println(param.asStr());
    setPump(param.asInt() == 1);
}

// After every (re)connect, pull the switch's last value from the
// server so the pump keeps the state the user set instead of
// silently resetting to OFF.
BLYNK_CONNECTED()
{
    Blynk.syncVirtual(PUMP_SWITCH_VPIN);
}

// =====================================================
// SEND SENSOR DATA
// =====================================================

void sendSensorData()
{
    // -------------------------------------------------
    // READ SENSORS
    // -------------------------------------------------

    TempAndHumidity th = dht.getTempAndHumidity();
    float temperature = th.temperature;
    float humidity = th.humidity;

    int waterValue = analogRead(WATER_PIN);
    int soilValue = analogRead(SOIL_PIN);
    int rainState = digitalRead(RAIN_DO);

    // -------------------------------------------------
    // SERIAL OUTPUT
    // -------------------------------------------------

    Serial.println();
    Serial.println("========== SENSOR READINGS ==========");

    if (!isnan(temperature))
    {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" C");

        Blynk.virtualWrite(V0, temperature);
    }
    else
    {
        Serial.println("Temperature: ERROR");
    }

    if (!isnan(humidity))
    {
        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");

        Blynk.virtualWrite(V1, humidity);
    }
    else
    {
        Serial.println("Humidity: ERROR");
    }

    Serial.print("Water Level: ");
    Serial.println(waterValue);

    Serial.print("Soil Moisture: ");
    Serial.println(soilValue);

    Serial.print("Rain: ");
    Serial.println(rainState);

    Serial.print("Pump: ");
    Serial.println(pumpOn ? "ON" : "OFF");

    // -------------------------------------------------
    // BLYNK
    // -------------------------------------------------

    Blynk.virtualWrite(V2, waterValue);
    Blynk.virtualWrite(V3, soilValue);
    Blynk.virtualWrite(V4, rainState);

    // =================================================
    // WARNING LEVEL
    //
    // 0 = NORMAL
    // 1 = WARNING
    // 2 = DANGER
    // =================================================

    int warningLevel = 0;

    // =================================================
    // TEMPERATURE
    // =================================================

    if (!isnan(temperature))
    {
        if (temperature >= 35)
        {
            warningLevel = 2;
        }
        else if (temperature >= 30)
        {
            warningLevel = 1;
        }
    }

    // =================================================
    // WATER LEVEL
    // =================================================
    //
    // Based on your observed readings:
    //
    // 0–700       = Normal
    // 701–1500    = Warning
    // >1500       = Danger
    //
    // =================================================

    if (waterValue > 1500)
    {
        warningLevel = 2;
    }
    else if (waterValue > 700 && warningLevel < 1)
    {
        warningLevel = 1;
    }

    // =================================================
    // SOIL MOISTURE
    // =================================================
    //
    // Your FC-28:
    //
    // ~4095 = very dry
    // ~1250–1350 = wet
    //
    // 0–1999     = Normal
    // 2000–3500  = Warning
    // >3500      = Danger
    //
    // =================================================

    if (soilValue > 3500)
    {
        warningLevel = 2;
    }
    else if (soilValue >= 2000 && warningLevel < 1)
    {
        warningLevel = 1;
    }

    // =================================================
    // RAIN
    // =================================================
    //
    // 0 = Rain detected
    // 1 = No rain
    //
    // Rain produces WARNING, not danger.
    //
    // =================================================

    if (rainState == 0)
    {
        Serial.println("Rain Status: RAIN DETECTED");

        if (warningLevel < 1)
        {
            warningLevel = 1;
        }
    }
    else
    {
        Serial.println("Rain Status: NO RAIN");
    }

    // =================================================
    // RESET OUTPUTS
    // =================================================

    digitalWrite(RED_LED, LOW);
    digitalWrite(YELLOW_LED, LOW);
    digitalWrite(GREEN_LED, LOW);

    buzzerOff();

    // =================================================
    // NORMAL
    // =================================================

    if (warningLevel == 0)
    {
        digitalWrite(GREEN_LED, HIGH);

        Serial.println("SYSTEM STATUS: NORMAL");
    }

    // =================================================
    // WARNING
    // =================================================

    else if (warningLevel == 1)
    {
        digitalWrite(YELLOW_LED, HIGH);

        Serial.println("SYSTEM STATUS: WARNING");
    }

    // =================================================
    // DANGER
    // =================================================

    else
    {
        digitalWrite(RED_LED, HIGH);

        Serial.println("SYSTEM STATUS: DANGER");

        // Short buzzer beep
        buzzerOn();
        delay(200);
        buzzerOff();
    }

    Serial.println("====================================");
}

// =====================================================
// CONNECTION CHECK
// =====================================================

// Print why the ESP32 dropped / failed to join, so the serial log
// tells us whether it's a password, signal or AP problem.
void onWiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
    uint8_t reason = info.wifi_sta_disconnected.reason;
    Serial.print("\nWiFi: disconnected, reason ");
    Serial.print(reason);

    switch (reason)
    {
    case 2:
    case 15:
    case 202:
    case 204:
        Serial.println(" (auth failed - check password / network type)");
        break;
    case 201:
        Serial.println(" (SSID not found - out of range or 5 GHz only)");
        break;
    case 203:
        Serial.println(" (AP refused association - AP full or MAC blocked)");
        break;
    default:
        Serial.println();
        break;
    }
}

// List every AP broadcasting our SSID with its signal strength.
void scanForNetwork()
{
    Serial.println("WiFi: scanning...");
    int n = WiFi.scanNetworks();
    bool found = false;

    for (int i = 0; i < n; i++)
    {
        if (WiFi.SSID(i) == ssid)
        {
            found = true;
            Serial.printf("  found \"%s\"  RSSI %d dBm  ch %d  %s\n",
                          ssid, WiFi.RSSI(i), WiFi.channel(i),
                          WiFi.encryptionType(i) == WIFI_AUTH_WPA2_ENTERPRISE
                              ? "ENTERPRISE (not supported by this code)"
                              : "");
        }
    }

    if (!found)
    {
        Serial.printf("  \"%s\" NOT visible (%d networks seen)\n", ssid, n);
    }

    WiFi.scanDelete();
}

void checkConnection()
{
    static unsigned long lastReconnect = 0;

    if (WiFi.status() != WL_CONNECTED)
    {
        // A WPA2 handshake on a busy network can take >5 s; reconnecting
        // too often aborts every attempt before it can finish.
        if (millis() - lastReconnect >= 20000)
        {
            Serial.println("WiFi: NOT CONNECTED (retrying...)");
            lastReconnect = millis();
            WiFi.disconnect();
            WiFi.begin(ssid, pass);
        }
        return;
    }

    if (!Blynk.connected())
    {
        Serial.print("WiFi: CONNECTED (IP ");
        Serial.print(WiFi.localIP());
        Serial.println(") - connecting to Blynk...");

        if (Blynk.connect(3000))
        {
            Serial.println("Blynk: CONNECTED");
        }
        else
        {
            Serial.println("Blynk: FAILED (check auth token / internet access)");
        }
    }
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);

    // 1 = power-on, 9 = brownout (relay/pump pulling the supply down),
    // 4/5/6/7 = crash or watchdog. Tells us why the pump state was lost.
    Serial.print("Reset reason: ");
    Serial.println(esp_reset_reason());

    // DHT
    dht.setup(DHT_PIN, DHTesp::DHT22);

    // Sensors
    pinMode(WATER_PIN, INPUT);
    pinMode(SOIL_PIN, INPUT);
    pinMode(RAIN_DO, INPUT);

    // LEDs
    pinMode(RED_LED, OUTPUT);
    pinMode(YELLOW_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);

    // Buzzer
    pinMode(BUZZER, OUTPUT);

    // Pump relay - start OFF until Blynk tells us the switch state
    digitalWrite(PUMP_PIN, RELAY_OFF);
    pinMode(PUMP_PIN, OUTPUT);

    // Buzzer PWM
    ledcSetup(BUZZER_CHANNEL, BUZZER_FREQUENCY, BUZZER_RESOLUTION);
    ledcAttachPin(BUZZER, BUZZER_CHANNEL);
    buzzerOff();

    // LEDs OFF
    digitalWrite(RED_LED, LOW);
    digitalWrite(YELLOW_LED, LOW);
    digitalWrite(GREEN_LED, LOW);

    // =================================================
    // BLYNK
    // =================================================

    Serial.println("================================");
    Serial.println("IoT ENVIRONMENTAL MONITOR");
    Serial.println("Connecting to Blynk...");
    Serial.println("================================");

    // Non-blocking: Blynk.begin() would hang here forever if WiFi
    // or Blynk is unreachable, and the sensors/LEDs would never run.
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.onEvent(onWiFiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    // Campus networks have many APs with the same SSID. Scan all
    // channels and join the strongest one instead of the first found.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

    scanForNetwork();

    WiFi.begin(ssid, pass);
    Blynk.config(BLYNK_AUTH_TOKEN);

    // Connect immediately instead of waiting for the first timer tick.
    // Wait at most 20 s for WiFi so sensors still start if it's down.
    Serial.print("WiFi: connecting");
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000)
    {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
    checkConnection();

    // Send data every 2 seconds
    timer.setInterval(2000L, sendSensorData);

    // Check / restore connection every 5 seconds
    timer.setInterval(5000L, checkConnection);
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
    // Always service Blynk while connected so switch changes from the
    // app are received immediately.
    if (Blynk.connected())
    {
        Blynk.run();
    }
    timer.run();
}

