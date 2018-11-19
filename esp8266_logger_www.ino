#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>
#include "FS.h"
#include <time.h>                       // time() ctime()
#include <sys/time.h>                   // struct timeval
#include <OneWire.h>
#include <DallasTemperature.h>

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#include "password.inc"

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

#define LED_MAY_LIGHT

#ifdef LED_MAY_LIGHT
#define LED_ON      { pinMode(D0, OUTPUT); digitalWrite(D0, LOW); }
#define LED_OFF     { pinMode(D0, OUTPUT); digitalWrite(D0, HIGH); }
#define LED_TOGGLE  { pinMode(D0, OUTPUT); digitalWrite(D0, (digitalRead(D0) == HIGH ? LOW : HIGH) ); }
#else
#define LED_ON      { }
#define LED_OFF     { }
#define LED_TOGGLE  { }
#endif

#define TZ              3           // (utc+) TZ in hours
#define DST_MN          0           // use 60mn for summer time in some countries
#define TZ_MN           ((TZ)*60)
#define TZ_SEC          ((TZ)*3600)
#define DST_SEC         ((DST_MN)*60)

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////


time_t time_unix;
struct tm * time_info;
uint16_t year;
uint8_t month, day, hour, minute, second;

uint32_t second_counter;
volatile uint8_t flag_1second;

uint8_t dallas_temperature_requested;
float ds_t1;
float ds_t2;

const float DALLAS_TEMPERATURE_ERROR = -300;
const float ADC_LSB_VBAT = 0.00510908;
const float RSSI_ERROR = -300;

typedef struct
{
  uint32_t uptime;
  time_t time_unix;
  int16_t rssi, rssi_min, rssi_max;
  float vbat, vbat_min, vbat_max;
  float t1, t1_min, t1_max;
  float t2, t2_min, t2_max;
} __attribute__((packed)) record_t;

#define RECORDS_PERIOD_1S (60)
record_t records_period_1s[RECORDS_PERIOD_1S];
record_t record_1s;

#define RECORDS_PERIOD_10S (60)
record_t records_period_10s[RECORDS_PERIOD_10S];
record_t record_10s;

#define RECORDS_PERIOD_1M (60)
record_t records_period_1m[RECORDS_PERIOD_1M];
record_t record_1m;

record_t record_10m;

const char* record_1m_filename = "/record_1m.bin";
const char* record_10m_filename = "/record_10m.bin";

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

ESP8266WebServer server(80);
Ticker staticTicker5sec;
ESP8266WiFiMulti wifiMulti;

OneWire  ds1(D1);  // on pin D1
OneWire  ds2(D2);  // on pin D2
DallasTemperature sensor1(&ds1);
DallasTemperature sensor2(&ds2);

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

void periodic_in_timer();

/*
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

     .oooooo..o oooooooooooo ooooooooooooo ooooo     ooo ooooooooo.
    d8P'    `Y8 `888'     `8 8'   888   `8 `888'     `8' `888   `Y88.
    Y88bo.       888              888       888       8   888   .d88'
     `"Y8888o.   888oooo8         888       888       8   888ooo88P'
         `"Y88b  888    "         888       888       8   888
    oo     .d8P  888       o      888       `88.    .8'   888
    8""88888P'  o888ooooood8     o888o        `YbodP'    o888o

//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
*/

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  for (int i = 0; i < WIFI_STA_CNT; i++)
  {
    wifiMulti.addAP( wifi_ssid[i], wifi_password[i] );
  }

  Serial.print("\r\nBooting");

  Serial.printf("\r\nFilesystem initialization... %d", SPIFFS.begin() );
  if (SPIFFS.exists("/.formatted") == 0)
  {
    Serial.printf("\r\nFilesystem not formatted...");
    SPIFFS.format();
    File f = SPIFFS.open("/.formatted", "w");
    Serial.printf("...%d", f != 0);
    f.close();
  }
  if (!SPIFFS.exists(record_1m_filename))
  {
    File f = SPIFFS.open(record_1m_filename, "w+");
    f.close();
  }
  if (!SPIFFS.exists(record_10m_filename))
  {
    File f = SPIFFS.open(record_10m_filename, "w+");
    f.close();
  }


  configTime(TZ_SEC, DST_SEC, "pool.ntp.org", "time.nist.gov");

  /*
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\r\nWiFi connecting");
  while (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("\r\nConnection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  Serial.print("\r\nConnected to "); Serial.print(ssid);
  Serial.print("\r\nIP address: "); Serial.print(WiFi.localIP());
  */

  variables_setup();

  ArduinoOTA_setup();
  WebServer_setup();

  Serial.print("\r\nReady");
  Serial.print("\r\nHost OTA="); Serial.print(ArduinoOTA.getHostname());

  flag_1second = 0;
  staticTicker5sec.attach_ms(1000, periodic_in_timer);
}

