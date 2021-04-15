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
const char*    aes_key   = "<UNUSED>";
const char*    dest_ip   = "255.255.255.255";
const uint16_t dest_port = 6969;
const char*    google_fp = "12 34 56 78 9A BC DE F0 12 34 56 78 90 AB CD BE EF 00 DE AD";
const char*    gsheet_id = "Kp9kPQN8XUJR3N6fNJZvAw-gcbDYGHbaWsQvAxufjPy_Fb9H_R48cuRR"; // Keep Calm! It's just an example

/* -------------------------------------- PINOUT -------------------------------------- */
const uint8_t pir_pin     = 14;
const uint8_t sensors_pin = 0;

/* ----------------------------------- LOOP TIMERS ------------------------------------ */
const unsigned long status_update_time = 30000UL;   // 30 sec
const unsigned long gsheet_update_time = 180000UL;  // 3 min
const unsigned long ntpupd_update_time = 3600000UL; // 1 h

/* ------------------------------------ PARAMETERS ------------------------------------ */
const int   buffer_slots_number = 128;
const int   buffer_slot_size    = 128;
const int   header_size         = 256;
const char* ntp_server          = "europe.pool.ntp.org";

/* =================================== END OF CONF ==================================== */
/* ==================================================================================== */


WiFiUDP udp;
OneWire onewire(sensors_pin);
DallasTemperature temp_sensors(&onewire);
NTPClient ntp(udp, ntp_server);
volatile unsigned int motion_counter = 0;

class GoogleSender
{
    WiFiClientSecure client;

    const char* host = "script.google.com";
    const int   port = 443;

    char header [header_size];

    char buffer [buffer_slots_number][buffer_slot_size];
    uint16_t buffer_size;

    char** back_buffer;
    uint16_t back_buffer_size;
    
public:
    GoogleSender() : buffer_size(0)
    {
        sprintf(header, "POST /macros/s/%s/exec HTTP/1.1\r\nHost: %s\r\nUser-Agent: HMD-Sensor\r\nContent-Type: application/json\r\nContent-Length: ", gsheet_id, host);
    }

    void add(const char* msg)
    {
        strcpy(buffer[buffer_size], msg);
        buffer_size++;
        Log.notice("Added to Google Sheet buffer: %d/%d" CR, buffer_size, buffer_slots_number);
    }

    int lengthJsonList()
    {
        // Length for json list format: [{...},{...},{...}] (all_json_length + comma_number + brackets_number)
        int result = buffer_size + 1; // buffer_size - 1 (comma_number) + 2 brackets
        if(buffer_size == 0) result++; // If buffer is empty, json will have 2 brackets []
        for(int i = 0; i < buffer_size; ++i)
        {
            result += strlen(buffer[i]);
        }
        return result;
    }

    void clear()
    {
        buffer_size = 0;
    }

    void send()
    {
        Log.notice("Sending to Google Sheet" CR);

        client.setInsecure();
        if (!client.connect(host, port)) {
            Log.error("Connection failed" CR);
            return;
        }

        if (client.verify(google_fp, host)) Log.notice("Certificate matches" CR);
        else                                Log.warning("Certificate doesn't match" CR);

        int content_length = lengthJsonList();

        client.print(header);
        client.println(content_length);
        client.println();
        client.print("[");
        for(int i = 0; i < buffer_size; ++i)
        {
            client.print(buffer[i]);
            if(i < buffer_size - 1) client.print(",");
        }
        client.println("]");

        // while (client.connected()) {
        //     String line = client.readStringUntil('\n');
        //     if (line == "\r") {
        //         Serial.println("headers received");
        //         break;
        //     }
        // }
        // String line = client.readStringUntil('\n');

        Log.notice("Google sheet sent!" CR);
        
        clear();
    }

} google_sheet;

bool isWifiConnected()
{
    return WiFi.status() == WL_CONNECTED;
}

void sendUdpMessage(const char* msg)
{
    Log.notice("UDP sending: %s" CR, msg);
    udp.beginPacket(dest_ip, dest_port);
    // Encrypt msg
    udp.write(msg);
    udp.endPacket();
    Log.notice("UDP sent!" CR);
}

