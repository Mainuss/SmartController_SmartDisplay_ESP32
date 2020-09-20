// TODO : checksum validation on first full frame
// TODO : BT lock

//////------------------------------------
////// Inludes

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <EEPROM.h>

#include "main.h"
#include "OTA_softap.h"
#include "MedianFilter.h"
#include "dht_nonblocking.h"

//////------------------------------------
////// Defines

#define DEBUG_BLE_SCAN 0
#define DEBUG_BLE_NOTIFY 0
#define DEBUG_DISPLAY_FRAME_LCD_TO_CNTRL 0
#define DEBUG_DISPLAY_FRAME_CNTRL_TO_LCD 0
#define DEBUG_DISPLAY_SPEED 0
#define DEBUG_DISPLAY_MODE 0
#define DEBUG_DISPLAY_BRAKE 0
#define DEBUG_DISPLAY_ECO 0
#define DEBUG_DISPLAY_ACCEL 0
#define DEBUG_DISPLAY_BUTTON1 0
#define DEBUG_DISPLAY_BUTTON2 0
#define DEBUG_DISPLAY_CURRENT 0
#define DEBUG_DISPLAY_DHT 0
#define DEBUG_SERIAL_CHECKSUM_LCD_TO_CNTRL 0
#define DEBUG_SERIAL_CHECKSUM_CNTRL_TO_LCD 0

#define PIN_SERIAL_LCD_TO_ESP 25
#define PIN_SERIAL_ESP_TO_CNTRL 26
#define PIN_SERIAL_CNTRL_TO_ESP 34
#define PIN_SERIAL_ESP_TO_LCD 13
#define PIN_IN_BRAKE 12
#define PIN_IN_VOLTAGE 33
#define PIN_IN_CURRENT 35
#define PIN_IN_BUTTON1 9
#define PIN_IN_BUTTON2 10
#define PIN_IN_DH12 27

#define MODE_LCD_TO_CNTRL 0
#define MODE_CNTRL_TO_LCD 1

#define DATA_BUFFER_SIZE 30
#define BAUD_RATE 1200

#define ANALOG_TO_VOLTS 43.48
#define ANALOG_TO_CURRENT 35
#define NB_CURRENT_CALIB 3000

#define EEPROM_SIZE 1024

#define BLE_MTU 64

// See the following for generating UUIDs: https://www.uuidgenerator.net/
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define SPEED_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a0"
#define MODE_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a1"
#define BRAKE_STATUS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a2"
#define VOLTAGE_STATUS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a3"
#define CURRENT_STATUS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a4"
#define POWER_STATUS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a5"
#define BTLOCK_STATUS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a6"
#define TEMPERATURE_STATUS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a7"
#define HUMIDITY_STATUS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define SETTINGS_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define SPEED_LIMITER_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define ECO_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26ab"
#define ACCEL_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26ac"
#define CURRENT_CALIB_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26ad"

//////------------------------------------
////// Variables

// settings

#pragma pack(push, 1)
struct field_s
{
  /*
  uint8_t Button_1_short_press_action = 0;
  uint8_t Button_1_short_press_action = 0;
  uint8_t Button_1_long_press_action = 0;
  uint8_t Button_2_short_press_action = 0;
  uint8_t Button_2_long_press_action = 0;
  uint16_t Button_long_press_duration = 500;
  uint8_t Bluetooth_lock_mode = 0;
  uint8_t Bluetooth_pin_code = 0;
  char Beacon_Mac_Address[20];
 */
  uint8_t Beacon_range;
  uint8_t Mode_Z_Power_limitation;
  uint8_t Mode_Z_Eco_mode;
  uint8_t Mode_Z_Acceleration;
  uint8_t Electric_brake_progressive_mode;
  uint8_t Electric_brake_min_value;
  uint8_t Electric_brake_max_value;
  uint16_t Electric_brake_time_between_mode_shift;
  uint8_t Electric_brake_disabled_condition;
  uint8_t Electric_brake_disabled_voltage_limit;
  uint8_t Current_loop_mode;
  uint8_t Current_loop_max_current;
  uint8_t Speed_loop_mode;
  uint8_t Speed_limiter_at_startup;
  uint8_t Wheel_size;
  uint8_t Motor_pole_number;

} __attribute__((packed));
#pragma pack(pop)

union settings_bt
{
  struct field_s fields;
  unsigned char buffer[sizeof(struct field_s)];
} settings;

// Time
uint32_t timeLastNotifyBle = 0;
uint32_t timeLastBrake = 0;
uint32_t timeLoop = 0;

int begin_soft = 0;
int begin_hard = 0;

char data_buffer_lcd[DATA_BUFFER_SIZE];
char data_buffer_cntrl[DATA_BUFFER_SIZE];
char data_speed_buffer[4];

HardwareSerial hwSerCntrlToLcd(1);
HardwareSerial hwSerLcdToCntrl(2);

DHT_nonblocking dht_sensor(PIN_IN_DH12, DHT_TYPE_11);

int i_loop = 0;

bool bleLock = false;
bool blePicclyVisible = true;
int8_t bleMinPowerLock = -80;

uint8_t speedLimiter = 1;

float currentHumidity = 0.0;
float currentTemperature = 0.0;

int i_LcdToCntrl = 0;
int i_CntrlToLcd = 0;
int begin_LcdToCntrl = 1;
int begin_CntrlToLcd = 1;

int isModified_LcdToCntrl = 0;
int isModified_CntrlToLcd = 0;

uint8_t speedCurrent = 0;
uint8_t speedOld = 0;
uint8_t fakeSpeed = 25;

uint8_t powerReduction = 0;

uint8_t modeOrder = 3;
uint8_t modeLcd = 0;
uint8_t modeLcdOld = 0;

uint8_t accelOrder = 0;
uint8_t accelLcd = 0;
uint8_t accelLcdOld = 0;

uint8_t ecoOrder = 0;
uint8_t ecoLcd = 0;
uint8_t ecoLcdOld = 0;

uint8_t brakeStatus = 0;
uint8_t brakeStatusOld = 0;
uint8_t breakeSentOrder = -1;

uint8_t currentCalibOrder = 1;
uint32_t iCurrentCalibOrder = 0;

uint8_t button1Status = 0;
uint8_t button2Status = 0;

uint16_t voltageStatus = 0;
uint32_t voltageInMilliVolts = 0;
MedianFilter voltageFilter(100, 0);
MedianFilter currentFilter(100, 0);
MedianFilter currentFilterInit(100, 0);

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristicSpeed = NULL;
BLECharacteristic *pCharacteristicMode = NULL;
BLECharacteristic *pCharacteristicBrakeSentOrder = NULL;
BLECharacteristic *pCharacteristicVoltageStatus = NULL;
BLECharacteristic *pCharacteristicCurrentStatus = NULL;
BLECharacteristic *pCharacteristicPowerStatus = NULL;
BLECharacteristic *pCharacteristicBtlockStatus = NULL;
BLECharacteristic *pCharacteristicTemperatureStatus = NULL;
BLECharacteristic *pCharacteristicHumidityStatus = NULL;
BLECharacteristic *pCharacteristicSettings = NULL;
BLECharacteristic *pCharacteristicSpeedLimiter = NULL;
BLECharacteristic *pCharacteristicEco = NULL;
BLECharacteristic *pCharacteristicAccel = NULL;
BLECharacteristic *pCharacteristicCurrentCalib = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;
BLEScan *pBLEScan;