void variables_setup()
{
  second_counter = 0;

  memset(records_period_1s, sizeof(records_period_1s), 0);
  memset(records_period_10s, sizeof(records_period_10s), 0);
  memset(records_period_1m, sizeof(records_period_1m), 0);

  ds_t1 = DALLAS_TEMPERATURE_ERROR;
  ds_t2 = DALLAS_TEMPERATURE_ERROR;

  set_record_to_default(&record_1s);
  set_record_to_default(&record_10s);
  set_record_to_default(&record_1m);
  set_record_to_default(&record_10m);
}

/*
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

    ooooo          .oooooo.     .oooooo.   ooooooooo.
    `888'         d8P'  `Y8b   d8P'  `Y8b  `888   `Y88.
     888         888      888 888      888  888   .d88'
     888         888      888 888      888  888ooo88P'
     888         888      888 888      888  888
     888       o `88b    d88' `88b    d88'  888
    o888ooooood8  `Y8bood8P'   `Y8bood8P'  o888o

//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
*/

void loop()
{
  LED_TOGGLE;

  if (flag_1second != 0)
  {
    flag_1second = 0;
    every_second_handle();
  }
  dallas_temperature_handle();

  monitorWiFi();
  server.handleClient();
  ArduinoOTA.handle();
}


/*
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

  ooooooooooooo ooooo ooo        ooooo oooooooooooo ooooooooo.
  8'   888   `8 `888' `88.       .888' `888'     `8 `888   `Y88.
       888       888   888b     d'888   888          888   .d88'
       888       888   8 Y88. .P  888   888oooo8     888ooo88P'
       888       888   8  `888'   888   888    "     888`88b.
       888       888   8    Y     888   888       o  888  `88b.
      o888o     o888o o8o        o888o o888ooooood8 o888o  o888o

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
*/

void periodic_in_timer()
{
  second_counter++;
  //
  //
  time_unix = time(nullptr);
  time_info = localtime ( &time_unix );
  year = time_info->tm_year + 1900;
  month = time_info->tm_mon + 1;
  day = time_info->tm_mday;
  hour = time_info->tm_hour;
  minute = time_info->tm_min;
  second = time_info->tm_sec;
  //
  //
  record_1s.uptime = second_counter;
  record_1s.time_unix = time_unix;
  record_1s.rssi = WiFi.RSSI();
  record_1s.vbat = analogRead(A0) * ADC_LSB_VBAT;
  record_1s.t1 = ds_t1;
  record_1s.t2 = ds_t2;
  //
  //
  flag_1second++;
}

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////


void set_record_to_default(record_t* rec)
{
  rec->uptime = 0;
  rec->time_unix = 0;
  rec->rssi = RSSI_ERROR;
  rec->rssi_min = RSSI_ERROR;
  rec->rssi_max = RSSI_ERROR;
  rec->vbat = -1;
  rec->vbat_min = -1;
  rec->vbat_max = -1;
  rec->t1 = DALLAS_TEMPERATURE_ERROR;
  rec->t1_min = DALLAS_TEMPERATURE_ERROR;
  rec->t1_max = DALLAS_TEMPERATURE_ERROR;
  rec->t2 = DALLAS_TEMPERATURE_ERROR;
  rec->t2_min = DALLAS_TEMPERATURE_ERROR;
  rec->t2_max = DALLAS_TEMPERATURE_ERROR;
}


boolean connectioWasAlive = true;
void monitorWiFi()
{
  if (wifiMulti.run() != WL_CONNECTED)
  {
    if (connectioWasAlive == true)
    {
      connectioWasAlive = false;
      Serial.print("Looking for WiFi ");
    }
    Serial.print(".");
    //delay(500);
  }
  else if (connectioWasAlive == false)
  {
    connectioWasAlive = true;
    Serial.printf(" connected to %s\n", WiFi.SSID().c_str());
  }
}

void dallas_temperature_handle()
{
  if (dallas_temperature_requested == 0) { return; }

  if (dallas_temperature_requested & 0x01)
  {
    if (sensor1.getDeviceCount() > 0)
    {
      if (sensor1.isConversionComplete())
      {
        dallas_temperature_requested &= ~0x01;
        ds_t1 = sensor1.getTempCByIndex(0);
      }
    }
    else
    {
        dallas_temperature_requested &= ~0x01;
        ds_t1 = DALLAS_TEMPERATURE_ERROR;
    }
  }

  if (dallas_temperature_requested & 0x02)
  {
    if (sensor2.getDeviceCount() > 0)
    {
      if (sensor2.isConversionComplete())
      {
        dallas_temperature_requested &= ~0x02;
        ds_t2 = sensor2.getTempCByIndex(0);
      }
    }
    else
    {
        dallas_temperature_requested &= ~0x02;
        ds_t2 = DALLAS_TEMPERATURE_ERROR;
    }
  }

}

