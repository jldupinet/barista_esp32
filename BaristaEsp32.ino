#include <WiFi.h>
#include <time.h>

const char* ssid = "Plus";
const char* password = "........";


const char* NTP_SERVER = "pool.ntp.org";
const char* TZ_INFO    = "CST6CDT,M4.1.0,M10.5.0";  // enter your time zone (https://remotemonitoringsystems.ca/time-zone-abbreviations.php)

tm timeinfo;
time_t now;
long unsigned lastNTPtime;
unsigned long lastEntryTime;

long unsigned lastWakeTime = 0;
long lastUpdateTime = 0;

/* BLE Libs */
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

/* HX711 Libs and Config */
#include <HX711_ADC.h>

/* Pin Definition */
const int HX711_dout = 5; 
const int HX711_sck = 4;

//HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);

long t;
float WEIGHT_SCALE_FACTOR = 443.63;

/* Class to save our Extraction State */
#include "Extraction.h"
Extraction currentExtraction;

BLEServer *pServer = NULL;
BLECharacteristic * pTxCharacteristic;
bool deviceConnected = false;
bool oldDeviceConnected = false;
uint8_t txValue = 0;

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // RX Characteristic
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // TX Characteristic

#define LED 2

#define SHOT_BUTTON 26 // Pin to control shot button via relay
#define POWER_BUTTON 27 // Pin to control power button via relay


/* Helper Class to receibe BLE events, called when a device is connected/disconnected */
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

/* Handler for received commands from BLE 
 *  
 *  All Commands come in the following format:
 *  COMMAND,ARG1,ARG2
 *  
 *  COMMAND can be:
 *  > e - for starting an extraction, ex: e,10,30.0
 *  > p - to press power (TODO)
 *  > s - to stop current extraction (TODO)
 *  
 *  ARG1 and ARG2 are floats only used on the "extract" order for the moment
 *  and represent PreInfusion Time (ARG1) and Target Weight (ARG2).

*/
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
        if (rxValue[0] == 'e') {
          std::size_t first_comma = rxValue.find(',');
          std::size_t second_comma = rxValue.find(',', first_comma + 1);

          float preInfuseTime = atof((rxValue.substr(first_comma + 1, second_comma - first_comma - 1)).c_str());
          float targetWeight = atof((rxValue.substr(second_comma + 1, rxValue.length())).c_str());
          currentExtraction = Extraction(preInfuseTime, targetWeight);
        }
      }
    }

};




void setup() {
  Serial.begin(115200);
  
  // Configure machine buttons controlled via relay (ground triggered)
  pinMode(SHOT_BUTTON, OUTPUT);
  pinMode(LED, OUTPUT);
  digitalWrite(SHOT_BUTTON, HIGH);

    WiFi.begin(ssid, password);

  int counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    if (++counter > 100) ESP.restart();
    Serial.print ( "." );
  }
  Serial.println("\n\nWiFi connected\n\n");
  digitalWrite(LED, HIGH);
  delay(200);
  digitalWrite(LED, LOW);
  delay(200);
  digitalWrite(LED, HIGH);
  delay(200);
  digitalWrite(LED, LOW);


  // Create the BLE Device
  BLEDevice::init("UART Service");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );

  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE
                                          );

  pRxCharacteristic->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();

  // Start advertising
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify...");

  Serial.println("Wait...");
  initScale();
  Serial.println("Setup Completed!");


}

void initScale(){
  LoadCell.begin();
  long stabilisingtime = 2000; // tare preciscion can be improved by adding a few seconds of stabilising time
  LoadCell.start(stabilisingtime);
  LoadCell.setCalFactor(WEIGHT_SCALE_FACTOR); // user set calibration factor (float)
}

bool getNTPtime(int sec) {

  {
    uint32_t start = millis();
    do {
      time(&now);
      localtime_r(&now, &timeinfo);
      Serial.print(".");
      delay(10);
    } while (((millis() - start) <= (1000 * sec)) && (timeinfo.tm_year < (2016 - 1900)));
    if (timeinfo.tm_year <= (2016 - 1900)) return false;  // the NTP call was not successful
    //Serial.print("now ");  Serial.println(now);
    char time_output[30];
    strftime(time_output, 30, "%a  %d-%m-%y %T", localtime(&now));
    Serial.println(time_output);
    Serial.println();
  }
  return true;
}

void updateClock(){

    //Serial.printf("Connecting to %s ", ssid);

      //init and get the time
    configTime(0, 0, NTP_SERVER);
    setenv("TZ", TZ_INFO, 1);
    
    if (getNTPtime(10)) {  // wait up to 10sec to sync
    } else {
      Serial.println("Time not set");
      ESP.restart();
    }
//    showTime(timeinfo);
    autoWakeUp(timeinfo);
    lastNTPtime = time(&now);
    lastEntryTime = millis();
//    //disconnect WiFi as it's no longer needed
//    WiFi.disconnect(true);
//    WiFi.mode(WIFI_OFF);
    
  
}

