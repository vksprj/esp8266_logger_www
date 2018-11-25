
/*
//////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////

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