void every_second_handle()
{
  //
  //
  sensor1.begin();
  if (sensor1.getDeviceCount() > 0) { sensor1.requestTemperatures(); }
  dallas_temperature_requested |= 0x01;
  sensor2.begin();
  if (sensor2.getDeviceCount() > 0) { sensor2.requestTemperatures(); }
  dallas_temperature_requested |= 0x02;
  //
  //  period 1 second
  //
  memmove(
    &records_period_1s[1],
    &records_period_1s[0],
    (RECORDS_PERIOD_1S-1)*sizeof(records_period_1s[0])
    );
  records_period_1s[0] = record_1s;
  average_record(&record_1s, &record_10s, 10);
  //
  //  period 10 seconds
  //
  if ( (second_counter % 10) == 0 )
  {
    record_10s.uptime = record_1s.uptime;
    record_10s.time_unix = record_1s.time_unix;
    memmove(
      &records_period_10s[1],
      &records_period_10s[0],
      (RECORDS_PERIOD_10S-1)*sizeof(records_period_10s[0])
      );
    records_period_10s[0] = record_10s;
    average_record(&record_10s, &record_1m, 6);
  }
  //
  //  period 60 seconds / 1 minute
  //
  if ( (second_counter % 60) == 0 )
  {
    record_1m.uptime = record_10s.uptime;
    record_1m.time_unix = record_10s.time_unix;
    memmove(
      &records_period_1m[1],
      &records_period_1m[0],
      (RECORDS_PERIOD_1M-1)*sizeof(records_period_1m[0])
      );
    records_period_1m[0] = record_1m;
    average_record(&record_1m, &record_10m, 10);
  }
  //
  //  period 600 seconds / 10 minutes
  //
  if ( (second_counter % 600) == 0 )
  {
    record_10m.uptime = record_1m.uptime;
    record_10m.time_unix = record_1m.time_unix;
    //
    File f1m = SPIFFS.open(record_1m_filename, "a");
    if (f1m)
    {
      uint32_t size = f1m.size();
      size = size % sizeof(record_t);
      if (size > 0)
      {
        Serial.printf("\r\nERROR FS %s pos error %d record size %d",
          record_1m_filename, size, sizeof(record_t));
      }
      while (size > 0)
      {
        f1m.write(0);
        size--;
      }
      for (int i = 10; i > 0; i--)
      {
        f1m.write((const uint8_t *)&records_period_1m[i-1], sizeof(records_period_1m[0]));
      }
      f1m.close();
    }
    //
    File f10m = SPIFFS.open(record_10m_filename, "a");
    if (f10m)
    {
      uint32_t size = f10m.size();
      size = size % sizeof(record_t);
      if (size > 0)
      {
        Serial.printf("\r\nERROR FS %s pos error %d record size %d",
          record_10m_filename, size, sizeof(record_t));
      }
      while (size > 0)
      {
        f10m.write(0);
        size--;
      }
      f10m.write((const uint8_t *)&record_10m, sizeof(record_10m));
      f10m.close();
    }
  }


  //
  //  reset averaging values to default
  //
  set_record_to_default(&record_1s);
  if ( (second_counter %  10) == 0 ) { set_record_to_default(&record_10s); }
  if ( (second_counter %  60) == 0 ) { set_record_to_default(&record_1m); }
  if ( (second_counter % 600) == 0 ) { set_record_to_default(&record_10m); }

}

