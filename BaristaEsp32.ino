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

//#define LED 2

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
  digitalWrite(SHOT_BUTTON, HIGH);

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

void loop() {

  //update() should be called at least as often as HX711 sample rate; >10Hz@10SPS, >80Hz@80SPS
  //longer delay in sketch will reduce effective sample rate (be carefull with delay() in loop)

  if (deviceConnected) {
    if (currentExtraction.shouldExtract) {
      extract();
    }
  }

  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
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
