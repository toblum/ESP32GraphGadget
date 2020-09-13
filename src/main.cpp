/**
 * Example: Update Server
 * Description:
 *   In this example we will provide a "firmware update" link in the
 *   config portal.
 *   (See ESP8266 ESP8266HTTPUpdateServer examples 
 *   to understand UpdateServer!)
 *   (ESP32: HTTPUpdateServer library is ported for ESP32 in this project.)
 *   (See previous examples for more details!)
 * 
 * Hardware setup for this example:
 *   - An LED is attached to LED_BUILTIN pin with setup On=LOW.
 *   - [Optional] A push button is attached to pin D2, the other leg of the
 *     button should be attached to GND.
 */

#include <IotWebConf.h>
#include <Arduino.h>
#include <ArduinoMSGraph.h>

#include <ArduinoJson.h>
#include "SPIFFS.h"


/**
 * ePaper display
 */
// #define SPI_MOSI 23
// #define SPI_MISO -1
// #define SPI_CLK 18

#define ELINK_SS 5
#define ELINK_BUSY 4
#define ELINK_RESET 16
#define ELINK_DC 17

#include <GFX.h>
#include <GxEPD2_BW.h> // including both doesn't hurt
#include <GxEPD2_3C.h> // including both doesn't hurt
#include <Fonts/FreeSans9pt7b.h>

#define MAX_DISPLAY_BUFFER_SIZE 800
#define MAX_HEIGHT(EPD) (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))
GxEPD2_BW<GxEPD2_213_B72, GxEPD2_213_B72::HEIGHT> display(GxEPD2_213_B72(/*CS=D8*/ ELINK_SS, /*DC=D3*/ ELINK_DC, /*RST=D4*/ ELINK_RESET, /*BUSY=D2*/ ELINK_BUSY));
char *statusMessage = strdup("Startup ...");



/**
 * iotWebConf
 */
// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "GraphGadget";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "configureme";

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "cfg_0"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN 39

DNSServer dnsServer;
WebServer server(80);
HTTPUpdateServer httpUpdater;
byte lastIotWebConfState;

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);

#define STRING_LEN 64
#define INTEGER_LEN 16
char paramClientIdValue[STRING_LEN];
char paramTenantValue[STRING_LEN];
char paramPollIntervalValue[INTEGER_LEN];
IotWebConfSeparator separator = IotWebConfSeparator();
IotWebConfParameter paramClientId = IotWebConfParameter("Client-ID (Generic ID: 3837bbf0-30fb-47ad-bce8-f460ba9880c3)", "clientId", paramClientIdValue, STRING_LEN, "text", "e.g. 3837bbf0-30fb-47ad-bce8-f460ba9880c3", "3837bbf0-30fb-47ad-bce8-f460ba9880c3");
IotWebConfParameter paramTenant = IotWebConfParameter("Tenant hostname / ID", "tenantId", paramTenantValue, STRING_LEN, "text", "e.g. contoso.onmicrosoft.com");
IotWebConfParameter paramPollInterval = IotWebConfParameter("Presence polling interval (sec) (default: 30)", "pollInterval", paramPollIntervalValue, INTEGER_LEN, "number", "10..300", "30", "min='10' max='300' step='5'");



/**
 * ArduinoMSGraph
 */
String access_token = "";
String refresh_token = "";
String id_token = "";
unsigned int expires = 0;
const char *deviceCode = "";

ArduinoMSGraph graphClient(paramTenantValue, paramClientIdValue);




/**
 * General
 */
#define DEFAULT_ERROR_RETRY_INTERVAL 15
#define AUTH_POLLING_INTERVAL 10
enum states {
	SMODEINITIAL,
	SMODEWIFICONNECTING,
	SMODEWIFICONNECTED,
	SMODEAPMODE,
	SMODEAPPSETUP,
	SMODEDEVICELOGINSTARTED,
	SMODEDEVICELOGINFAILED,
	SMODEAUTHREADY,
	SMODEREFRESHTOKEN,
	SMODEERROR
};
uint8_t currentState = SMODEINITIAL;
uint8_t lastState = SMODEINITIAL;
static unsigned long tsPolling = 0;



#include "epaper_display.h"
// #include "helper.h"



/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
	// -- Let IotWebConf test and handle captive portal requests.
	if (iotWebConf.handleCaptivePortal())
	{
		// -- Captive portal request were already served.
		return;
	}
	String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
	s += "<title>IotWebConf 04 Update Server</title></head><body>Hello world!";
	s += "Go to <a href='config'>configure page</a> to change values.";
	s += "</body></html>\n";

	server.send(200, "text/html", s);
}



void wifiConnected()
{
	Serial.println("### WiFi was connected.");
	currentState = SMODEWIFICONNECTED;
}

void configSaved()
{
	Serial.println("### Configuration was updated.");
	currentState = SMODEWIFICONNECTED;
}

