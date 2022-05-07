#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <MQ135.h>
#include <time.h>
#include <SoftwareSerial.h>
#include <SdsDustSensor.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <Firebase.h>

#define machinePin D0 // chân ra quạt
#define DHTTYPE DHT21
#define DHTPIN D6
#define LightRelay D5

#define FIREBASE_HOST "https://smart-parking-tunnel-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define FIREBASE_API "AIzaSyB85wECEH5JMBSR5xRka8rWZZbGtUvh2H4"

WiFiClient espClient;
PubSubClient client(espClient);

MQ135 mq135_sensor = MQ135(A0); // chân vào analog cảm biến đo nồng độ khí
// SoftwareSerial s(D7, D8); //RX TX
SdsDustSensor sds(D4, D7);
DHT dht(DHTPIN, DHTTYPE);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

const char *ssid = "nhatrovn.vn CN31"; // tên wifi
const char *pass = "nhatrovn.vn"; // mật khẩu wifi

const char *mqtt_server = "broker.hivemq.com"; // server MQTT
const int mqtt_port = 1883;                    // port server MQTT

const String currentVersion = "2.0";                                  // version hiện tại của chương trình này
const char *serverUrl = "http://robot-kambria.000webhostapp.com/bin"; // chứa file update

int stateManual;      // bien chế độ - 1: manual và 0 : auto
int stateLight = 0;   // bien đèn 1 - ON và 0 - OFF
int stateMachine = 0; // bien quạt 1 - ON và 0 - OFF
int valuePPM = 800;
int valuePM25, valuePM10; // bien set ngưỡng bụi
int updateCheck = 0;      // biến trạng thái update OTA
float pmBui;

String c;        // biến mess của mqtt
String strTopic; // biến topic mqtt

unsigned long time_1 = 0; // khởi tạo time đếm giây
unsigned long time_2 = 0;

void callback(char *topic, byte *payload, unsigned int length);
void Wifi();
void MQTT();
void mode(int numMode);
void Manual();
void pubStateNow();
int getCO2();
void Auto();
void controlLight();
void updateFirmware();
void getInfoUpdate();
void getPM();

void setup()
{
    Serial.begin(115200);
    pinMode(machinePin, OUTPUT);
    //pinMode(D3, INPUT);  // chân vào của cảm biến phát hiện chuyển động
    pinMode(D5, OUTPUT); // chân ra đèn led
    //pinMode(D6, INPUT);  // chân vào DHT21
    sds.begin();
    sds.setActiveReportingMode();
    sds.setContinuousWorkingPeriod();
    dht.begin();
    delay(5000);
    Wifi();
    client.subscribe("CO2Measurement/mode");
    client.subscribe("CO2Measurement/machine");
    client.subscribe("CO2Measurement/stateNow");
    client.subscribe("CO2Measurement/setPPMAuto");
    client.subscribe("CO2Measurement/setLight");
    client.subscribe("CO2Measurement/setPM25Auto");
    client.subscribe("CO2Measurement/setPM10Auto");
    pubStateNow(); // gửi dữ liệu lúc vừa mở system

    // firebase
    /* config.api_key = FIREBASE_API;
    config.database_url = FIREBASE_HOST;
    if (Firebase.signUp(&config, &auth, "", ""))
    {
        Serial.println("Login to firebase OK");
    }
    else
    {
        Serial.println("Login to firebase Failed");
    }
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true); */
}
void loop()
{
    client.loop();
    if ((unsigned long)(millis() - time_1) > 5000)
    {
        char buffer[50];                               // tao bien buffer kieu mang ki tu de lam trung gian
        sprintf(buffer, "%d", getCO2());               // dua gia tri cua ham int getCO2 veo kieu chuoi buffer
        client.publish("CO2Measurement/Data", buffer); // pub data len mqtt broker
        //Serial.print(getCO2());
        //Serial.println();
        pubStateNow();
        Serial.print("Rzero of MQ135: ");
        Serial.println(mq135_sensor.getRZero());
        getPM();
        time_1 = millis();
    }
    // lấy giá trị bụi
    updateFirmware();
}

#pragma region Network

void Wifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    Serial.print("Connecting to Wifi");

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(800);
    }
    delay(800);
    Serial.print("Connected to Wifi");
    MQTT();
}