void average_record(record_t *src, record_t *dst, uint16_t cnt)
{
  if (cnt == 0) { return; }
  //
  //
  if (src->rssi != RSSI_ERROR)
  {
    if (dst->rssi == RSSI_ERROR)
    {
      dst->rssi = src->rssi;
      dst->rssi_min = src->rssi;
      dst->rssi_max = src->rssi;
    }
    else
    {
      dst->rssi = (dst->rssi*(cnt-1) + src->rssi) / cnt;
      if (dst->rssi_min > src->rssi) { dst->rssi_min = src->rssi; }
      if (dst->rssi_max < src->rssi) { dst->rssi_max = src->rssi; }
    }
  }
  //
  //
  dst->vbat = (dst->vbat*(cnt-1) + src->vbat) / cnt;
  if (dst->vbat_min > src->vbat) { dst->vbat_min = src->vbat; }
  if (dst->vbat_max < src->vbat) { dst->vbat_max = src->vbat; }
  //
  //
  if (src->t1 != DALLAS_TEMPERATURE_ERROR)
  {
    if (dst->t1 == DALLAS_TEMPERATURE_ERROR)
    {
      dst->t1 = src->t1;
      dst->t1_min = src->t1;
      dst->t1_max = src->t1;
    }
    else
    {
      dst->t1 = (dst->t1*(cnt-1) + src->t1) / cnt;
      if (dst->t1_min > src->t1) { dst->t1_min = src->t1; }
      if (dst->t1_max < src->t1) { dst->t1_max = src->t1; }
    }
  }
  //
  //
  if (src->t2 != DALLAS_TEMPERATURE_ERROR)
  {
    if (dst->t2 == DALLAS_TEMPERATURE_ERROR)
    {
      dst->t2 = src->t2;
      dst->t2_min = src->t2;
      dst->t2_max = src->t2;
    }
    else
    {
      dst->t2 = (dst->t2*(cnt-1) + src->t2) / cnt;
      if (dst->t2_min > src->t2) { dst->t2_min = src->t2; }
      if (dst->t2_max < src->t2) { dst->t2_max = src->t2; }
    }
  }
}

String time_to_string(time_t time)
{
  String result;
  char temps[32];
  struct tm * info = localtime ( &time );

  sprintf(temps, "%d/%02d/%02d %02d:%02d:%02d",
    info->tm_year + 1900,
    info->tm_mon + 1,
    info->tm_mday,
    info->tm_hour,
    info->tm_min,
    info->tm_sec
    );

  result += temps;
  return result;
}

/*
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

    oooooo   oooooo     oooo oooooooooooo oooooooooo.
     `888.    `888.     .8'  `888'     `8 `888'   `Y8b
      `888.   .8888.   .8'    888          888     888
       `888  .8'`888. .8'     888oooo8     888oooo888'
        `888.8'  `888.8'      888    "     888    `88b
         `888'    `888'       888       o  888    .88P
          `8'      `8'       o888ooooood8 o888bood8P'

//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
*/

void WebServer_setup()
{
  server.on("/", handleRoot);
  server.on("/menu", handle_menu);
  server.on("/content", handle_content);

  server.on("/fs", handleFS);
  server.on("/records1s", handle_records1s);
  server.on("/records10s", handle_records10s);
  server.on("/records1m", handle_records1m);
  server.on("/records1mf", handle_records1mf);

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.print("\r\nHTTP server started");
}

void handleRoot()
{
  Serial.printf("\r\nvoid handleRoot()");
  String msg;

  int time_make = millis();
  //
  //
  msg += F("<html>");

    msg += F("<head>");
      msg += F("<meta charset=\"utf-8\">");
      msg += F("<title>"); msg += _hostname; msg += F("</title>");
    msg += F("</head>");

    msg += F("<frameset rows="80,*" cols="*">");
      msg += F("<frame src=\"/menu\" name=\"MENU\" noresize scrolling=\"no\">");
      msg += F("<frame src=\"/content\" name=\"content\">");
    msg += F("</frameset>");

  msg += F("</html>");
  //
  //
  time_make = millis() - time_make;
  //
  //
  int time_send = millis();
  server.send(200, "text/html", msg);
  time_send = millis() - time_send;
  //
  //
  Serial.printf("\tmake=% 5dms send=% 5dms, length=% 5d",
    time_make, time_send, msg.length() );
}



void handle_menu()
{
  //LED_ON
  Serial.printf("\r\nvoid handle_menu()");
  int time_make = millis();

  String message;
  char _buffer[80];
  message += F("<html>");

    message += F("<head>");
      message += F("<meta http-equiv='refresh' content='10'/>");
      message += F("<meta charset=\"utf-8\">");
      message += F("<title>"); message += _hostname; message += F("</title>");
    message += F("</head>");

    message += F("<body>");
      message += F("<p>");
      message += F("<a href=\"/\">[Главная страница]</a>");
      message += F("<br><a href=\"/fs\">[FS]</a>&nbsp");
      message += F("<a href=\"/records1s\" target=\"content\">[Память 1сек]</a>&nbsp");
      message += F("<a href=\"/records10s\" target=\"content\">[Память 10сек]</a>&nbsp");
      message += F("<a href=\"/records1m\" target=\"content\">[Память 1мин]</a>&nbsp");
      message += F("<a href=\"/records1mf\" target=\"content\">[Накопитель 1мин]</a>&nbsp");
      message += F("<a href=\"/records10mf\" target=\"content\">[Накопитель 10мин]</a>&nbsp");

      message += F("<br>Uptime seconds = "); message += second_counter;
      message += F("<br>Time now [ctime]= "); message += ctime(&time_unix);
    message += F("</body>");
  message += F("</html>");

  time_make = millis() - time_make;
  int time_send = millis();
  server.send(200, "text/html", message);
  time_send = millis() - time_send;

  //LED_OFF
  Serial.printf("\tmake=% 5dms send=% 5dms, length=% 5d",
    time_make,
    time_send,
    message.length()
    );
}