void statemachine() {
	byte iotWebConfState = iotWebConf.getState();
	if (iotWebConfState != lastIotWebConfState) {
		if (iotWebConfState == IOTWEBCONF_STATE_NOT_CONFIGURED || iotWebConfState == IOTWEBCONF_STATE_AP_MODE) {
			DBG_PRINTLN(F("Detected AP mode"));
			currentState = SMODEAPMODE;
		}
		if (iotWebConfState == IOTWEBCONF_STATE_CONNECTING) {
			DBG_PRINTLN(F("WiFi connecting"));
			currentState = SMODEWIFICONNECTING;
		}
	}
	lastIotWebConfState = iotWebConfState;

	// Statemachine: AP mode active
	if (currentState == SMODEAPMODE && lastState != SMODEAPMODE) {
		sprintf(statusMessage, "=== WiFi setup ===\nConnect to access point:\n\"%s\"\nUse passkey: \"%s\"\nOpen URL: http://192.168.4.1", thingName, wifiInitialApPassword);
		displayMessage(statusMessage);
	}

	// Statemachine: After wifi is connected
	if (currentState == SMODEWIFICONNECTED && lastState != SMODEWIFICONNECTED) {
		DBG_PRINTLN(F("Wifi connected, waiting for requests ..."));

		// Init ArduinoMSGraph context
		bool got_context = graphClient.readContextFromSPIFFS();
		if (got_context) {
			currentState = SMODEREFRESHTOKEN;
		} else {
			if (strlen(paramClientIdValue) > 0 && strlen(paramTenantValue) > 0) {

				DynamicJsonDocument authDoc(10000);
				// Start device login flow
				graphClient.startDeviceLoginFlow(authDoc, "offline_access%20openid%20Presence.Read%20Calendars.Read");

				// Consume result
				deviceCode = strdup(authDoc["device_code"].as<const char*>());
				const char *user_code = authDoc["user_code"].as<const char*>();
				const char *verification_uri = authDoc["verification_uri"].as<const char*>();
				const char *message = authDoc["message"].as<const char*>();

				sprintf(statusMessage, "=== Device login flow ===\nTo authenticate go to:\n%s, enter code:\n%s and login.", verification_uri, user_code);
				displayMessage(statusMessage);
				DBG_PRINTLN(message);
				DBG_PRINTLN(deviceCode);

				currentState = SMODEDEVICELOGINSTARTED;
			} else {
				sprintf(statusMessage, "=== AzureAD app setup ===\nPlease open config page at:\nhttp#://xxx.xxx.xxx.x/config\nand enter app data.");
				displayMessage(statusMessage);
				currentState = SMODEAPPSETUP;
			}
		}
	}

	// Statemachine: Refresh token
	if (currentState == SMODEREFRESHTOKEN) {
		if (lastState != SMODEREFRESHTOKEN) {
			statusMessage = strdup("=== Refresh token ===");
			displayMessage(statusMessage);
		}

		if (millis() >= tsPolling) {
			bool res = graphClient.refreshToken();
			if (res) {
				graphClient.saveContextToSPIFFS();
				currentState = SMODEAUTHREADY;
			} else {
				tsPolling = millis() + (DEFAULT_ERROR_RETRY_INTERVAL * 1000);
			}
		}
	}

	// Statemachine: Device login flow running
	if (currentState == SMODEDEVICELOGINSTARTED) {
		if (millis() >= tsPolling) {
			DBG_PRINT("deviceCode: ");
			DBG_PRINTLN(deviceCode);
			DynamicJsonDocument authDoc(10000);
			bool res = graphClient.pollForToken(authDoc, deviceCode);

			if (res) {
				DBG_PRINTLN("GOT ACCESS TOKEN! Yay!");
				DBG_PRINTLN(authDoc["access_token"].as<String>());

				graphClient.saveContextToSPIFFS();
				currentState = SMODEAUTHREADY;
			}

			tsPolling = millis() + (AUTH_POLLING_INTERVAL * 1000);
		}
	}

	// Statemachine: After wifi is connected
	if (currentState == SMODEAUTHREADY && lastState != SMODEAUTHREADY) {
		sprintf(statusMessage, "=== Auth ready ===\n%s",  access_token.c_str());
		displayMessage(statusMessage);
	}

	// Update laststate
	if (currentState != lastState) {
		lastState = currentState;
		DBG_PRINTLN(F("======================================================================"));
	}
}


void setup()
{
	Serial.begin(115200);
	Serial.println();
	Serial.println("Starting up...");

	iotWebConf.setConfigPin(CONFIG_PIN);
	iotWebConf.setupUpdateServer(&httpUpdater);
	iotWebConf.skipApStartup();
	iotWebConf.setConfigSavedCallback(&configSaved);
	iotWebConf.setWifiConnectionCallback(&wifiConnected);
	iotWebConf.setWifiConnectionTimeoutMs(10000);

	iotWebConf.addParameter(&separator);
	iotWebConf.addParameter(&paramClientId);
	iotWebConf.addParameter(&paramTenant);
	iotWebConf.addParameter(&paramPollInterval);

	// -- Initializing the configuration.
	iotWebConf.init();

	// -- Set up required URL handlers on the web server.
	server.on("/", handleRoot);
	server.on("/config", [] { iotWebConf.handleConfig(); });
	server.onNotFound([]() { iotWebConf.handleNotFound(); });

	// Init SPIFFS
	if(!SPIFFS.begin(true)){
		Serial.println("An Error has occurred while mounting SPIFFS");
		return;
	}

	// Init display
	displayInit();
	displayMessage(statusMessage);

	Serial.println("Ready.");
}

void loop()
{
	// -- doLoop should be called as frequently as possible.
	iotWebConf.doLoop();

	statemachine();
}