// BYTE 3 FROM LCD TO CONTRL ... for each sequence number // brute force
const byte modeLcd0[256] = {0x80, 0x05, 0x06, 0x2b, 0x34, 0x29, 0x2a, 0x2f, 0x28, 0x2d, 0x2e, 0x53, 0x7c, 0x51, 0x52, 0x57, 0x50, 0x55, 0x56, 0x7b, 0x84, 0x79, 0x7a, 0x7f, 0x78,
                            0x7d, 0x7e, 0x63, 0x0c, 0x61, 0x62, 0x67, 0x60, 0x65, 0x66, 0x0b, 0x14, 0x09, 0x0a, 0x0f, 0x08, 0x0d, 0x0e, 0x33, 0x5c, 0x31, 0x32, 0x37, 0x30, 0x35, 0x36, 0x5b, 0x64, 0x59, 0x5a,
                            0x5f, 0x58, 0x5d, 0x5e, 0x43, 0x6c, 0x41, 0x42, 0x47, 0x40, 0x45, 0x46, 0x6b, 0x74, 0x69, 0x6a, 0x6f, 0x68, 0x6d, 0x6e, 0x13, 0x3c, 0x11, 0x12, 0x17, 0x10, 0x15, 0x16, 0x3b,
                            0x44, 0x39, 0x3a, 0x3f, 0x38, 0x3d, 0x3e, 0x23, 0x4c, 0x21, 0x22, 0x27, 0x20, 0x25, 0x26, 0x4b, 0x54, 0x49, 0x4a, 0x4f, 0x48, 0x4d, 0x4e, 0x73, 0x1c, 0x71, 0x72, 0x77, 0x70,
                            0x75, 0x76, 0x1b, 0x24, 0x19, 0x1a, 0x1f, 0x18, 0x1d, 0x1e, 0x83, 0x2c, 0x81, 0x82, 0x07, 0x80, 0x05, 0x06, 0x2b, 0x34, 0x29, 0x2a, 0x2f, 0x28, 0x2d, 0x2e, 0x53, 0x7c, 0x51, 0x52,
                            0x57, 0x50, 0x55, 0x56, 0x7b, 0x84, 0x79, 0x7a, 0x7f, 0x78, 0x7d, 0x7e, 0x63, 0x0c, 0x61, 0x62, 0x67, 0x60, 0x65, 0x66, 0x0b, 0x14, 0x09, 0x0a, 0x0f, 0x08, 0x0d, 0x0e, 0x33, 0x5c,
                            0x31, 0x32, 0x37, 0x30, 0x35, 0x36, 0x5b, 0x64, 0x59, 0x5a, 0x5f, 0x58, 0x5d, 0x5e, 0x43, 0x6c, 0x41, 0x42, 0x47, 0x40, 0x45, 0x46, 0x6b, 0x74, 0x69, 0x6a, 0x6f, 0x68, 0x6d,
                            0x6e, 0x13, 0x3c, 0x11, 0x12, 0x17, 0x10, 0x15, 0x16, 0x3b, 0x44, 0x39, 0x3a, 0x3f, 0x38, 0x3d, 0x3e, 0x23, 0x4c, 0x21, 0x22, 0x27, 0x20, 0x25, 0x26, 0x4b, 0x54, 0x49, 0x4a,
                            0x4f, 0x48, 0x4d, 0x4e, 0x73, 0x1c, 0x71, 0x72, 0x77, 0x70, 0x75, 0x76, 0x1b, 0x24, 0x19, 0x1a, 0x1f, 0x18, 0x1d, 0x1e, 0x83, 0x2c, 0x81, 0x82, 0x7};
const byte modeLcd1[256] = {0x85, 0x0a, 0x0b, 0x30, 0x39, 0x2e, 0x2f, 0x34, 0x2d, 0x32, 0x33, 0x58, 0x81, 0x56, 0x57, 0x5c, 0x55, 0x5a, 0x5b, 0x80, 0x89, 0x7e, 0x7f, 0x84,
                            0x7d, 0x82, 0x83, 0x68, 0x11, 0x66, 0x67, 0x6c, 0x65, 0x6a, 0x6b, 0x10, 0x19, 0x0e, 0x0f, 0x14, 0x0d, 0x12, 0x13, 0x38, 0x61, 0x36, 0x37, 0x3c, 0x35, 0x3a, 0x3b, 0x60, 0x69,
                            0x5e, 0x5f, 0x64, 0x5d, 0x62, 0x63, 0x48, 0x71, 0x46, 0x47, 0x4c, 0x45, 0x4a, 0x4b, 0x70, 0x79, 0x6e, 0x6f, 0x74, 0x6d, 0x72, 0x73, 0x18, 0x41, 0x16, 0x17, 0x1c, 0x15, 0x1a,
                            0x1b, 0x40, 0x49, 0x3e, 0x3f, 0x44, 0x3d, 0x42, 0x43, 0x28, 0x51, 0x26, 0x27, 0x2c, 0x25, 0x2a, 0x2b, 0x50, 0x59, 0x4e, 0x4f, 0x54, 0x4d, 0x52, 0x53, 0x78, 0x21, 0x76, 0x77,
                            0x7c, 0x75, 0x7a, 0x7b, 0x20, 0x29, 0x1e, 0x1f, 0x24, 0x1d, 0x22, 0x23, 0x88, 0x31, 0x86, 0x87, 0x0c, 0x85, 0x0a, 0x0b, 0x30, 0x39, 0x2e, 0x2f, 0x34, 0x2d, 0x32, 0x33, 0x58,
                            0x81, 0x56, 0x57, 0x5c, 0x55, 0x5a, 0x5b, 0x80, 0x89, 0x7e, 0x7f, 0x84, 0x7d, 0x82, 0x83, 0x68, 0x11, 0x66, 0x67, 0x6c, 0x65, 0x6a, 0x6b, 0x10, 0x19, 0x0e, 0x0f, 0x14, 0x0d,
                            0x12, 0x13, 0x38, 0x61, 0x36, 0x37, 0x3c, 0x35, 0x3a, 0x3b, 0x60, 0x69, 0x5e, 0x5f, 0x64, 0x5d, 0x62, 0x63, 0x48, 0x71, 0x46, 0x47, 0x4c, 0x45, 0x4a, 0x4b, 0x70, 0x79, 0x6e,
                            0x6f, 0x74, 0x6d, 0x72, 0x73, 0x18, 0x41, 0x16, 0x17, 0x1c, 0x15, 0x1a, 0x1b, 0x40, 0x49, 0x3e, 0x3f, 0x44, 0x3d, 0x42, 0x43, 0x28, 0x51, 0x26, 0x27, 0x2c, 0x25, 0x2a, 0x2b,
                            0x50, 0x59, 0x4e, 0x4f, 0x54, 0x4d, 0x52, 0x53, 0x78, 0x21, 0x76, 0x77, 0x7c, 0x75, 0x7a, 0x7b, 0x20, 0x29, 0x1e, 0x1f, 0x24, 0x1d, 0x22, 0x23, 0x88, 0x31, 0x86, 0x87, 0x0c};