void handle_content()
{
  //LED_ON
  Serial.printf("\r\nvoid handleRoot()");
  int time_make = millis();

  String message;
  char _buffer[80];
  message += F("<html>");

    message += F("<head>");
      message += F("<meta http-equiv='refresh' content='10'/>");
      message += F("<meta charset=\"utf-8\">");
      message += F("<title>"); message += _hostname; message += F("</title>");
    message += F("</head>");

    message += F("<body>");
      message += F("<p>hello from esp8266!");

      message += F("<p>Uptime seconds = "); message += second_counter;

      message += F("<p>SSID = "); message += WiFi.SSID();
      message += F("<br>RSSI = "); message += WiFi.RSSI();

      message += F("<p>Temperature 1 = "); message += ds_t1;
      message += F("<br>Temperature 2 = "); message += ds_t2;

      message += F("<p>Time now [ctime]= "); message += ctime(&time_unix);
      message += F("<br>Time now [time_t]= "); message += time_unix;
      sprintf(_buffer, "%d/%d/%d %d:%d:%d", year,month,day, hour,minute,second);
      message += F("<br>Time now [timespec]= "); message += _buffer;

      message += F("<p>sizeof(record_t) = "); message += sizeof(record_t);

      message += F("<p>FreeHeap = "); message += ESP.getFreeHeap();
      message += F("<br>CycleCount = "); message += ESP.getCycleCount();
      message += F("<br>millis() = "); message += millis();

      message += F("<p>SketchSize = "); message += ESP.getSketchSize();
      message += F("<br>FreeSketchSpace = "); message += ESP.getFreeSketchSpace();

      message += F("<p>ChipId = "); message += ESP.getChipId();
      message += F("<br>CoreVersion = "); message += ESP.getCoreVersion();
      message += F("<br>CpuFreqMHz = "); message += ESP.getCpuFreqMHz();

      message += F("<p>SdkVersion = ");  message += ESP.getSdkVersion();

      message += F("<p>FlashChipId = "); message += ESP.getFlashChipId();
      message += F("<br>FlashChipSize = "); message += ESP.getFlashChipSize();
      message += F("<br>FlashChipRealSize = "); message += ESP.getFlashChipRealSize();
      message += F("<br>FlashChipSpeed = "); message += ESP.getFlashChipSpeed();
    message += F("</body>");
  message += F("</html>");

  time_make = millis() - time_make;
  int time_send = millis();
  server.send(200, "text/html", message);
  time_send = millis() - time_send;

  //LED_OFF
  Serial.printf("\tmake=% 5dms send=% 5dms, length=% 5d",
    time_make,
    time_send,
    message.length()
    );
}

void handle_records1s()
{
  //LED_ON
  Serial.printf("\r\nvoid handle_records1s()");
  int time_make = millis();

  String message;
  message += F("<html>");

    message += F("<head>");
      message += F("<meta http-equiv='refresh' content='5'/>");
      message += F("<meta charset=\"utf-8\">");
      message += F("<title>"); message += _hostname; message += F("</title>");
    message += F("</head>");

    message += F("<body>");
      message += F("<br><a href=\"/\">[Главная страница]</a>");
      message += F("<br>Текущее время: "); message += time_to_string(time_unix);
      message += F("<p>");
      message += F("<b><a href=\"/records1s\">[Записи 1сек]</a></b>&nbsp");
      message += F("<a href=\"/records10s\">[Записи 10сек]</a>&nbsp");
      message += F("<a href=\"/records1m\">[Записи 1мин]</a>&nbsp");

      message += F("<h1>Записи с периодом 1 секунда</h1>");

      message += F("<p>");


      message += F("<table width=\"800\" border=\"1px\">");
      message += F("<caption>");
        message += F("Записи с периодом 1 секунда");
      message += F("</caption>");
      message += F("<thead>");
        message += F("<tr>");
          message += F("<th>Запись №</th>");
          message += F("<th>Наработка</th>");
          message += F("<th>Дата и время</th>");
          message += F("<th>RSSI</th>");
          message += F("<th>RSSI min</th>");
          message += F("<th>RSSI max</th>");
          message += F("<th>Vbat</th>");
          message += F("<th>Vbat min</th>");
          message += F("<th>Vbat max</th>");
          message += F("<th>T1</th>");
          message += F("<th>T1 min</th>");
          message += F("<th>T1 max</th>");
          message += F("<th>T2</th>");
          message += F("<th>T2 min</th>");
          message += F("<th>T2 max</th>");
        message += F("</tr>");
      message += F("</thead>");

      message += F("<tbody>");

#define TD(x) { message += F("<td>"); message += (x); message += F("</td>"); }
      for (int i = 0; i < RECORDS_PERIOD_1S; i++)
      {
        message += F("<tr>");
          TD(i);
          TD(records_period_1s[i].uptime);
          TD( time_to_string(records_period_1s[i].time_unix) );
          TD(records_period_1s[i].rssi);
          TD(records_period_1s[i].rssi_min);
          TD(records_period_1s[i].rssi_max);
          TD(records_period_1s[i].vbat);
          TD(records_period_1s[i].vbat_min);
          TD(records_period_1s[i].vbat_max);
          TD(records_period_1s[i].t1);
          TD(records_period_1s[i].t1_min);
          TD(records_period_1s[i].t1_max);
          TD(records_period_1s[i].t2);
          TD(records_period_1s[i].t2_min);
          TD(records_period_1s[i].t2_max);
        message += F("</tr>");
      }
#undef TD

      message += F("</tbody>");
      message += F("</table>");


    message += F("</body>");
  message += F("</html>");

  time_make = millis() - time_make;
  int time_send = millis();
  server.send(200, "text/html", message);
  time_send = millis() - time_send;

  //LED_OFF
  Serial.printf("\tmake=% 5dms send=% 5dms, length=% 5d",
    time_make,
    time_send,
    message.length()
    );
}