void showTime(tm localTime) {
  Serial.print(localTime.tm_mday);
  Serial.print('/');
  Serial.print(localTime.tm_mon + 1);
  Serial.print('/');
  Serial.print(localTime.tm_year - 100);
  Serial.print('-');
  Serial.print(localTime.tm_hour);
  Serial.print(':');
  Serial.print(localTime.tm_min);
  Serial.print(':');
  Serial.print(localTime.tm_sec);
  Serial.print(" Day of Week ");
  if (localTime.tm_wday == 0)   Serial.println(7);
  else Serial.println(localTime.tm_wday);
}


void autoWakeUp(tm localTime){
  if((now - lastWakeTime) < (61)){
    Serial.println("Too recent");
    pTxCharacteristic->setValue("Too recent");
    pTxCharacteristic->notify();
    return;
  }
  //Serial.println(&localTime, "%A, %B %d %Y %H:%M:%S");
  if(localTime.tm_hour == 20 && localTime.tm_min % 2 == 0){
    Serial.println("Turning ON");
    pTxCharacteristic->setValue("Turning ON");
    pTxCharacteristic->notify();
    digitalWrite(POWER_BUTTON, LOW);
    delay(200);
    digitalWrite(POWER_BUTTON, HIGH);
    lastWakeTime = now;
  } else {
    Serial.println("Not Time Yet");
    pTxCharacteristic->setValue("Not Time Yet");
    pTxCharacteristic->notify();
  }
}

void loop() {

  //update() should be called at least as often as HX711 sample rate; >10Hz@10SPS, >80Hz@80SPS
  //longer delay in sketch will reduce effective sample rate (be carefull with delay() in loop)

  if (deviceConnected) {
    digitalWrite(LED, HIGH);
    if (currentExtraction.shouldExtract) {
      extract();
    }
  } else {
    //update time
    updateClock();
    delay(1000);
  }

  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
    digitalWrite(LED, LOW);

  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }

  //autoWakeUp();
}

/*
   Function to send the latest extraction state via BLE to client
   
   It can be one of the following updates:
   > w - for updating current weight and time elapsed, ex: w,10,11.1
   > f - to signal extraction finished with final time and weight, ex: f,32,30 
*/
void notifyDataUpdate(bool finish) {
  char prefix = finish ? 'f' : 'w';
  char outValue[20];
  sprintf(outValue, "%c,%.2f,%.2f", prefix, currentExtraction.elapsedTime, currentExtraction.currentWeight);
  pTxCharacteristic->setValue(outValue);
  pTxCharacteristic->notify();
  Serial.print("BT> ");
  Serial.println(outValue);
  delay(10);
}

/*
   Obtain a stable weight reading
*/
float getWeight() {
  float weight;

  while (millis() < t + 250) {
    //wait
    LoadCell.update();
  }

  weight = fabs(LoadCell.getData());
  t = millis();
  // do some basic filtering to remove outliers
  if(weight < 1){ // remove very little weights (noise)
    return 0;
  }
  if(weight > 100){ // we know we will never weight more than 100g of espresso
    return currentExtraction.currentWeight;
  }
  if(weight < currentExtraction.currentWeight){ // very likely a measurement error
    return currentExtraction.currentWeight;
  }
  return weight;
}

/*
 * Start Extraction Routine
 * It runs an extraction cycle witht the configured preInfusion Time and target Weight
 * 
 */
void extract() {
  Serial.println("Going to start extracting:");
  Serial.println(currentExtraction.preInfusionTime);
  Serial.println(currentExtraction.targetWeight);
  initScale();
  Serial.println("Tare scale");
  LoadCell.tare();
  Serial.println("Tare complete");
  
  // Run PreInfusion Stage
  unsigned long startPreInfusionTime = millis();
  Serial.println("Sending LOW to relay");
  digitalWrite(SHOT_BUTTON, LOW);
  while ((millis() - startPreInfusionTime) < currentExtraction.preInfusionTime * 1000) {
    currentExtraction.currentWeight = getWeight();
    currentExtraction.elapsedTime = (millis() - startPreInfusionTime) / 1000;
    notifyDataUpdate(false);
    Serial.print(currentExtraction.elapsedTime);
    Serial.print("/");
    Serial.println(currentExtraction.preInfusionTime);
  }
  Serial.println("Sending HIGH to relay");
  digitalWrite(SHOT_BUTTON, HIGH);

  // Run Full Infusion Stage until target weight is reached
  while (currentExtraction.currentWeight < currentExtraction.targetWeight) {
    currentExtraction.currentWeight = getWeight();
    currentExtraction.elapsedTime = (millis() - startPreInfusionTime) / 1000;
    notifyDataUpdate(false);
    Serial.print(currentExtraction.currentWeight);
    Serial.print("/");
    Serial.println(currentExtraction.targetWeight);
  }
  digitalWrite(SHOT_BUTTON, LOW);
  delay(200);
  digitalWrite(SHOT_BUTTON, HIGH);
  notifyDataUpdate(true);
  currentExtraction.shouldExtract = false;
  Serial.println("Done");
}