const byte modeLcd2[256] = {0x8a, 0x0f, 0x10, 0x35, 0x3e, 0x33, 0x34, 0x39, 0x32, 0x37, 0x38, 0x5d, 0x86, 0x5b, 0x5c, 0x61, 0x5a, 0x5f, 0x60, 0x85, 0x8e, 0x83, 0x84, 0x89, 0x82, 0x87,
                            0x88, 0x6d, 0x16, 0x6b, 0x6c, 0x71, 0x6a, 0x6f, 0x70, 0x15, 0x1e, 0x13, 0x14, 0x19, 0x12, 0x17, 0x18, 0x3d, 0x66, 0x3b, 0x3c, 0x41, 0x3a, 0x3f, 0x40, 0x65, 0x6e, 0x63, 0x64,
                            0x69, 0x62, 0x67, 0x68, 0x4d, 0x76, 0x4b, 0x4c, 0x51, 0x4a, 0x4f, 0x50, 0x75, 0x7e, 0x73, 0x74, 0x79, 0x72, 0x77, 0x78, 0x1d, 0x46, 0x1b, 0x1c, 0x21, 0x1a, 0x1f, 0x20, 0x45,
                            0x4e, 0x43, 0x44, 0x49, 0x42, 0x47, 0x48, 0x2d, 0x56, 0x2b, 0x2c, 0x31, 0x2a, 0x2f, 0x30, 0x55, 0x5e, 0x53, 0x54, 0x59, 0x52, 0x57, 0x58, 0x7d, 0x26, 0x7b, 0x7c, 0x81, 0x7a,
                            0x7f, 0x80, 0x25, 0x2e, 0x23, 0x24, 0x29, 0x22, 0x27, 0x28, 0x8d, 0x36, 0x8b, 0x8c, 0x11, 0x8a, 0x0f, 0x10, 0x35, 0x3e, 0x33, 0x34, 0x39, 0x32, 0x37, 0x38, 0x5d, 0x86, 0x5b,
                            0x5c, 0x61, 0x5a, 0x5f, 0x60, 0x85, 0x8e, 0x83, 0x84, 0x89, 0x82, 0x87, 0x88, 0x6d, 0x16, 0x6b, 0x6c, 0x71, 0x6a, 0x6f, 0x70, 0x15, 0x1e, 0x13, 0x14, 0x19, 0x12, 0x17, 0x18,
                            0x3d, 0x66, 0x3b, 0x3c, 0x41, 0x3a, 0x3f, 0x40, 0x65, 0x6e, 0x63, 0x64, 0x69, 0x62, 0x67, 0x68, 0x4d, 0x76, 0x4b, 0x4c, 0x51, 0x4a, 0x4f, 0x50, 0x75, 0x7e, 0x73, 0x74, 0x79,
                            0x72, 0x77, 0x78, 0x1d, 0x46, 0x1b, 0x1c, 0x21, 0x1a, 0x1f, 0x20, 0x45, 0x4e, 0x43, 0x44, 0x49, 0x42, 0x47, 0x48, 0x2d, 0x56, 0x2b, 0x2c, 0x31, 0x2a, 0x2f, 0x30, 0x55, 0x5e,
                            0x53, 0x54, 0x59, 0x52, 0x57, 0x58, 0x7d, 0x26, 0x7b, 0x7c, 0x81, 0x7a, 0x7f, 0x80, 0x25, 0x2e, 0x23, 0x24, 0x29, 0x22, 0x27, 0x28, 0x8d, 0x36, 0x8b, 0x8c, 0x11};

//////------------------------------------
//////------------------------------------
////// Setups

class BLEServerCallback : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    Serial.println("BLE connected");
    deviceConnected = true;

    if (blePicclyVisible)
    {
      bleLock = false;
      Serial.println(" ==> device connected ==> UNLOCK decision");
      Serial.println("-------------------------------------");
    }
    else
    {
      bleLock = false;
      Serial.println(" ==> device connected but PICLLY invisible ==> UNLOCK decision");
      Serial.println("-------------------------------------");
    }

    // notify of current modes / values (for value not uptate by LCD)
    pCharacteristicSpeedLimiter->setValue((uint8_t *)&speedLimiter, 1);
    pCharacteristicSpeedLimiter->notify();
  };

  void onDisconnect(BLEServer *pServer)
  {
    Serial.println("BLE disonnected");
    deviceConnected = false;

    if (blePicclyVisible)
    {
      bleLock = false;

      Serial.println(" ==> device disconnected but PICLLY visible ==> UNLOCK decision");
      Serial.println("-------------------------------------");
    }
    else
    {
      bleLock = true;

      Serial.println(" ==> device disconnected but PICLLY invisible ==> LOCK decision");
      Serial.println("-------------------------------------");
    }
  }
};

class BLEAdvertisedDeviceCallback : public BLEAdvertisedDeviceCallbacks
{
  /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    //Serial.print("BLE Advertised Device found: ");
    //Serial.println(advertisedDevice.toString().c_str());
    /*
    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.getServiceUUID().equals(SERVICE_UUID)) {

      //
      Serial.print("Found our device!  address: ");
      advertisedDevice.getScan()->stop();

      pServerAddress = new BLEAddress(advertisedDevice.getAddress());
      doConnect = true;

    } // Found our server
    */
  } // onResult
};  // MyAdvertisedDeviceCallbacks

