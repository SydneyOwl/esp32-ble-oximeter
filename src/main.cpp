#include "cal_BPM_SpO2.h"
#include <SPI.h>
#include "epd1in54.h"
#include "epdpaint.h"
#include "imagedata.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <ArduinoJson.h>

#define COLORED 0
#define UNCOLORED 1

#define STABLE_SP02 0.35
#define STABLE_BPM 3
#define LOW_SPO2 90
#define LOW_BPM 60
#define HIGH_BPM 100

#define SERVICE_UUID "cdfa000d-a7b4-4aab-aabe-4e81a362188a"
#define CHARACTERISTIC_UUID "b7feb784-2f96-4a1e-ad52-f1804c58ad18"

MAX30105 particleSensor;

extern double eSpO2;
extern double Ebpm;
extern float ir_forWeb;
extern float red_forWeb;
extern uint32_t ir, red;
extern uint32_t ESP_getFlashChipId();

extern bool max30102_fail;
bool bluetooth_connected;

void updateDisplay_task(void *pvParameters);

unsigned char image[1024];
Epd epd;
Paint paint(image, 0, 0); // width should be the multiple of 8

class MyCallbacks : public BLECharacteristicCallbacks
{

    void onWrite(BLECharacteristic *pCharacteristic)
    { // 写方法
        DynamicJsonDocument doc(1024);
        char raw_JSON[1024];
        std::string value = pCharacteristic->getValue(); // 接收值
        if (value.length() > 0)
        {
            Serial.println("*********");
            for (int i = 0; i < value.length(); i++) // 遍历输出字符串
                Serial.print(value[i]);
            Serial.println();
            Serial.println("*********");
            DeserializationError error = deserializeJson(doc, value.c_str());
            if (error)
            {
                log_e("反序列化配置文件失败", error.f_str());
                doc.clear();
                doc["status"] = 300;
                doc["msg"] = "Failed to parse your request!";
                serializeJson(doc, raw_JSON);
                pCharacteristic->setValue(raw_JSON);
                pCharacteristic->notify();
                return;
            }
            // Normal Status
            std::string order = doc["order"];
            if (order == "getData")
            {
                doc.clear();
                doc["status"] = 200;
                doc["millis"] = millis();
                doc["BPM"] = Ebpm;
                doc["SpO2"] = eSpO2;
                doc["ir_forGraph"] = ir_forWeb;
                doc["red_forGraph"] = red_forWeb;
                doc["ir"] = ir;
                doc["red"] = red;
                serializeJson(doc, raw_JSON);
                pCharacteristic->setValue(raw_JSON);
                pCharacteristic->notify();
                return;
            }
            if (order == "getDeviceInfo")
            {
                char compilationDate[50];
                sprintf(compilationDate, "%s %s", __DATE__, __TIME__);
                doc.clear();
                doc["millis"] = millis();
                doc["compilationDate"] = compilationDate;

                doc["chipModel"] = ESP.getChipModel();
                doc["chipRevision"] = ESP.getChipRevision();
                doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
                doc["chipCores"] = ESP.getChipCores();

                doc["heapSizeKiB"] = ESP.getHeapSize() / 1024;
                doc["freeHeapKiB"] = ESP.getFreeHeap() / 1024;

                doc["psramSizeKiB"] = ESP.getPsramSize() / 1024;
                doc["freePsramKiB"] = ESP.getFreePsram() / 1024;

                doc["flashChipId"] = ESP_getFlashChipId();
                doc["flashSpeedMHz"] = ESP.getFlashChipSpeed() / 1000000;
                doc["flashSizeMib"] = ESP.getFlashChipSize() / 1024 / 1024;

                doc["sketchMD5"] = ESP.getSketchMD5();
                doc["sdkVersion"] = ESP.getSdkVersion();
                serializeJson(doc, raw_JSON);
                pCharacteristic->setValue(raw_JSON);
                pCharacteristic->notify();
                return;
            }
        }
    }
};

class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer)
    {
        Serial.print("connected");
        bluetooth_connected = true;
    }

    void onDisconnect(BLEServer *pServer)
    {
        Serial.print("disconnected");
        bluetooth_connected = false;
        abort();
    }
};