void handle_records10s()
{
  //LED_ON
  Serial.printf("\r\nvoid handle_records10s()");
  int time_make = millis();

  String message;
  message += F("<html>");

    message += F("<head>");
      message += F("<meta http-equiv='refresh' content='5'/>");
      message += F("<meta charset=\"utf-8\">");
      message += F("<title>"); message += _hostname; message += F("</title>");
    message += F("</head>");

    message += F("<body>");
      message += F("<br><a href=\"/\">[Главная страница]</a>");
      message += F("<br>Текущее время: "); message += time_to_string(time_unix);
      message += F("<p>");
      message += F("<a href=\"/records1s\">[Записи 1сек]</a>&nbsp");
      message += F("<b><a href=\"/records10s\">[Записи 10сек]</a></b>&nbsp");
      message += F("<a href=\"/records1m\">[Записи 1мин]</a>&nbsp");

      message += F("<h1>Записи с периодом 10 секунд</h1>");

      message += F("<p>");


      message += F("<table width=\"800\" border=\"1px\">");
      message += F("<caption>");
        message += F("Записи с периодом 10 секунд");
      message += F("</caption>");
      message += F("<thead>");
        message += F("<tr>");
          message += F("<th>Запись №</th>");
          message += F("<th>Наработка</th>");
          message += F("<th>Дата и время</th>");
          message += F("<th>RSSI</th>");
          message += F("<th>RSSI min</th>");
          message += F("<th>RSSI max</th>");
          message += F("<th>Vbat</th>");
          message += F("<th>Vbat min</th>");
          message += F("<th>Vbat max</th>");
          message += F("<th>T1</th>");
          message += F("<th>T1 min</th>");
          message += F("<th>T1 max</th>");
          message += F("<th>T2</th>");
          message += F("<th>T2 min</th>");
          message += F("<th>T2 max</th>");
        message += F("</tr>");
      message += F("</thead>");

      message += F("<tbody>");

#define TD(x) { message += F("<td>"); message += (x); message += F("</td>"); }
      for (int i = 0; i < RECORDS_PERIOD_10S; i++)
      {
        message += F("<tr>");
          TD(i);
          TD(records_period_10s[i].uptime);
          TD( time_to_string(records_period_10s[i].time_unix) );
          TD(records_period_10s[i].rssi);
          TD(records_period_10s[i].rssi_min);
          TD(records_period_10s[i].rssi_max);
          TD(records_period_10s[i].vbat);
          TD(records_period_10s[i].vbat_min);
          TD(records_period_10s[i].vbat_max);
          TD(records_period_10s[i].t1);
          TD(records_period_10s[i].t1_min);
          TD(records_period_10s[i].t1_max);
          TD(records_period_10s[i].t2);
          TD(records_period_10s[i].t2_min);
          TD(records_period_10s[i].t2_max);
        message += F("</tr>");
      }
#undef TD

      message += F("</tbody>");
      message += F("</table>");


    message += F("</body>");
  message += F("</html>");

  time_make = millis() - time_make;
  int time_send = millis();
  server.send(200, "text/html", message);
  time_send = millis() - time_send;

  //LED_OFF
  Serial.printf("\tmake=% 5dms send=% 5dms, length=% 5d",
    time_make,
    time_send,
    message.length()
    );
}


