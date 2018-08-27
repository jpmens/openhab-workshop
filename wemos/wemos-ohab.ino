/*
 * wemos-ohab.ino (C)2018 by Jan-Piet Mens <jp@mens.de>
 * A Wemos D1 mini which is to be used in an openHAB automation workshop.
 * The mini will have its blue built-in LED switched on or off via MQTT.
 * The device subscribes to openhab/xxxxxx/led with xxxxx being the lowercased
 * chipId so that we can have several Wemos D1 mini in a workshop. As soon
 * as the LED is toggled, the device publishes to ../led/state to
 * confirm the toggle.
 *
 * A button attached between GND and pin D3 will serve as a toggle which reports
 * back (publish) via MQTT to openHAB at the topic openhab/xxxxxx/switch
 *
 * Build using AsyncMqttClient and the Arduino IDE 1.8.5.
 *
 * Heavily inspired by:
 *    https://github.com/marvinroger/async-mqtt-client/blob/master/examples/Fully
 *    Featured-ESP8266/FullyFeatured-ESP8266.ino
 * 
 * JPM flashed 11 Wemos D1 mini for workshop on Sat Jun  9 17:34:22 CEST 2018 (openhab/chipid/#)
 */

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

#define WIFI_SSID	"SSID"
#define WIFI_PASSWORD	"WIFIPASSWORD"
#define MQTT_HOST	IPAddress(192, 168, 8, 1)
#define MQTT_PORT	1883

char subtopic[128];
char pubtopic[128];
char willtopic[128];
char lamptopic[128];

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

/*
 * Pushbuttons can generate spurious open/close transitions
 * when pressed, due to mechanical and physical issues.
 * This example demonstrates how to debounce an input,
 * which means checking twice in a short period of time to
 * make sure the pushbutton is definitely pressed.
 */

const int buttonPin = D3;

/*
 * The variable `lampState' indicates the desired state of
 * an openHAB switch (logical) and is not necessarily
 * associated with the Wemos' on-board LED.
 */

int lampState = LOW;
int buttonState = LOW;
int lastButtonState = LOW;   // the previous reading from the input pin
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

void connectToWifi()
{
	Serial.println("Connecting to Wi-Fi...");
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt()
{
	Serial.println("Connecting to MQTT...");

	mqttClient.setKeepAlive(60).setWill(willtopic, 0, true, "dead", strlen("dead") + 1);

	mqttClient.connect();

}

void onWifiConnect(const WiFiEventStationModeGotIP & event)
{
	Serial.println("Connected to Wi-Fi.");
	connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected & event)
{
	Serial.println("Disconnected from Wi-Fi.");
	mqttReconnectTimer.detach();
	//ensure we don 't reconnect to MQTT while reconnecting to Wi-Fi
	wifiReconnectTimer.once(2, connectToWifi);
}


void pubState()
{
	int realstate = !digitalRead(BUILTIN_LED);

	mqttClient.publish(pubtopic, 2, true, realstate ? "on" : "off");
}

void getmac(char *macstr)
{
	byte mac[6];

	WiFi.macAddress(mac);

	/* Use 6 characters of chip-id, like last 6 octets of clientId */
	sprintf(macstr, "%02x", mac[3]);
	sprintf(macstr + strlen(macstr), "%02x", mac[4]);
	sprintf(macstr + strlen(macstr), "%02x", mac[5]);
}

void onMqttConnect(bool sessionPresent)
{
	Serial.print("Connected to MQTT. Subscribing to ");
	Serial.println(subtopic);

	uint16_t packetIdSub = mqttClient.subscribe(subtopic, 2);

	pubState();
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
	Serial.println("Disconnected from MQTT.");

	if (WiFi.isConnected()) {
		mqttReconnectTimer.once(2, connectToMqtt);
	}
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
	int state;			// 1 or 0

	if (len < 1)
		return;

	/* we expect 1/0 in payload; truncate/kill anything else */

	state = (*payload == '1') ? LOW : HIGH;

	digitalWrite(BUILTIN_LED, state);
	pubState();
}

void setup()
{
	static char macstr[20];

	Serial.begin(115200);
	Serial.println();
	Serial.println();

	getmac(macstr);

	sprintf(subtopic, "openhab/%s/led", macstr);
	sprintf(lamptopic, "openhab/%s/switch", macstr);
	sprintf(pubtopic, "%s/state", subtopic);
	sprintf(willtopic, "%s/state", subtopic);

	wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
	wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

	mqttClient.onConnect(onMqttConnect);
	mqttClient.onDisconnect(onMqttDisconnect);
	mqttClient.onMessage(onMqttMessage);

	mqttClient.setServer(MQTT_HOST, MQTT_PORT);

	connectToWifi();

	/* The BUILTIN-LED has a 10K pull up resistor so it will be lit when LOW. */

	pinMode(BUILTIN_LED, OUTPUT);  		// initialize onboard LED as output
	digitalWrite(BUILTIN_LED, HIGH);	// switch LED off

	pinMode(buttonPin, INPUT);
}

void loop() {
	int reading = digitalRead(buttonPin);

	/* Was button just pressed? */
	if (reading != lastButtonState) {
		lastDebounceTime = millis(); // reset the debouncing timer
	}

	if ((millis() - lastDebounceTime) > debounceDelay) {
		/* The reading has been there longer than debounce delay
		 * so take it as the current state and toggle only if HIGH
		 */
		if (reading != buttonState) {
			buttonState = reading;

			if (buttonState == HIGH) {
				lampState = !lampState;
				mqttClient.publish(lamptopic, 2, true, lampState ? "ON" : "OFF");
			}
		}
	}

	lastButtonState = reading;
}