void setup()
{
    // 初始化串口
    Serial.begin(115200);
    if (epd.Init(lut_full_update) != 0)
    {
        Serial.print("e-Paper init failed");
        return;
    }
    epd.ClearFrameMemory(0xFF); // bit set = white, bit reset = black
    epd.DisplayFrame();
    epd.ClearFrameMemory(0xFF); // bit set = white, bit reset = black
    epd.DisplayFrame();
    SPI.endTransaction();
    delay(1000);
    if (epd.Init(lut_partial_update) != 0)
    {
        Serial.print("e-Paper init failed");
        return;
    }
    paint.SetWidth(200);
    paint.SetHeight(20);
    paint.Clear(UNCOLORED);
    for (int i = 0; i < 10; i++)
    {
        epd.SetFrameMemory(paint.GetImage(), 0, 20 * i, paint.GetWidth(), paint.GetHeight());
    }
    epd.DisplayFrame();

    // 开启 心率&血压 监测任务
    if (xTaskCreate(
            cal_BPM_SpO2_task,
            "cal_BPM_SpO2",
            4096, /* Stack depth - small microcontrollers will use much
            less stack than this. */
            NULL, /* This example does not use the task parameter. */
            1,    /* This task will run at priority 1. */
            NULL) /* This example does not use the task handle. */
        != pdPASS)
    {
        log_e("Couldn't create cal_BPM_SpO2 task\n");
    }
    // 开启 心率&血压 监测任务
    if (xTaskCreate(
            updateDisplay_task,
            "updateDisplay_task",
            4096, /* Stack depth - small microcontrollers will use much
            less stack than this. */
            NULL, /* This example does not use the task parameter. */
            1,    /* This task will run at priority 1. */
            NULL) /* This example does not use the task handle. */
        != pdPASS)
    {
        log_e("Couldn't create UpdateDisplay task\n");
    }

    BLEDevice::init("OWL血氧仪");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    BLECharacteristic *pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_NOTIFY |
            BLECharacteristic::PROPERTY_WRITE);
    BLEDescriptor *desc = new BLEDescriptor("00002902-0000-1000-8000-00805F9B34FB");
    pCharacteristic->addDescriptor(desc);
    pCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();
    BLEAdvertising *pAdvertising = pServer->getAdvertising();
    pAdvertising->start();
}

void loop()
{
}