void MQTT()
{
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    while (!client.connected())
    {
        if (client.connect("ESP8266_CO2"))
        {
            Serial.print("Connected to MQTT Broker");
            // ScrollDisplayLCD(25);
            delay(500);
        }
        else
        {
            Serial.print("failed with state ");
            Serial.print(client.state());
            delay(2000);
        }
    }
}

#pragma endregion Network

void callback(char *topic, byte *payload, unsigned int length) // ham tra ve data mqtt
{
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);
    Serial.print("Message:");
    payload[length] = '\0';

    strTopic = String((char *)topic); // topic
    c = String((char *)payload);      // data trong topic
    Serial.print(c);
    Serial.println();
    Serial.print("--------------");
    if (strTopic == "CO2Measurement/mode") // topic
    {
        if (c == "manual")
        {
            mode(0);
        }
        else if (c == "auto")
        {
            mode(1);
        }
    }
    if (strTopic == "CO2Measurement/machine")
    {
        //đảo trạng thái biến stateMachine
        int *temp;
        temp = &stateMachine;
        if (c == "on")
        {
            *temp = 1;
        }
        if (c == "off")
        {
            *temp = 0;
        }
        Manual();
    }
    if (strTopic == "CO2Measurement/setLight")
    {
        //đảo trạng thái biến stateLight
        int *temp;
        temp = &stateLight;
        if (c == "1")
        {
            *temp = 1;
        }
        if (c == "0")
        {
            *temp = 0;
        }
        Manual();
    }
    // hàm gửi trạng thái hiện tại system lúc Desktop vừa mở
    if (strTopic == "CO2Measurement/stateNow")
    {
        if (c == "1")
        {
            pubStateNow();
        }
    }
    // hàm set giá trị cho hàm auto
    if (strTopic == "CO2Measurement/setPPMAuto")
    {
        valuePPM = atoi((char *)payload);
    }
    if (strTopic == "CO2Measurement/setPM25Auto")
    {
        valuePM25 = atoi((char *)payload);
    }
    if (strTopic == "CO2Measurement/setPM10Auto")
    {
        valuePM10 = atoi((char *)payload);
    }
}
// hàm gửi trạng thái hiện tại
void pubStateNow() // ham này khi mo len se gui du lieu len cloud
{
    client.publish("CO2Measurement/statusDevice", "1");
    if (getCO2() == 0)
    {
        client.publish("CO2Measurement/statusSensor", "0");
    }
    else
    {
        client.publish("CO2Measurement/statusSensor", "1");
    }
    if (pmBui <= 0)
    {
        client.publish("CO2Measurement/statusSensorBui", "0");
    }
    else
    {
        client.publish("CO2Measurement/statusSensorBui", "1");
    }
    if (stateMachine == 1)
    {
        client.publish("CO2Measurement/statusMachine", "1");
    }
    else
    {
        client.publish("CO2Measurement/statusMachine", "0");
    }
    if (stateManual == 1)
    {
        client.publish("CO2Measurement/statusMode", "1");
    }
    else
    {
        client.publish("CO2Measurement/statusMode", "0");
    }
    if (stateLight == 1)
    {
        client.publish("CO2Measurement/statusLight", "1");
    }
    else
    {
        client.publish("CO2Measurement/statusLight", "0");
    }
    Auto();
}

void mode(int numMode) // ham lua chon che do, 0: manual; 1: auto
{
    // manual
    if (numMode == 0)
    {
        Serial.print("Manual Mode");
        stateManual = 1;
        client.publish("CO2Measurement/statusMode", "1");
    }
    // auto
    else if (numMode == 1)
    {

        Serial.print("che do tu dong");
        client.publish("CO2Measurement/statusMode", "0");
        if (getCO2() >= valuePPM)
        {
            digitalWrite(machinePin, HIGH);
            client.publish("CO2Measurement/statusMachine", "1");
        }
        else
        {
            digitalWrite(machinePin, LOW);
            client.publish("CO2Measurement/statusMachine", "0");
        }
        stateManual = 0;
    }
    Serial.println();
    Serial.print(numMode);
}

