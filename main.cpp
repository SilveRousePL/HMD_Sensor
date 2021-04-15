#include <ArduinoLog.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/* ================================== CONFIGURATION =================================== */
/* ---------------------------------- WIFI AND CRED ----------------------------------- */
const char*    ssid      = "<YOUR WIFI SSID>";
const char*    password  = "<YOUR WIFI PASSWORD>";
const char*    dest_ip   = "255.255.255.255";
const uint16_t dest_port = 6969;
const char*    aes_key   = "<UNUSED>";

/* -------------------------------------- PINOUT -------------------------------------- */
const uint8_t pir_pin  = 14;
const uint8_t temp_pin = 0;

/* ----------------------------------- LOOP TIMERS ------------------------------------ */
const unsigned long ntpupd_update_time = 3600000UL; // 1 h
const unsigned long temper_update_time = 120000UL;  // 120 sec
const unsigned long status_update_time = 60000UL;   // 60 sec

/* ------------------------------------ PARAMETERS ------------------------------------ */
const uint32_t sensor_id  = 10000;
const char*    ntp_server = "europe.pool.ntp.org";

/* --------------------------------------- JSON --------------------------------------- */
const char* json = "{\"sensor_id\": %u, \"time\": \"%u\", \"type\": \"%s\", \"uptime\": %u, \"motion\": %d, \"motion_counter\": %u, \"temp0\": %f}";

/* =================================== END OF CONF ==================================== */
/* ==================================================================================== */


WiFiUDP udp;
OneWire onewire(temp_pin);
DallasTemperature temp_sensors(&onewire);
NTPClient ntp(udp, ntp_server);

volatile int   motion_counter = 0;
volatile int   motion         = false;
volatile float temp0          = 0.0f;


void buildJsonMessage(char* msg, const char* type)
{
    unsigned long time        = ntp.getEpochTime();
    unsigned long uptime      = millis();

    sprintf(msg, json, sensor_id, time, type, uptime, motion, motion_counter, temp0);
}

void encrypt(char* msg, const char* key)
{
    // TODO: Encrypt
}

void sendUdpMessage(const char* type)
{
    char msg [250];
    buildJsonMessage(msg, type);
    Log.notice("[Message] Send via UDP: %s" CR, msg);
    udp.beginPacket(dest_ip, dest_port);
    encrypt(msg, aes_key);
    udp.write(msg);
    udp.endPacket();
}

float getTemperature(int index)
{
    return temp_sensors.getTempCByIndex(index);
}

void updateTemperatures()
{
    temp_sensors.requestTemperatures();
    temp0 = getTemperature(0);
}

ICACHE_RAM_ATTR void motionDetected()
{
    motion = digitalRead(pir_pin);
    Log.notice("[Motion] INTERRUPT! %s" CR, motion ? "Motion detected" : "No motion");
    if(motion)
    {
        motion_counter++;
        Log.notice("[Motion] Current motion counter: %d" CR, motion_counter);
    }
    sendUdpMessage("motion");
}

void setupWifi()
{
    Log.notice("[WiFi] Connecting to %s ", ssid);
    WiFi.softAPdisconnect(true);
    WiFi.begin(ssid, password);
    while(WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Log.notice(CR "[WiFi] Connected, IP: %s " CR, WiFi.localIP().toString().c_str());
}

void setup()
{
    Serial.begin(115200);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    Log.notice("[Startup] SETUP START" CR);
    Log.notice("[Startup] Compile time: %s %s" CR, __DATE__, __TIME__);

    setupWifi();
    ntp.begin();
    ntp.update();

    temp_sensors.begin();
    attachInterrupt(digitalPinToInterrupt(pir_pin), motionDetected, CHANGE);

    updateTemperatures();
    motion = digitalRead(pir_pin);

    sendUdpMessage("hello");
    Log.notice("[Startup] SETUP DONE" CR);
}

unsigned long recent_exectime_ntpupd;
unsigned long recent_exectime_temper;
unsigned long recent_exectime_status;
void loop()
{
    unsigned long uptime = millis();

    if(uptime - recent_exectime_ntpupd >= ntpupd_update_time)
    {
        recent_exectime_ntpupd = uptime;
        ntp.update();
    }

    if(uptime - recent_exectime_temper >= temper_update_time)
    {
        recent_exectime_temper = uptime;
        updateTemperatures();
    }

    if(uptime - recent_exectime_status >= status_update_time) 
    {
        recent_exectime_status = uptime;
        sendUdpMessage("status");
    }

    delay(500);
}