void updateDisplay_task(void *pvParameters)
{
    if (max30102_fail)
    {
        paint.SetWidth(200);
        paint.SetHeight(20);
        paint.Clear(COLORED);
        paint.DrawStringAt(20, 2, "MAX30102 ERROR", &Font16, UNCOLORED);
        epd.SetFrameMemory(paint.GetImage(), 0, 0, paint.GetWidth(), paint.GetHeight());
        epd.DisplayFrame();
        return;
    }
    double last_spo2 = 0;
    double last_ebpm = 0;
    while (1)
    {
        delay(500);
        if (bluetooth_connected)
        {
            paint.SetWidth(200);
            paint.SetHeight(40);
            paint.Clear(UNCOLORED);
            paint.DrawStringAt(2, 20, "SpO2: ->BLE", &Font24, COLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 50, paint.GetWidth(), paint.GetHeight());
            paint.Clear(UNCOLORED);
            paint.DrawStringAt(2, 20, "BPM: ->BLE", &Font24, COLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 100, paint.GetWidth(), paint.GetHeight());
            epd.DisplayFrame();
            while (1)
            {
                delay(100);
            }
            
        }
        if (abs(eSpO2 - MINIMUM_SPO2) < 0.1)
        {
            paint.SetWidth(200);
            paint.SetHeight(20);
            paint.Clear(COLORED);
            paint.DrawStringAt(20, 2, "PLACE UR FINGER", &Font16, UNCOLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 0, paint.GetWidth(), paint.GetHeight());
            paint.SetWidth(50);
            paint.SetHeight(20);
            paint.Clear(UNCOLORED);
            epd.SetFrameMemory(paint.GetImage(), 75, 30, paint.GetWidth(), paint.GetHeight());
            last_ebpm = 0;
            last_spo2 = 0;
            // clear instruction
            paint.SetWidth(200);
            paint.SetHeight(20);
            paint.Clear(UNCOLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 30, paint.GetWidth(), paint.GetHeight());
        }
        else
        {
            paint.SetWidth(200);
            paint.SetHeight(20);
            paint.Clear(UNCOLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 0, paint.GetWidth(), paint.GetHeight());
            if (abs(eSpO2 - MINIMUM_SPO2) > 0.1 && (abs(last_spo2 - eSpO2) > STABLE_SP02 || abs(last_ebpm - Ebpm) > STABLE_BPM))
            {
                Serial.print("111");
                paint.SetWidth(50);
                paint.SetHeight(20);
                paint.Clear(COLORED);
                paint.DrawStringAt(5, 2, "WAIT", &Font16, UNCOLORED);
                epd.SetFrameMemory(paint.GetImage(), 5, 30, paint.GetWidth(), paint.GetHeight());
            }
            else
            {
                Serial.print("222");
                paint.SetWidth(50);
                paint.SetHeight(20);
                paint.Clear(UNCOLORED);
                epd.SetFrameMemory(paint.GetImage(), 5, 30, paint.GetWidth(), paint.GetHeight());
            }
            last_spo2 = eSpO2;
            last_ebpm = Ebpm;
        }

        // if (bluetooth_connected)
        // {
        //     paint.SetWidth(200);
        //     paint.SetHeight(20);
        //     paint.Clear(COLORED);
        //     paint.DrawStringAt(30, 2, "BLE CONNECTED", &Font16, UNCOLORED);
        //     epd.SetFrameMemory(paint.GetImage(), 0, 180, paint.GetWidth(), paint.GetHeight());
        // }
        // else
        // {
        //     paint.SetWidth(200);
        //     paint.SetHeight(20);
        //     paint.Clear(UNCOLORED);
        //     epd.SetFrameMemory(paint.GetImage(), 0, 180, paint.GetWidth(), paint.GetHeight());
        // }

        paint.SetWidth(200);
        paint.SetHeight(40);
        paint.Clear(UNCOLORED);
        if (abs(eSpO2 - MINIMUM_SPO2) < 0.1)
        {
            paint.DrawStringAt(0, 20, "SpO2: ---", &Font24, COLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 50, paint.GetWidth(), paint.GetHeight());
        }
        else
        {
            std::string tmp = std::to_string(eSpO2);
            std::string result = tmp.substr(0, tmp.find(".") + 2);

            paint.DrawStringAt(2, 20, ("SpO2: " + result + "%").c_str(), &Font24, COLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 50, paint.GetWidth(), paint.GetHeight());
            // low
            if (eSpO2 < LOW_SPO2)
            {
                paint.SetWidth(60);
                paint.SetHeight(20);
                paint.Clear(COLORED);
                paint.DrawStringAt(5, 2, "LSP02", &Font16, UNCOLORED);
                epd.SetFrameMemory(paint.GetImage(), 65, 30, paint.GetWidth(), paint.GetHeight());
            }
            else
            {
                paint.SetWidth(60);
                paint.SetHeight(20);
                paint.Clear(UNCOLORED);
                epd.SetFrameMemory(paint.GetImage(), 65, 30, paint.GetWidth(), paint.GetHeight());
            }
        }

        paint.SetWidth(200);
        paint.SetHeight(40);
        paint.Clear(UNCOLORED);
        if (abs(eSpO2 - MINIMUM_SPO2) < 0.1)
        {
            paint.DrawStringAt(0, 20, "BPM: ---", &Font24, COLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 100, paint.GetWidth(), paint.GetHeight());
        }
        else
        {
            std::string tmp = std::to_string(Ebpm);
            std::string result = tmp.substr(0, tmp.find("."));
            paint.DrawStringAt(2, 20, ("BPM: " + result).c_str(), &Font24, COLORED);
            epd.SetFrameMemory(paint.GetImage(), 0, 100, paint.GetWidth(), paint.GetHeight());
            // Ebpm = 10;
            // low
            if (Ebpm < LOW_BPM)
            {
                paint.SetWidth(60);
                paint.SetHeight(20);
                paint.Clear(COLORED);
                paint.DrawStringAt(5, 2, "L-BPM", &Font16, UNCOLORED);
                epd.SetFrameMemory(paint.GetImage(), 140, 30, paint.GetWidth(), paint.GetHeight());
            }
            else if ((Ebpm > HIGH_BPM))
            {
                paint.SetWidth(60);
                paint.SetHeight(20);
                paint.Clear(COLORED);
                paint.DrawStringAt(5, 2, "H-BPM", &Font16, UNCOLORED);
                epd.SetFrameMemory(paint.GetImage(), 140, 30, paint.GetWidth(), paint.GetHeight());
            }
            else
            {
                paint.SetWidth(60);
                paint.SetHeight(20);
                paint.Clear(UNCOLORED);
                epd.SetFrameMemory(paint.GetImage(), 140, 30, paint.GetWidth(), paint.GetHeight());
            }
        }
        epd.DisplayFrame();
    }
}
