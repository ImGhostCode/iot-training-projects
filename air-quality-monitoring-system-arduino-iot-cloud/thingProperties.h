#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>

const char DEVICE_LOGIN_NAME[] = "1ace3f50-4315-48e9-927e-f0a9b9a82b5d";

const char SSID[] = SECRET_SSID;
const char PASS[] = SECRET_OPTIONAL_PASS;
const char DEVICE_KEY[] = SECRET_DEVICE_KEY;

void onAirQualityChange();
void onHumidityChange();
void onTemperatureChange();

CloudPercentage air_quality;
CloudRelativeHumidity humidity;
CloudTemperature temperature;

void initProperties(){
  ArduinoCloud.setBoardId(DEVICE_LOGIN_NAME);
  ArduinoCloud.setSecretDeviceKey(DEVICE_KEY);
  ArduinoCloud.addProperty(air_quality, READWRITE, ON_CHANGE,onAirQualityChange);
  ArduinoCloud.addProperty(humidity, READWRITE, ON_CHANGE,onHumidityChange);
  ArduinoCloud.addProperty(temperature, READWRITE, ON_CHANGE,onTemperatureChange);
}

WiFiConnectionHandler ArduinoIoTPreferredConnection(SSID, PASS);