void Manual() // che do manual
{
    if (stateManual == 1)
    {
        if (stateMachine == 1)
        {
            digitalWrite(machinePin, HIGH);
            client.publish("CO2Measurement/statusMachine", "1");
        }
        if (stateMachine == 0)
        {
            digitalWrite(machinePin, LOW);
            client.publish("CO2Measurement/statusMachine", "0");
        }
        if (stateLight == 1)
        {
            digitalWrite(LightRelay, HIGH);
            client.publish("CO2Measurement/statusLight", "1");
        }
        if (stateLight == 0)
        {
            digitalWrite(LightRelay, LOW);
            client.publish("CO2Measurement/statusLight", "0");
        }
    }
    if (stateManual == 0)
    {
        // neu khong bat manual ma chinh machine
    }
}

void Auto() // che do tu dong
{
    if (stateManual == 0)
    {
        
        controlLight();
        getPM();
        
    }
}

int getCO2() // ham tra ve gia tri CO2
{
    float ppm;
    // float t = 25.0;
    // float h = 60.0;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    // hàm kiem tra
    if (isnan(h) || isnan(t))
    {
        t = 25.0;
        h = 60.0;
        ppm = mq135_sensor.getPPM();
        client.publish("CO2Measurement/statusSensorDHT", "0");
    }
    else
    {
        ppm = mq135_sensor.getCorrectedPPM(t, h);
        client.publish("CO2Measurement/statusSensorDHT", "1");
    }
    Serial.print("Nhiet do: ");
    Serial.println(t);
    Serial.print("Do am: ");
    Serial.println(h);
    
    return (int)ppm;
}

// hàm phát hiện chuyển động và bật đèn
void controlLight()
{
    if (digitalRead(D3) == 1)
    {
        digitalWrite(LightRelay, HIGH);
        client.publish("CO2Measurement/statusLight", "1");
    }
    else
    {
        digitalWrite(LightRelay, LOW);
        client.publish("CO2Measurement/statusLight", "1");
    }
    Serial.print("move sensor: ");
    Serial.println(digitalRead(D3));
}

void updateFirmware()
{
    unsigned long time_2 = 0;
    if ((unsigned long)(millis() - time_2) > 30000)
    {
        getInfoUpdate();
        if (updateCheck == 1)
        {
            Firebase.RTDB.setInt(&fbdo, "Update", 0);
            delay(500);
            t_httpUpdate_return ret = ESPhttpUpdate.update(espClient, serverUrl, currentVersion);
            switch (ret)
            {
            case HTTP_UPDATE_FAILED:
                Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s", ESPhttpUpdate.getLastError(),
                              ESPhttpUpdate.getLastErrorString().c_str());
                ESP.restart();
                break;
            case HTTP_UPDATE_NO_UPDATES:
                Serial.println("HTTP_UPDATE_NO_UPDATES");
                ESP.restart();
                break;
            case HTTP_UPDATE_OK:
                Serial.println("HTTP_UPDATE_OK");
            default:
                break;
            }
        }
        time_2 = millis();
    }
}

void getInfoUpdate()
{
    if (Firebase.ready())
    {
        if (Firebase.RTDB.getInt(&fbdo, "/Update"))
        {
            if (fbdo.dataType() == "int")
            {
                if (fbdo.intData() == 1)
                {
                    updateCheck = fbdo.intData();
                }
                else
                {
                    updateCheck = 0;
                }
            }
        }
    }
}
void getPM() // ham lay gia tri bụi
{
    PmResult pm = sds.readPm();
    if (pm.isOk())
    {
        char pm25[50];
        char pm10[50];
        sprintf(pm25, "%.2f", pm.pm25);
        sprintf(pm10, "%.2f", pm.pm10);

        client.publish("CO2Measurement/PM2.5", pm25);
        client.publish("CO2Measurement/PM10", pm10);

        pmBui = pm.pm10;
        if (stateManual == 0)
        {
            if (pm.pm25 >= valuePM25 || pm.pm10 >= valuePM10 || getCO2()>= valuePPM)
            {
                digitalWrite(machinePin, HIGH);
                client.publish("CO2Measurement/statusMachine", "1");
            }
            else
            {
                digitalWrite(machinePin, LOW);
                client.publish("CO2Measurement/statusMachine", "0");
            }
        }
    }
}