class BLECharacteristicCallback : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pCharacteristic)
  {
    if (pCharacteristic->getUUID().toString() == MODE_CHARACTERISTIC_UUID)
    {
      std::string rxValue = pCharacteristic->getValue();
      modeOrder = rxValue[0];

      char print_buffer[500];
      sprintf(print_buffer, "%02x", modeOrder);
      Serial.print("Write mode : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == SETTINGS_CHARACTERISTIC_UUID)
    {
      std::string rxValue = pCharacteristic->getValue();

      for (int i = 0; i < rxValue.length(); i++)
      {
        settings.buffer[i] = rxValue[i];
      }

      //memcpy(&settings.buffer, &rxValue, sizeof(settings.buffer));

      Serial.print("Settings len : ");
      Serial.println(rxValue.length());
      Serial.print("Settings size : ");
      Serial.println(rxValue.size());

      Serial.print("Settings : ");
      for (int i = 0; i < rxValue.length(); i++)
      {
        char print_buffer[5];
        sprintf(print_buffer, "%02x ", rxValue[i]);
        Serial.print(print_buffer);
      }
      Serial.println("");

      displaySettings();

      saveSettings();
    }
    else if (pCharacteristic->getUUID().toString() == SPEED_LIMITER_CHARACTERISTIC_UUID)
    {
      std::string rxValue = pCharacteristic->getValue();
      speedLimiter = rxValue[0];

      char print_buffer[500];
      sprintf(print_buffer, "%02x", speedLimiter);
      Serial.print("Write speedLimiter : ");
      Serial.println(print_buffer);

      // notify of current value
      pCharacteristicSpeedLimiter->setValue((uint8_t *)&speedLimiter, 1);
      pCharacteristicSpeedLimiter->notify();
    }
    else if (pCharacteristic->getUUID().toString() == ECO_CHARACTERISTIC_UUID)
    {
      std::string rxValue = pCharacteristic->getValue();
      ecoOrder = rxValue[0];

      char print_buffer[500];
      sprintf(print_buffer, "%02x", ecoOrder);
      Serial.print("Write eco : ");
      Serial.println(print_buffer);

      // notify of current value
      pCharacteristicEco->setValue((uint8_t *)&ecoOrder, 1);
      pCharacteristicEco->notify();
    }
    else if (pCharacteristic->getUUID().toString() == ACCEL_CHARACTERISTIC_UUID)
    {
      std::string rxValue = pCharacteristic->getValue();
      accelOrder = rxValue[0];

      char print_buffer[500];
      sprintf(print_buffer, "%02x", accelOrder);
      Serial.print("Write accel : ");
      Serial.println(print_buffer);

      // notify of current value
      pCharacteristicAccel->setValue((uint8_t *)&accelOrder, 1);
      pCharacteristicAccel->notify();
    }
    else if (pCharacteristic->getUUID().toString() == CURRENT_CALIB_CHARACTERISTIC_UUID)
    {
      std::string rxValue = pCharacteristic->getValue();
      currentCalibOrder = rxValue[0];

      char print_buffer[500];
      sprintf(print_buffer, "%02x", currentCalibOrder);
      Serial.print("Write currentCalibOrder : ");
      Serial.println(print_buffer);
    }
  }

  void onRead(BLECharacteristic *pCharacteristic)
  {

    if (pCharacteristic->getUUID().toString() == MODE_CHARACTERISTIC_UUID)
    {
      pCharacteristicMode->setValue((uint8_t *)&modeOrder, 1);

      char print_buffer[500];
      sprintf(print_buffer, "%02x", modeOrder);
      Serial.print("Read mode : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == SPEED_CHARACTERISTIC_UUID)
    {
      pCharacteristicSpeed->setValue((uint8_t *)&speedCurrent, 1);

      char print_buffer[500];
      sprintf(print_buffer, "%02x", speedCurrent);
      Serial.print("Read speed : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == BRAKE_STATUS_CHARACTERISTIC_UUID)
    {
      pCharacteristicBrakeSentOrder->setValue((uint8_t *)&breakeSentOrder, 1);

      char print_buffer[500];
      sprintf(print_buffer, "%02x", breakeSentOrder);
      Serial.print("Read breakeSentOrder : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == VOLTAGE_STATUS_CHARACTERISTIC_UUID)
    {
      uint32_t voltage = voltageFilter.getMean();
      pCharacteristicVoltageStatus->setValue((uint8_t *)&voltage, 4);

      char print_buffer[500];
      sprintf(print_buffer, "%02f", voltage / 1000.0);
      Serial.print("Read voltage : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == CURRENT_STATUS_CHARACTERISTIC_UUID)
    {
      int32_t current = currentFilter.getMean();
      pCharacteristicCurrentStatus->setValue((uint8_t *)&current, 4);

      char print_buffer[500];
      sprintf(print_buffer, "%02d", current);
      Serial.print("Read current : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == POWER_STATUS_CHARACTERISTIC_UUID)
    {
      int32_t current = currentFilter.getMean();
      uint32_t voltage = voltageFilter.getMean();
      int32_t power = (current / 1000.0) * (voltage / 1000.0);
      pCharacteristicPowerStatus->setValue((uint8_t *)&power, 4);

      char print_buffer[500];
      sprintf(print_buffer, "%04d", power);
      Serial.print("Read power : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == BTLOCK_STATUS_CHARACTERISTIC_UUID)
    {
      pCharacteristicBtlockStatus->setValue((uint8_t *)&bleLock, 1);

      char print_buffer[500];
      sprintf(print_buffer, "%02x", bleLock);
      Serial.print("Read bleLock : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == TEMPERATURE_STATUS_CHARACTERISTIC_UUID)
    {
      int32_t temp = currentTemperature * 1000.0;
      pCharacteristicTemperatureStatus->setValue((uint8_t *)&temp, 4);

      char print_buffer[500];
      sprintf(print_buffer, "%f", currentTemperature);
      Serial.print("Read currentTemperature : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == HUMIDITY_STATUS_CHARACTERISTIC_UUID)
    {
      int32_t temp = currentHumidity * 1000.0;
      pCharacteristicHumidityStatus->setValue((uint8_t *)&temp, 4);

      char print_buffer[500];
      sprintf(print_buffer, "%f", currentHumidity);
      Serial.print("Read currentHumidity : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == SPEED_LIMITER_CHARACTERISTIC_UUID)
    {
      pCharacteristicHumidityStatus->setValue((uint8_t *)&speedLimiter, 1);

      char print_buffer[500];
      sprintf(print_buffer, "%d", speedLimiter);
      Serial.print("Read speedLimiter : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == ECO_CHARACTERISTIC_UUID)
    {
      pCharacteristicEco->setValue((uint8_t *)&ecoOrder, 1);

      char print_buffer[500];
      sprintf(print_buffer, "%d", ecoOrder);
      Serial.print("Read eco : ");
      Serial.println(print_buffer);
    }
    else if (pCharacteristic->getUUID().toString() == ACCEL_CHARACTERISTIC_UUID)
    {
      pCharacteristicAccel->setValue((uint8_t *)&accelOrder, 1);

      char print_buffer[500];
      sprintf(print_buffer, "%d", accelOrder);
      Serial.print("Read accel : ");
      Serial.println(print_buffer);
    }
  }
};

void bleOnScanResults(BLEScanResults scanResults)
{
  Serial.print("BLE Scan Device found: ");
  Serial.println(scanResults.getCount());

  bool newBlePicclyVisible = false;

  for (int i = 0; i < scanResults.getCount(); i++)
  {
    String name = scanResults.getDevice(i).getName().c_str();
    int rssi = scanResults.getDevice(i).getRSSI();
    std::string address = scanResults.getDevice(i).getAddress().toString();
    String addressStr = address.c_str();

#if DEBUG_BLE_SCAN
    Serial.print("BLE device : ");
    Serial.print(name);
    Serial.print(" / adress : ");
    Serial.print(addressStr);
    Serial.print(" / name : ");
    Serial.print(name);
    Serial.print(" / rssi ");
    Serial.println(rssi);
#endif

    if (addressStr == "ac:23:3f:56:ec:6c")
    {
      if (rssi < bleMinPowerLock)
      {
#if DEBUG_BLE_SCAN
        Serial.println(" ==> PICC-LY found ... but too far away ==> lock from scan");
        newBlePicclyVisible = false;
#endif
      }
      else
      {
#if DEBUG_BLE_SCAN
        Serial.println(" ==> PICC-LY found ==> unlock from scan");
#endif
        newBlePicclyVisible = true;
      }
    }
  }

  // store piclyy status
  blePicclyVisible = newBlePicclyVisible;

  // launch new scan
  pBLEScan->start(20, &bleOnScanResults, false);

  // set BT lock
  if ((!deviceConnected))
  {
    if (!blePicclyVisible)
    {
      bleLock = true;
      Serial.println(" ==> no device connected and PICC-LY no found ==> LOCK decision");
    }
    else
    {
      bleLock = false;
      Serial.println(" ==> no device connected and PICC-LY found ==> UNLOCK decision");
    }
  }

  Serial.println("-------------------------------------");
}

void setupPins()
{
  pinMode(PIN_IN_BRAKE, INPUT);
  pinMode(PIN_IN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_IN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_IN_VOLTAGE, INPUT);
  pinMode(PIN_IN_CURRENT, INPUT);

  //pinMode(PIN_IN_DH12, INPUT_PULLUP);
  pinMode(14, OUTPUT);
}

void setupBLE()
{

  // Create the BLE Device
  Serial.println("init");
  BLEDevice::init("SmartLCD");
  Serial.println("mtu set 2");
  BLEDevice::setMTU(BLE_MTU);

  int mtu = BLEDevice::getMTU();
  Serial.print("MTU : ");
  Serial.println(mtu);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BLEServerCallback());

  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID), 50);

  // Create a BLE Characteristic
  pCharacteristicSpeed = pService->createCharacteristic(
      SPEED_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicMode = pService->createCharacteristic(
      MODE_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicBrakeSentOrder = pService->createCharacteristic(
      BRAKE_STATUS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicVoltageStatus = pService->createCharacteristic(
      VOLTAGE_STATUS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicCurrentStatus = pService->createCharacteristic(
      CURRENT_STATUS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicPowerStatus = pService->createCharacteristic(
      POWER_STATUS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicBtlockStatus = pService->createCharacteristic(
      BTLOCK_STATUS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicTemperatureStatus = pService->createCharacteristic(
      TEMPERATURE_STATUS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicHumidityStatus = pService->createCharacteristic(
      HUMIDITY_STATUS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicSettings = pService->createCharacteristic(
      SETTINGS_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicSpeedLimiter = pService->createCharacteristic(
      SPEED_LIMITER_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicEco = pService->createCharacteristic(
      ECO_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicAccel = pService->createCharacteristic(
      ACCEL_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicCurrentCalib = pService->createCharacteristic(
      CURRENT_CALIB_CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_READ);

  pCharacteristicSpeed->addDescriptor(new BLE2902());
  pCharacteristicMode->addDescriptor(new BLE2902());
  pCharacteristicBrakeSentOrder->addDescriptor(new BLE2902());
  pCharacteristicVoltageStatus->addDescriptor(new BLE2902());
  pCharacteristicCurrentStatus->addDescriptor(new BLE2902());
  pCharacteristicPowerStatus->addDescriptor(new BLE2902());
  pCharacteristicBtlockStatus->addDescriptor(new BLE2902());
  pCharacteristicTemperatureStatus->addDescriptor(new BLE2902());
  pCharacteristicHumidityStatus->addDescriptor(new BLE2902());
  pCharacteristicSettings->addDescriptor(new BLE2902());
  pCharacteristicSpeedLimiter->addDescriptor(new BLE2902());
  pCharacteristicEco->addDescriptor(new BLE2902());
  pCharacteristicAccel->addDescriptor(new BLE2902());
  pCharacteristicCurrentCalib->addDescriptor(new BLE2902());

  pCharacteristicSpeed->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicMode->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicBrakeSentOrder->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicVoltageStatus->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicCurrentStatus->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicPowerStatus->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicBtlockStatus->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicTemperatureStatus->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicHumidityStatus->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicSettings->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicSpeedLimiter->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicEco->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicAccel->setCallbacks(new BLECharacteristicCallback());
  pCharacteristicCurrentCalib->setCallbacks(new BLECharacteristicCallback());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");

  // Start scanning
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new BLEAdvertisedDeviceCallback());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(20, &bleOnScanResults, false);
}

void setupSerial()
{

  //  swSerLcdToCntrl.begin(BAUD_RATE, SWSERIAL_8N1, PIN_SERIAL_LCD_TO_CNTRL_RX, SERIAL_LCD_TO_CNTRL_TXPIN, false, 256);
  hwSerLcdToCntrl.begin(BAUD_RATE, SERIAL_8N1, PIN_SERIAL_LCD_TO_ESP, PIN_SERIAL_ESP_TO_CNTRL);

  //  swSerCntrlToLcd.begin(BAUD_RATE, SWSERIAL_8N1, SERIAL_CNTRL_TO_LCD_RXPIN, SERIAL_CNTRL_TO_LCD_TXPIN, false, 256);
  hwSerCntrlToLcd.begin(BAUD_RATE, SERIAL_8N1, PIN_SERIAL_CNTRL_TO_ESP, PIN_SERIAL_ESP_TO_LCD);
}

void setupEPROMM()
{
  EEPROM.begin(EEPROM_SIZE);
}

void setupOTA()
{
  OTA_setup();
}

void initDataWithSettings()
{
  speedLimiter = (settings.fields.Speed_limiter_at_startup == 1);
}

////// Setup
void setup()
{

  // Initialize the Serial (use only in setup codes)
  Serial.begin(115200);
  Serial.println(PSTR("\n\nsetup --- begin"));

  timeLastNotifyBle = millis();

  Serial.println(PSTR("   serial ..."));
  setupSerial();
  delay(10);

  Serial.println(PSTR("   BLE ..."));
  setupBLE();
  delay(10);

  Serial.println(PSTR("   pins ..."));
  setupPins();
  delay(10);

  Serial.println(PSTR("   eeprom ..."));
  setupEPROMM();

  Serial.println(PSTR("   settings ..."));
  restoreSettings();
  displaySettings();

  Serial.println(PSTR("   init data with settings ..."));
  initDataWithSettings();

/*
  Serial.println(PSTR("   setup OTA ..."));
  setupOTA();
*/

  // End off setup
  Serial.println("setup --- end");
}

//////------------------------------------
//////------------------------------------
////// Various functions

void saveSettings()
{
  Serial.print("saveSettings : ");
  Serial.print(sizeof(settings));
  Serial.println(" bytes");

  EEPROM.writeBytes(0, settings.buffer, sizeof(settings));
  EEPROM.commit();
}

void restoreSettings()
{

  Serial.print("restoreSettings");
  Serial.println(sizeof(settings));

  EEPROM.readBytes(0, settings.buffer, sizeof(settings));
}

uint8_t getCheckSum(char *string)
{
  byte rtn = 0;

  for (byte i = 0; i < 14; i++)
  {
    rtn ^= string[i];
  }

  return rtn;
}

void displaySettings()
{
  Serial.print("// Beacon_range : ");
  Serial.println(settings.fields.Beacon_range);
  Serial.print("// Mode_Z_Power_limitation : ");
  Serial.println(settings.fields.Mode_Z_Power_limitation);
  Serial.print("// Mode_Z_Eco_mode : ");
  Serial.println(settings.fields.Mode_Z_Eco_mode);
  Serial.print("// Mode_Z_Acceleration : ");
  Serial.println(settings.fields.Mode_Z_Acceleration);
  Serial.print("// Electric_brake_progressive_mode : ");
  Serial.println(settings.fields.Electric_brake_progressive_mode);
  Serial.print("// Electric_brake_min_value : ");
  Serial.println(settings.fields.Electric_brake_min_value);
  Serial.print("// Electric_brake_max_value : ");
  Serial.println(settings.fields.Electric_brake_max_value);
  Serial.print("// Electric_brake_time_between_mode_shift : ");
  Serial.println(settings.fields.Electric_brake_time_between_mode_shift);
  Serial.print("// Electric_brake_disabled_condition : ");
  Serial.println(settings.fields.Electric_brake_disabled_condition);
  Serial.print("// Electric_brake_disabled_voltage_limit : ");
  Serial.println(settings.fields.Electric_brake_disabled_voltage_limit);
  Serial.print("// Current_loop_mode : ");
  Serial.println(settings.fields.Current_loop_mode);
  Serial.print("// Current_loop_max_current : ");
  Serial.println(settings.fields.Current_loop_max_current);
  Serial.print("// Speed_loop_mode : ");
  Serial.println(settings.fields.Speed_loop_mode);
  Serial.print("// Speed_limiter_at_startup : ");
  Serial.println(settings.fields.Speed_limiter_at_startup);
  Serial.print("// Wheel_size : ");
  Serial.println(settings.fields.Wheel_size);
  Serial.print("// Motor_pole_number : ");
  Serial.println(settings.fields.Motor_pole_number);
}

void displayFrame(int mode, char data_buffer[], byte checksum)
{

  char print_buffer[500];

  // for excel
  sprintf(print_buffer, "(%d) %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x / %02x",
          mode,
          data_buffer[0],
          data_buffer[1],
          data_buffer[2],
          data_buffer[3],
          data_buffer[4],
          data_buffer[5],
          data_buffer[6],
          data_buffer[7],
          data_buffer[8],
          data_buffer[9],
          data_buffer[10],
          data_buffer[11],
          data_buffer[12],
          data_buffer[13],
          data_buffer[14],
          checksum);

  Serial.println(print_buffer);
}

void displaySpeed()
{

  Serial.print("speedCurrent : ");
  Serial.print(speedCurrent);
  Serial.println("");
}

void displayBrake()
{
  Serial.print("Brake : ");
  Serial.println(brakeStatus);
}

void displayButton1()
{
  Serial.print("Button1 : ");
  Serial.println(button1Status);
}

void displayButton2()
{
  Serial.print("Button2 : ");
  Serial.println(button2Status);
}

void displayMode(char data_buffer[])
{

  uint32_t byteDiff = (data_buffer[5] - data_buffer[2]);
  uint8_t modeLcd = byteDiff & 0x03;
  uint8_t modeLcd2 = (byteDiff >> 2) & 0x1;
  uint8_t modeLcd3 = (byteDiff >> 3) & 0x1;
  uint8_t modeLcd4 = (byteDiff >> 4) & 0x1;
  uint8_t modeLcd5 = (byteDiff >> 5) & 0x1;
  uint8_t modeLcd6 = (byteDiff >> 6) & 0x1;
  uint8_t modeLcd7 = (byteDiff >> 7) & 0x1;

  char print_buffer[500];
  sprintf(print_buffer, "%02x %02x / %02x / %02x / %02x / %02x / %02x / %02x / %02x", data_buffer[2], data_buffer[5], modeLcd, modeLcd2, modeLcd3, modeLcd4, modeLcd5, modeLcd6, modeLcd7);

#if DEBUG_DISPLAY_MODE
  Serial.print("LCD mode : ");
  Serial.print(print_buffer);
  Serial.println("");
#endif
}

uint8_t modifyModeOld(char var, char data_buffer[])
{
  uint32_t byteDiff = (var - data_buffer[2]);
  uint8_t modeLcd = byteDiff & 0x03;
  uint8_t modeLcdMask = byteDiff & 0xfc;
  uint8_t newmodeLcd2 = modeOrder | modeLcdMask;
  uint32_t newmodeLcd3 = (newmodeLcd2 + data_buffer[2]) & 0xff;

  char print_buffer[500];
  /*
  sprintf(print_buffer, "%02x %02x / %s %02x / %s %02x / %s %02x / %s %02x  / %s %02x  / %s %02x ",
          data_buffer[2],
          var,
          "byteDiff",
          byteDiff,
          "lcd",
          modeLcd,
          "mask",
          modeLcdMask,
          "order",
          orderMode,
          "newmodeLcd2",
          newmodeLcd2,
          "newmodeLcd3",
          newmodeLcd3);
          */

  sprintf(print_buffer, "%s %02x / %s %02x",
          "lcd",
          modeLcd,
          "order",
          modeOrder);

  Serial.print("LCD mode : ");
  Serial.print(print_buffer);
  Serial.println("");

  return newmodeLcd3;
}

uint8_t getMode(char var, char data_buffer[])
{
  uint32_t byteDiff = (var - data_buffer[2]);
  uint8_t modeLcd = (byteDiff & 0x03) + 1;

  char print_buffer[500];
  sprintf(print_buffer, "%s %02x / %s %02x",
          "lcd",
          modeLcd,
          "order",
          modeOrder);

#if DEBUG_DISPLAY_MODE
  Serial.print("LCD mode : ");
  Serial.print(print_buffer);
  Serial.println("");
#endif

  return modeLcd;
}

uint8_t modifyMode(char var, char data_buffer[])
{
  uint8_t newmodeLcd3;

  uint32_t byteDiff = (var - data_buffer[2]);
  uint8_t modeLcd = (byteDiff & 0x03) + 1;

  // override Smartphone mode with LCD mode
  if (modeLcdOld != modeLcd)
  {
    modeOrder = modeLcd;
    modeLcdOld = modeLcd;

    // notify bluetooth
    pCharacteristicMode->setValue((uint8_t *)&modeOrder, 1);
    pCharacteristicMode->notify();
  }

  if (modeOrder == 1)
    newmodeLcd3 = modeLcd0[(uint8_t)(data_buffer[2])];
  else if (modeOrder == 2)
    newmodeLcd3 = modeLcd1[(uint8_t)(data_buffer[2])];
  else if (modeOrder == 3)
    newmodeLcd3 = modeLcd2[(uint8_t)(data_buffer[2])];
  else
    newmodeLcd3 = modeLcd2[(uint8_t)(data_buffer[2])];

  return newmodeLcd3;
}

uint8_t modifyPower(char var, char data_buffer[])
{
  uint8_t newPower;

  // lock escooter by reducing power to 5%
  if (bleLock == true)
  {
    newPower = 5;
  }
  else if (speedLimiter == 1)
  {
    newPower = 37;

    /*
    Serial.print("Speed : ");
    Serial.print(speedCurrent);
    Serial.print(" / Power reduction : ");
    Serial.print(powerReduction);
    Serial.print(" / newPower : ");
    Serial.println(newPower);
    */
  }
  else
  {
    newPower = var;
  }

  return newPower;
}

uint8_t getBrakeFromLCD(char var, char data_buffer[])
{

  uint8_t brake = (var - data_buffer[3]) & 0x20;
  uint8_t brakeStatusNew = brake >> 5;

  //uint8_t brakeStatusNew = brakeStatus;
  if ((brakeStatusNew == 1) && (brakeStatusOld == 0))
  {
    brakeStatus = brakeStatusNew;
    timeLastBrake = millis();

#if DEBUG_DISPLAY_BRAKE
    Serial.print("Brake pressed at : ");
    Serial.println(timeLastBrake);
#endif

    // notify bluetooth
    pCharacteristicBrakeSentOrder->setValue((uint8_t *)&breakeSentOrder, 1);
    pCharacteristicBrakeSentOrder->notify();
  }
  else if ((brakeStatusNew == 0) && (brakeStatusOld == 1))
  {
    brakeStatus = brakeStatusNew;

    // reset to min
    breakeSentOrder = settings.fields.Electric_brake_min_value;

#if DEBUG_DISPLAY_BRAKE
    Serial.print("Brake released at : ");
    Serial.println(millis());
#endif

    // notify bluetooth
    pCharacteristicBrakeSentOrder->setValue((uint8_t *)&breakeSentOrder, 1);
    pCharacteristicBrakeSentOrder->notify();
  }

  brakeStatusOld = brakeStatusNew;
  brakeStatus = brakeStatusNew;

  /*
  char print_buffer[500];
  sprintf(print_buffer, "%s %02x / %s %02x / %s %02x",
          "var",
          var,
          "data_buffer[3]",
          data_buffer[3],
          "brake",
          brake);

  Serial.print("Brake : ");
  Serial.print(print_buffer);
  Serial.println("");
*/

  return brake;
}

uint8_t modifyBrake(char var, char data_buffer[])
{

  uint32_t currentTime = millis();

  if (settings.fields.Electric_brake_progressive_mode == 1)
  {
    if (brakeStatus == 1)
    {
      if (breakeSentOrder < settings.fields.Electric_brake_max_value)
      {
        if (currentTime - timeLastBrake > settings.fields.Electric_brake_time_between_mode_shift * 5)
        {
          breakeSentOrder = settings.fields.Electric_brake_min_value + 5;
        }
        else if (currentTime - timeLastBrake > settings.fields.Electric_brake_time_between_mode_shift * 4)
        {
          breakeSentOrder = settings.fields.Electric_brake_min_value + 4;
        }
        else if (currentTime - timeLastBrake > settings.fields.Electric_brake_time_between_mode_shift * 3)
        {
          breakeSentOrder = settings.fields.Electric_brake_min_value + 3;
        }
        else if (currentTime - timeLastBrake > settings.fields.Electric_brake_time_between_mode_shift * 2)
        {
          breakeSentOrder = settings.fields.Electric_brake_min_value + 2;
        }
        else if (currentTime - timeLastBrake > settings.fields.Electric_brake_time_between_mode_shift * 1)
        {
          breakeSentOrder = settings.fields.Electric_brake_min_value + 1;
        }
      }

      // notify bluetooth
      pCharacteristicBrakeSentOrder->setValue((uint8_t *)&breakeSentOrder, 1);
      pCharacteristicBrakeSentOrder->notify();
    }
    else
    // progressive brake enabled but brake released
    {
      breakeSentOrder = settings.fields.Electric_brake_min_value;
    }
  }
  else
  // progressive brake disabled
  {
    breakeSentOrder = var;
  }

#if DEBUG_DISPLAY_BRAKE
  char print_buffer[500];
  sprintf(print_buffer, "%s %02x %s %02x %s %02x %s %d %s %d %s %d",
          "Brake Status : ",
          brakeStatus,
          " / breakeSentOrder  : ",
          breakeSentOrder,
          " / Current LCD brake  : ",
          var,
          " / timeLastBrake  : ",
          timeLastBrake,
          " / currentTime  : ",
          currentTime,
          " / timeDiff  : ",
          currentTime - timeLastBrake);

  Serial.println(print_buffer);
#endif

  return breakeSentOrder;
}

uint8_t modifyEco(char var, char data_buffer[])
{

  ecoLcd = var;
  var = ecoOrder;

  // override Smartphone mode with LCD mode
  if (ecoLcd != ecoLcdOld)
  {
    ecoOrder = ecoLcd;
    ecoLcdOld = ecoLcd;

    // notify bluetooth
    pCharacteristicEco->setValue((uint8_t *)&ecoOrder, 1);
    pCharacteristicEco->notify();
  }

#if DEBUG_DISPLAY_ECO
  char print_buffer[500];
  sprintf(print_buffer, "%s %02x",
          "Eco Status : ",
          var);

  Serial.println(print_buffer);
#endif

  return var;
}

uint8_t modifyAccel(char var, char data_buffer[])
{

  accelLcd = var;
  var = accelOrder;

  // override Smartphone mode with LCD mode
  if (accelLcd != accelLcdOld)
  {
    accelOrder = accelLcd;
    accelLcdOld = accelLcd;

    // notify bluetooth
    pCharacteristicAccel->setValue((uint8_t *)&accelOrder, 1);
    pCharacteristicAccel->notify();

    /*
    Serial.print("Accel ==> notify new accelOrder : ");
    Serial.println(accelOrder);
*/
  }

#if DEBUG_DISPLAY_ACCEL
  char print_buffer[500];
  sprintf(print_buffer, "%s %02x",
          "Accel Status : ",
          var);

  Serial.println(print_buffer);
#endif

  return var;
}

uint8_t getSpeed()
{
  uint8_t high1 = (data_speed_buffer[2] - data_speed_buffer[0]) & 0xff;
  uint8_t offset_regul = (data_speed_buffer[1] - data_speed_buffer[0]) & 0xff;
  uint8_t high2 = (high1 - offset_regul) & 0xff;
  uint8_t low = (data_speed_buffer[3] - data_speed_buffer[0]);

  //  int speed = (((int)high2 * 256) + (low)) / 20.5;

  int speed = (((int)high2 * 256) + (low));
  speed = speed * (settings.fields.Wheel_size / 10.0) / settings.fields.Motor_pole_number / 10.5;

  return speed;
}

uint8_t modifySpeedHigh(char var, char data_buffer[], int fakeSpeed)
{

  // modify speed
  // if (speedOld => fakeSpeed)
  // {
  //   // modify speed
  //   return ((0x01 + data_buffer[3]) & 0xff);
  // }
  // else
  // {
  return var;
  //}
}

uint8_t modifySpeedLow(char var, char data_buffer[], int fakeSpeed)
{

  // modify speed
  // if (speedOld => fakeSpeed)
  // {
  //   return ((0xf0 + data_buffer[3]) & 0xff);
  // }
  // else
  // {
  return var;
  //}
}

int readHardSerial(int i, HardwareSerial *ss, int serialMode, char data_buffer[])
{

  byte var;

  if (ss->available() > 0)
  {

    var = ss->read();

    // LCD -> CNTRL
    if (serialMode == MODE_LCD_TO_CNTRL)
    {
      if ((var == 0xAA) && (begin_LcdToCntrl == 1))
      {
        begin_LcdToCntrl = 0;
        Serial.println(PSTR(" ===> detect begin AA"));
        i = 0;
      }
    }
    else
    // CNTRL -> LCD
    {
      if ((var == 0x36) && (begin_CntrlToLcd == 1))
      {
        begin_CntrlToLcd = 0;
        Serial.println(PSTR(" ===> detect begin 36"));
        i = 0;
      }
    }

    //---------------------
    // MODIFY LCD_TO_CNTRL
    if ((i == 5) && (serialMode == MODE_LCD_TO_CNTRL))
    {

      var = modifyMode(var, data_buffer);
      isModified_LcdToCntrl = 1;
    }

    if ((i == 7) && (serialMode == MODE_LCD_TO_CNTRL))
    {
      var = modifyPower(var, data_buffer);
      isModified_LcdToCntrl = 1;
    }

    if ((i == 10) && (serialMode == MODE_LCD_TO_CNTRL))
    {
      var = modifyBrake(var, data_buffer);
      isModified_LcdToCntrl = 1;
    }

    if ((i == 11) && (serialMode == MODE_LCD_TO_CNTRL))
    {
      var = modifyEco(var, data_buffer);
      isModified_LcdToCntrl = 1;
    }

    if ((i == 12) && (serialMode == MODE_LCD_TO_CNTRL))
    {
      var = modifyAccel(var, data_buffer);
      isModified_LcdToCntrl = 1;
    }

    //---------------------
    // MODIFY CNTRL_TO_LCD

    if ((i == 4) && (serialMode == MODE_CNTRL_TO_LCD))
    {
      getBrakeFromLCD(var, data_buffer);
    }

    // modify speed
    if ((i == 7) && (serialMode == MODE_CNTRL_TO_LCD))
    {
      data_speed_buffer[0] = data_buffer[3];
      data_speed_buffer[1] = data_buffer[5];
      data_speed_buffer[2] = var;
      var = modifySpeedHigh(var, data_buffer, fakeSpeed);
      isModified_CntrlToLcd = 1;
    }
    if ((i == 8) && (serialMode == MODE_CNTRL_TO_LCD))
    {
      data_speed_buffer[3] = var;
      var = modifySpeedLow(var, data_buffer, fakeSpeed);
      isModified_CntrlToLcd = 1;

      speedOld = speedCurrent;
      speedCurrent = getSpeed();
    }

    // CHECKSUM
    if ((isModified_LcdToCntrl == 1) && (i == 14) && (serialMode == MODE_LCD_TO_CNTRL))
    {
      uint8_t oldChecksum = var;
      var = getCheckSum(data_buffer);

#if DEBUG_SERIAL_CHECKSUM_LCD_TO_CNTRL
      char print_buffer[500];
      sprintf(print_buffer, "%02x %02x",
              oldChecksum,
              var);

      Serial.print(" ===> modified checksum LCD_TO_CNTRL : ");
      Serial.println(print_buffer);
#endif

      isModified_LcdToCntrl = 0;
    }
    else if (((isModified_CntrlToLcd) == 1) && (i == 14) && (serialMode == MODE_CNTRL_TO_LCD))
    {

      uint8_t oldChecksum = var;
      var = getCheckSum(data_buffer);

#if DEBUG_SERIAL_CHECKSUM_CNTRL_TO_LCD
      char print_buffer[500];
      sprintf(print_buffer, "%02x %02x",
              oldChecksum,
              var);

      Serial.print(" ===> modified checksum CNTRL_TO_LCD : ");
      Serial.println(print_buffer);
#endif

      isModified_CntrlToLcd = 0;
    }

    data_buffer[i] = var;

    ss->write(var);

    // display
    if (i == 14)
    {

      uint8_t checksum = getCheckSum(data_buffer);

      if (serialMode == MODE_CNTRL_TO_LCD)
      {
#if DEBUG_DISPLAY_FRAME_CNTRL_TO_LCD
        displayFrame(mode, data_buffer, checksum);
#endif
#if DEBUG_DISPLAY_SPEED
        displaySpeed();
#endif
      }
      else
      {
#if DEBUG_DISPLAY_FRAME_LCD_TO_CNTRL
        displayFrame(mode, data_buffer, checksum);
#endif
#if DEBUG_DISPLAY_MODE
        displayMode(data_buffer);
#endif
      }

      if (checksum != data_buffer[14])
      {
        Serial.println("====> CHECKSUM error ==> reset");

        if (serialMode == MODE_LCD_TO_CNTRL)
          begin_LcdToCntrl = 1;
        else
          begin_CntrlToLcd = 1;
      }

      i = 0;
    }
    else
    {
      i++;
    }
  }

  return i;
}

void processSerial()
{
  // read/write LCD -> CNTRL
  //  i_LcdToCntrl = readSoftSerial(i_LcdToCntrl, &swSerLcdToCntrl, MODE_LCD_TO_CNTRL, data_buffer_lcd);
  i_LcdToCntrl = readHardSerial(i_LcdToCntrl, &hwSerLcdToCntrl, MODE_LCD_TO_CNTRL, data_buffer_lcd);

  //i_CntrlToLcd = readSoftSerial(i_CntrlToLcd, &swSerCntrlToLcd, MODE_CNTRL_TO_LCD, data_buffer_cntrl);
  i_CntrlToLcd = readHardSerial(i_CntrlToLcd, &hwSerCntrlToLcd, MODE_CNTRL_TO_LCD, data_buffer_cntrl);
}

void processBLE()
{
  // notify changed value
  if (deviceConnected)
  {
    if (millis() > timeLastNotifyBle + 500)
    {

      pCharacteristicSpeed->setValue((uint8_t *)&speedCurrent, 1);
      pCharacteristicSpeed->notify();

#if DEBUG_BLE_NOTIFY
      Serial.print("Notify speed : ");
      Serial.println(speedCurrent);
#endif

      // notify bluetooth
      uint32_t voltage = voltageFilter.getMean();
      pCharacteristicVoltageStatus->setValue((uint8_t *)&voltage, 4);
      pCharacteristicVoltageStatus->notify();

#if DEBUG_BLE_NOTIFY
      Serial.print("Notify voltage : ");
      Serial.println(voltage);
#endif

      int32_t current = currentFilter.getMean();
      pCharacteristicCurrentStatus->setValue((uint8_t *)&current, 4);
      pCharacteristicCurrentStatus->notify();

#if DEBUG_BLE_NOTIFY
      Serial.print("Notify current : ");
      Serial.println(current);
#endif

      int32_t power = (current / 1000.0) * (voltage / 1000.0);
      if (power < 0)
        power = 0;
      pCharacteristicPowerStatus->setValue((uint8_t *)&power, 4);
      pCharacteristicPowerStatus->notify();

#if DEBUG_BLE_NOTIFY
      Serial.print("Notify power : ");
      Serial.println(power);
#endif

      pCharacteristicBtlockStatus->setValue((uint8_t *)&bleLock, 1);
      pCharacteristicBtlockStatus->notify();

#if DEBUG_BLE_NOTIFY
      Serial.print("Notify bleLock : ");
      Serial.println(bleLock);
#endif

      timeLastNotifyBle = millis();
    }
  }
  // disconnecting
  if (!deviceConnected && oldDeviceConnected)
  {
    //delay(500);                  // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected)
  {
    // do stuff here on connecting
    oldDeviceConnected = deviceConnected;
  }
}

void processBrake()
{
  brakeStatus = digitalRead(PIN_IN_BRAKE);
}

void processButton1()
{
  button1Status = digitalRead(PIN_IN_BUTTON1);
}

void processButton2()
{
  button2Status = digitalRead(PIN_IN_BUTTON2);
}

void processDHT()
{
  static unsigned long measurement_timestamp = millis();

  /* Measure once every four seconds. */
  if (millis() - measurement_timestamp > 4000ul)
  {

    float temperature;
    float humidity;

    if (dht_sensor.measure(&temperature, &humidity) == true)
    {
      measurement_timestamp = millis();

#if DEBUG_DISPLAY_DHT
      Serial.print("T = ");
      Serial.print(temperature, 1);
      Serial.print(" deg. C, H = ");
      Serial.print(humidity, 1);
      Serial.println("%");
#endif

      currentTemperature = temperature;
      currentHumidity = humidity;

      uint32_t temp = temperature * 1000;
      pCharacteristicTemperatureStatus->setValue((uint8_t *)&temp, 4);
      pCharacteristicTemperatureStatus->notify();

      temp = humidity * 1000;
      pCharacteristicHumidityStatus->setValue((uint8_t *)&temp, 4);
      pCharacteristicHumidityStatus->notify();
    }
  }
}

void processVoltage()
{

  voltageStatus = analogRead(PIN_IN_VOLTAGE);

  voltageInMilliVolts = (voltageStatus * 1000) / ANALOG_TO_VOLTS;
  voltageFilter.in(voltageInMilliVolts);

  /*   Serial.print("Voltage read : ");
  Serial.print(voltageStatus);
  Serial.print(" / in volts : ");
  Serial.println(voltageInMilliVolts / 1000.0); */
}

void processCurrent()
{
  int curerntRead = analogRead(PIN_IN_CURRENT);
  int currentInMillamps = (curerntRead - currentFilterInit.getMean()) * 1000 / ANALOG_TO_CURRENT;

  // current rest value
  currentFilter.in(currentInMillamps);
  if ((speedCurrent == 0) && (currentCalibOrder == 1))
  {

    currentFilterInit.in(curerntRead);

    iCurrentCalibOrder++;
    if (iCurrentCalibOrder > NB_CURRENT_CALIB)
    {
      iCurrentCalibOrder = 0;
      currentCalibOrder = 0;
    }

#if DEBUG_DISPLAY_CURRENT
    Serial.print("Current calibration ... ");
#endif
  }

#if DEBUG_DISPLAY_CURRENT
  Serial.print("Current read : ");
  Serial.print(curerntRead);
  Serial.print(" / currentFilterInit mean : ");
  Serial.print(currentFilterInit.getMean());
  Serial.print(" / in amperes : ");
  Serial.println(currentInMillamps / 1000.0);
#endif
}

//////------------------------------------
//////------------------------------------
////// Main loop

void loop()
{

  processSerial();
  processBLE();

  processButton1();
#if DEBUG_DISPLAY_BUTTON1
  displayButton1();
#endif

  processButton2();
#if DEBUG_DISPLAY_BUTTON2
  displayButton2();
#endif

  if (i_loop % 100 == 1)
  {
    processVoltage();
    //processCurrent();

    //processBrake();
    //displayBrake();
  }

  if (i_loop % 1000 == 1)
  {
    /* Measure temperature and humidity.  If the functions returns
     true, then a measurement is available. */
    processDHT();
  }

  if (i_loop % 10 == 1)
  {
    processCurrent();
  }

/*
  // handle OTA
  BLEDevice::deinit();
  OTA_loop();
*/

  // Give a time for ESP
  delay(1);
  i_loop++;

  digitalWrite(14, i_loop % 2);

  timeLoop = millis();
}

/////////// End