void handle_records1m()
{
  //LED_ON
  Serial.printf("\r\nvoid handle_records1m()");
  int time_make = millis();

  String message;
  message += F("<html>");

    message += F("<head>");
      message += F("<meta http-equiv='refresh' content='5'/>");
      message += F("<meta charset=\"utf-8\">");
      message += F("<title>"); message += _hostname; message += F("</title>");
    message += F("</head>");

    message += F("<body>");
      message += F("<br><a href=\"/\">[Главная страница]</a>");
      message += F("<br>Текущее время: "); message += time_to_string(time_unix);
      message += F("<p>");
      message += F("<a href=\"/records1s\">[Записи 1сек]</a>&nbsp");
      message += F("<a href=\"/records10s\">[Записи 10сек]</a>&nbsp");
      message += F("<b><a href=\"/records1m\">[Записи 1мин]</a></b>&nbsp");

      message += F("<h1>Записи с периодом 1 минута</h1>");

      message += F("<p>");


      message += F("<table width=\"800\" border=\"1px\">");
      message += F("<caption>");
        message += F("Записи с периодом 1 минута");
      message += F("</caption>");
      message += F("<thead>");
        message += F("<tr>");
          message += F("<th>Запись №</th>");
          message += F("<th>Наработка</th>");
          message += F("<th>Дата и время</th>");
          message += F("<th>RSSI</th>");
          message += F("<th>RSSI min</th>");
          message += F("<th>RSSI max</th>");
          message += F("<th>Vbat</th>");
          message += F("<th>Vbat min</th>");
          message += F("<th>Vbat max</th>");
          message += F("<th>T1</th>");
          message += F("<th>T1 min</th>");
          message += F("<th>T1 max</th>");
          message += F("<th>T2</th>");
          message += F("<th>T2 min</th>");
          message += F("<th>T2 max</th>");
        message += F("</tr>");
      message += F("</thead>");

      message += F("<tbody>");

#define TD(x) { message += F("<td>"); message += (x); message += F("</td>"); }
      for (int i = 0; i < RECORDS_PERIOD_1M; i++)
      {
        message += F("<tr>");
          TD(i);
          TD(records_period_1m[i].uptime);
          TD( time_to_string(records_period_1m[i].time_unix) );
          TD(records_period_1m[i].rssi);
          TD(records_period_1m[i].rssi_min);
          TD(records_period_1m[i].rssi_max);
          TD(records_period_1m[i].vbat);
          TD(records_period_1m[i].vbat_min);
          TD(records_period_1m[i].vbat_max);
          TD(records_period_1m[i].t1);
          TD(records_period_1m[i].t1_min);
          TD(records_period_1m[i].t1_max);
          TD(records_period_1m[i].t2);
          TD(records_period_1m[i].t2_min);
          TD(records_period_1m[i].t2_max);
        message += F("</tr>");
      }
#undef TD

      message += F("</tbody>");
      message += F("</table>");


    message += F("</body>");
  message += F("</html>");

  time_make = millis() - time_make;
  int time_send = millis();
  server.send(200, "text/html", message);
  time_send = millis() - time_send;

  //LED_OFF
  Serial.printf("\tmake=% 5dms send=% 5dms, length=% 5d",
    time_make,
    time_send,
    message.length()
    );
}