void sendHello(unsigned int motion_counter, float temperature)
{
    Log.notice("Send hello msg, motion_counter:%u, temperature:%f" CR, motion_counter, temperature);
    char msg [150];
    const char* json = "{\"time\": \"%u\", \"type\": \"hello\", \"uptime\": %u, \"motion_counter\": %u, \"temp0\": %f}";

    unsigned long time   = ntp.getEpochTime();
    unsigned long uptime = millis();

    sprintf(msg, json, time, uptime, motion_counter, temperature);
    sendUdpMessage(msg);
    google_sheet.add(msg);
    Log.notice("Hello sent!" CR);
}

void sendStatusUpdate(unsigned int motion_counter, float temperature)
{
    Log.notice("Send status update, motion_counter:%u, temperature:%f" CR, motion_counter, temperature);
    char msg [150];
    const char* json = "{\"time\": \"%u\", \"type\": \"status\", \"uptime\": %u, \"motion_counter\": %u, \"temp0\": %f}";
    
    unsigned long time   = ntp.getEpochTime();
    unsigned long uptime = millis();

    sprintf(msg, json, time, uptime, motion_counter, temperature);
    sendUdpMessage(msg);
    google_sheet.add(msg);
    Log.notice("Status sent!" CR);
}

void sendMotionUpdate(int motion, unsigned int motion_counter)
{
    Log.notice("Send motion update, motion:%d, motion_counter:%u" CR, motion, motion_counter);
    char msg [150];
    const char* json = "{\"time\": \"%u\", \"type\": \"motion\", \"uptime\": %u, \"motion\": %d, \"motion_counter\": %u}";
    
    unsigned long time   = ntp.getEpochTime();
    unsigned long uptime = millis();

    sprintf(msg, json, time, uptime, motion, motion_counter);
    sendUdpMessage(msg);
    google_sheet.add(msg);
    Log.notice("Motion sent!" CR);
}

void setupWifi()
{
    Log.notice("[WiFi] Connecting to %s ", ssid);
    WiFi.softAPdisconnect(true);
    WiFi.begin(ssid, password);
    while(!isWifiConnected()) {
        delay(500);
        Serial.print(".");
    }
    Log.notice(CR "[WiFi] Connected, IP:%s " CR, WiFi.localIP().toString().c_str());
}

float getTemperature()
{
    Log.notice("Get temperature from sensor 0 " CR);
    temp_sensors.requestTemperatures();
    return temp_sensors.getTempCByIndex(0);
}

ICACHE_RAM_ATTR void motionDetected()
{
    int motion = digitalRead(pir_pin);
    Log.notice("MOTION INTERRUPT! Motion:%d" CR, motion);
    if(motion)
    {
        motion_counter++;
    }
    Log.notice("Current motion counter:%u" CR, motion_counter);
    sendMotionUpdate(motion, motion_counter);
    Log.notice("Interrupt done" CR);
}

void setup()
{
    Serial.begin(115200);
    Log.begin(LOG_LEVEL_VERBOSE, &Serial);
    Log.notice("SETUP START" CR);

    setupWifi();
    ntp.begin();
    ntp.update();
    
    sendHello(motion_counter, getTemperature());

    temp_sensors.begin();
    Log.notice("Temperature sensors initialized" CR);
    attachInterrupt(digitalPinToInterrupt(pir_pin), motionDetected, CHANGE);
    Log.notice("Motion detector initialized" CR);

    Log.notice("SETUP DONE" CR);
}

unsigned long recent_exectime_status;
unsigned long recent_exectime_gsheet;
unsigned long recent_exectime_ntpupd;
void loop()
{
    unsigned long uptime = millis();

    if(uptime - recent_exectime_status >= status_update_time) 
    {
        recent_exectime_status = uptime;
        sendStatusUpdate(motion_counter, getTemperature());
    }

    if(uptime - recent_exectime_gsheet >= gsheet_update_time)
    {
        recent_exectime_gsheet = uptime;
        google_sheet.send();
    }

    if(uptime - recent_exectime_ntpupd >= ntpupd_update_time)
    {
        recent_exectime_ntpupd = uptime;
        ntp.update();
    }

    delay(1);
}