void handle_records1mf()
{
  //LED_ON
  Serial.printf("\r\nvoid handle_records1mf()");
  int time_make = millis();

  String message;
  message += F("<html>");

    message += F("<head>");
      message += F("<meta http-equiv='refresh' content='5'/>");
      message += F("<meta charset=\"utf-8\">");
      message += F("<title>"); message += _hostname; message += F("</title>");
    message += F("</head>");

    message += F("<body>");
      message += F("<br><a href=\"/\">[Главная страница]</a>");
      message += F("<br>Текущее время: "); message += time_to_string(time_unix);
      message += F("<p>");
      message += F("<a href=\"/records1s\">[Записи 1сек]</a>&nbsp");
      message += F("<a href=\"/records10s\">[Записи 10сек]</a>&nbsp");
      message += F("<b><a href=\"/records1m\">[Записи 1мин]</a></b>&nbsp");

      message += F("<h1>Записи с периодом 1 минута</h1>");

      message += F("<p>");


      message += F("<table width=\"800\" border=\"1px\">");
      message += F("<caption>");
        message += F("Записи с периодом 1 минута");
      message += F("</caption>");
      message += F("<thead>");
        message += F("<tr>");
          message += F("<th>Запись №</th>");
          message += F("<th>Наработка</th>");
          message += F("<th>Дата и время</th>");
          message += F("<th>RSSI</th>");
          message += F("<th>RSSI min</th>");
          message += F("<th>RSSI max</th>");
          message += F("<th>Vbat</th>");
          message += F("<th>Vbat min</th>");
          message += F("<th>Vbat max</th>");
          message += F("<th>T1</th>");
          message += F("<th>T1 min</th>");
          message += F("<th>T1 max</th>");
          message += F("<th>T2</th>");
          message += F("<th>T2 min</th>");
          message += F("<th>T2 max</th>");
        message += F("</tr>");
      message += F("</thead>");

      message += F("<tbody>");

    record_t rec;
    File f1m = SPIFFS.open(record_1m_filename, "r");
#define TD(x) { message += F("<td>"); message += (x); message += F("</td>"); }
    if (f1m)
    {
      int32_t pos = f1m.size();
      int count = 30;
      pos -= sizeof(rec);
      while ( (pos >= 0) && (count > 0) )
      {
        f1m.seek(pos, SeekSet);
        f1m.read((uint8_t*)&rec, sizeof(rec));
        message += F("<tr>");
          TD(pos / sizeof(rec));
          TD(rec.uptime);
          TD( time_to_string(rec.time_unix) );
          TD(rec.rssi);
          TD(rec.rssi_min);
          TD(rec.rssi_max);
          TD(rec.vbat);
          TD(rec.vbat_min);
          TD(rec.vbat_max);
          TD(rec.t1);
          TD(rec.t1_min);
          TD(rec.t1_max);
          TD(rec.t2);
          TD(rec.t2_min);
          TD(rec.t2_max);
        message += F("</tr>");

        pos -= sizeof(record_t);
        count--;
      }

      f1m.close();
    }
#undef TD

      message += F("</tbody>");
      message += F("</table>");

    message += F("</body>");
  message += F("</html>");

  time_make = millis() - time_make;
  int time_send = millis();
  server.send(200, "text/html", message);
  time_send = millis() - time_send;

  //LED_OFF
  Serial.printf("\tmake=% 5dms send=% 5dms, length=% 5d",
    time_make,
    time_send,
    message.length()
    );
}

void handleFS()
{

  FSInfo fs_info;
  SPIFFS.info(fs_info);

  String message;
  message += F("<html>");

    message += F("<head>");
      message += F("<meta http-equiv='refresh' content='5'/>");
      message += F("<meta charset=\"utf-8\">");
      message += F("<title>"); message += _hostname; message += F("</title>");
    message += F("</head>");

    message += F("<body>");
      message += F("<br><a href=\"/\">[Главная страница]</a>");
      message += F("<br>Текущее время: "); message += time_to_string(time_unix);
      message += F("<p>FS info");
      message += F("<br>Всего байт: "); message += fs_info.totalBytes;
      message += F("<br>Использовано байт: "); message += fs_info.usedBytes;
      message += F("<br>Размер блока: "); message += fs_info.blockSize;
      message += F("<br>Размер страницы: "); message += fs_info.pageSize;
      message += F("<br>Максимально открытых файлов: "); message += fs_info.maxOpenFiles;
      message += F("<br>Максимально длина пути: "); message += fs_info.maxPathLength;

      message += F("<p>FS formatted = "); message += SPIFFS.exists("/.formatted");

      message += F("<p>List of files:");

      Dir dir = SPIFFS.openDir("/");
      while (dir.next()) {
          message += F("<br>&emsp;"); message += dir.fileName();
          File f = dir.openFile("r");
          if (f)
          {
            message += F("&emsp;"); message += f.size(); message += F(" bytes");
          }
      }

    message += F("</body>");
  message += F("</html>");

  server.send(200, "text/html", message);
  //LED_OFF
}

void handleNotFound() {
  //LED_ON
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  //LED_OFF
}

/*
//////////////////////////////////////////////////////////
////////////    //////////////////////////////////////////////

        .oooooo.   ooooooooooooo       .o.
       d8P'  `Y8b  8'   888   `8      .888.
      888      888      888          .8"888.
      888      888      888         .8' `888.
      888      888      888        .88ooo8888.
      `88b    d88'      888       .8'     `888.
       `Y8bood8P'      o888o     o88o     o8888o

//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////
*/
void ArduinoOTA_setup()
{

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(_hostname);

  // No authentication by default
  ArduinoOTA.setPassword(_password);

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("\r\nUnmounting filesystem...");
    SPIFFS.end();
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("\r\nStart updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%% rssi=%i\r", (progress / (total / 100)), WiFi.RSSI());
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

  ArduinoOTA.begin();
  Serial.print("\r\nArduinoOTA started");

}
