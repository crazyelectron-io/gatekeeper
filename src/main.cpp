/*===================================================================================================*
 *DESCRIPTION:
 *  This rogram (running on an ESP32-based OLIMEX-board - ESP32-POE-ISO) interfaces with the remote
 *  for opening and closing thre gate of our driveway.
 *  It can be controlled through publishing messages to an MQTT topic (control/gate).
 *
 *VERSION HISTORY:
 *	v0.1	Initial test version for trying out the ethernet and MQTT connection logic (no gate control).
 *
 *NOTES:
 *  To use the PubSubClient library, add the line 'knolleary/PubSubClient@^2.8' to lib-deps in
 *  'platformio.ini'.
 *  See also https://github.com/espressif/arduino-esp32/blob/master/libraries/Ethernet/src/ETH.h for
 *  more info on the Ethernet library. It defaults to the LAN8720 type controller, which is compatible
 *  with the OLIMEX ESP32-POE-ISO board. We just need to define the GPIO used for the PHY clock.
 * 
 *COPYRIGHT:
 *	This program comes with ABSOLUTELY NO WARRANTY. Use at your own risk.
 *	This is free software, and you are welcome to redistribute it under certain conditions.
 *	The program and its source code are published under the GNU General Public License (GPL).
 *  See http://www.gnu.org/licenses/gpl-3.0.txt for details.
 *  Some parts are based on other open source code.
 *
 *@file main.cpp
 *@version 0.1
 *@date Sunday, July 30, 2023 08:44 UTC
 *==================================================================================================*/

/*==================================================================================================*\
|*                                 I N C L U D E   H E A D E R S                                    *|
\*==================================================================================================*/

#include <Arduino.h>                                            //Using the Arduino framework.
#include <ArduinoOTA.h>                                         //Include OTA update service.
#include <ETH.h>                                                //The Ethernet library definitions.
#include <PubSubClient.h>                                       //The MQTT client library definitions.
#include <WiFiUdp.h>
#include <ctype.h>

/*==================================================================================================*\
|*                               G L O B A L   C O N S T A N T S                                    *|
\*==================================================================================================*/

#define APP_VERSION "0.1"                                       //Software version

/*--- MQTT connection parameters ---*/
const PROGMEM char *MQTT_CLIENT_ID = "gatekeeper";              //MQTT Client ID
const PROGMEM char *MQTT_SERVER = "mosquitto.moerman.online";   //MQTT Broker (Mosquitto)
const PROGMEM unsigned int MQTT_PORT = 1883;                    //Port# on the MQTT Server
const PROGMEM char *MQTT_TOPIC = "control/gate";                //MQTT topic to subscribe to
#define MQTT_VERSION MQTT_VERSION_3_1_1                         //The MQTT version we use

/*--- Define OTA port ---*/
#define OTA_PORT 8266                                           //This is the default port for Arduino OTA library.

#define BAUDRATE 115200                                         //Serial port at 115,200 baud,

/*--- Network connection wait time (s) ---*/
#define CONNECT_WAIT 30

/*--- Debug/trace settings ---*/
#define MQTT_DEBUG 1                                            //Debug the MQTT handling

/*--- GPIO ports used for ethernet PHY ---*/
#define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT                       //Ethernet clock port for the ESP32-POE-ISO board
#define ETH_PHY_POWER 12                                        //PoE enable port (defaults to '-1').

/*--- GPIO Pin for gate control ---*/
#define GATE_GPIO 15

/*==================================================================================================*
 *                           G L O B A L   V A R I A B L E S                                        *
 *==================================================================================================*/

/*--- MQTT PubSub client handle/instance ---*/
PubSubClient hMqttClient;

/*--- Network connection handle/instance ---*/
WiFiClient hEspClient;

/*--- Ethernet connection active flag ---*/
static bool eth_connected = false;

/*==================================================================================================*
 *                                     F U N C T I O N S                                            *
 *==================================================================================================*/

/*------------------------------------------------------------------------------------------------*
 * WiFiEvent: Network callback function to handle connect and disconnect.
 *------------------------------------------------------------------------------------------------*
 *DESCRIPTION:
 *	
 *INPUT:
 *	
 *OUTPUT:
 *	None. Returns only after succesful connect.
 *NOTES:
 *	
 *------------------------------------------------------------------------------------------------*/
void NetworkEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
#ifdef ETH_DEBUG
      Serial.println("ETH started");
#endif
      ETH.setHostname(MQTT_CLIENT_ID);                          //Set the MQTT client ID as hostname
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
#ifdef ETH_DEBUG
      Serial.println("ETH connected");
#endif
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex())
        Serial.print(", FDX, ");
      else
        Serial.print(", HDX, ");
      Serial.print(ETH.linkSpeed());
      Serial.println(" Mbps");
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH stopped");
      eth_connected = false;
      break;
    default:
      Serial.print("ETH unknown event: ");
      Serial.println(event);
      break;
  }
}

/*------------------------------------------------------------------------------------------------*
 * SetupNetwork: Setup the network connection with the IoT network.
 *------------------------------------------------------------------------------------------------*
 *DESCRIPTION:
 *	Connect to the IoT network. In case of failure, retry for 60 seconds and if it still fails,
 *  reboot the system to try allover again.
 *INPUT:
 *	None.
 *OUTPUT:
 *	None. Returns only after succesful connect.
 *NOTES:
 *------------------------------------------------------------------------------------------------*/
void SetupNetwork()
{
  delay(100);                                                   //Let the SoC Ethernet stabilize

  /*--- Initialize the network connection ---*/
  Serial.print("Connecting network");                         //Tell the world we are connecting
  WiFi.onEvent(NetworkEvent);
  ETH.begin();

  /*--- Wait up to 30 seconds for the connection ---*/
  int nWait = CONNECT_WAIT;
  while (!eth_connected)
  {
    delay(1000);
    Serial.print(".");
    if (!--nWait)
    {
      /*--- Restart and retry if still not connected ---*/
      Serial.println(".");
      Serial.println("Connection Failed! Rebooting...");
      ESP.restart();
    }
  }
  Serial.println(".");
}

/*------------------------------------------------------------------------------------------------*
 * MqttCallback: MQTT callback function to receive subscribed topic messages.
 *------------------------------------------------------------------------------------------------*
 *DESCRIPTION:
 *	MQTT subscribe callback routine to receive the messages.
 *INPUT:
 *	
 *OUTPUT:
 *	None. Returns only after succesful connect.
 *NOTES:
 *------------------------------------------------------------------------------------------------*/
void MqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("New message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  switch(payload[0])
  {
    case '0':
#ifdef MQTT_DEBUG
      Serial.println("Received command 0 - PORT LO");
#endif
      digitalWrite(GATE_GPIO, LOW);
      break;
    case '1':
#ifdef MQTT_DEBUG
      Serial.println("Received command 1 - TOGGLE HI/LO");
#endif
      digitalWrite(GATE_GPIO, HIGH);
      delay(750);
      digitalWrite(GATE_GPIO, LOW);
      break;
    case '9':
#ifdef MQTT_DEBUG
      Serial.println("Received command 9 - RESET");
#endif
      ESP.restart();
      break;
    default:
      Serial.print("Received unknown command: ");
      Serial.println(payload[0]);
  }
}

/*------------------------------------------------------------------------------------------------*
 * ConnectMqtt: (Re)connect to the MQTT broker.
 *------------------------------------------------------------------------------------------------*
 *DESCRIPTION:
 *	Check if there is an active MQTT connection. If not try to re-establish a connection for 10
 *  seconds (retry 5 times, every other second), before quiting (to prevent stalling).
 *INPUT:
 *	None.
 *OUTPUT:
 *	(bool) true if a connection is established (within 10s), false if it failed.
 *------------------------------------------------------------------------------------------------*/
bool ConnectMqtt(void)
{
  if (!hMqttClient.connected())
  {
    Serial.print("Setup MQTT...");

    /*--- Loop until we're (re)connected for 10 seconds ---*/
    for (int nLoop = 0; nLoop < 5; ++nLoop)
    {
      /*--- Attempt to connect ---*/
      if (hMqttClient.connect(MQTT_CLIENT_ID))
      {
        Serial.print("connected as ");
        Serial.print(MQTT_CLIENT_ID);
        return true;
      }
      else
      {
#ifdef MQTT_DEBUG
        Serial.print("failed, rc=");
        Serial.print(hMqttClient.state());
        Serial.println("");
#else
        Serial.print(".");
#endif
        yield();
        delay(2000); //Wait 2s before retrying
      }
    }
    /*--- MQTT connection failed ---*/
#ifndef MQTT_DEBUG
    Serial.print("failed, rc=");
    Serial.print(hMqttClient.state());
    Serial.println("");
#endif
    return false;
  }
#ifdef MQTT_DEBUG
  Serial.println("MQTT connecting alive");
#endif

  return true; //We were connected
}

/*------------------------------------------------------------------------------------------------*
 * SetupMQTT: Setup the client connection with the MQTT Broker.
 *------------------------------------------------------------------------------------------------*
 *DESCRIPTION:
 *	Connect to the Mosquitto MQTT Broker server. In case of failure, retry for 120 seconds and if it
 *  still fails, reboot the system to try allover again.
 *INPUT:
 *	None.
 *OUTPUT:
 *	None. Returns only after succesful connect.
 *NOTES:
 *------------------------------------------------------------------------------------------------*/
void SetupMQTT()
{
  hMqttClient.setClient(hEspClient);                            //Initialize the MQTT client
  hMqttClient.setServer(MQTT_SERVER, MQTT_PORT);                //Define the MQTT broker
  (void)ConnectMqtt();
  hMqttClient.setCallback(MqttCallback);                        //Setup callback for subscribed topic
  hMqttClient.subscribe(MQTT_TOPIC);
#ifdef MQTT_DEBUG
  Serial.print("Subscribed to topic ");
  Serial.println(MQTT_TOPIC);
#endif
}

/*------------------------------------------------------------------------------------------------*
 * SetupOTA: Setup for OTA updates.
 *------------------------------------------------------------------------------------------------*
 *DESCRIPTION:
 *	Defines  callback routines for the Arduino Framework OTA library routines; only to show the
 *  status. All actual OTA processing is handled by the library.
 *INPUT:
 *	None.
 *OUTPUT:
 *	None.
 *------------------------------------------------------------------------------------------------*/
void SetupOTA(void)
{
  ArduinoOTA.setPort(OTA_PORT);                                 //Port defaults to 8266 /*TODO: check */
  ArduinoOTA.setHostname(MQTT_CLIENT_ID);                       //Defaults to esp32-[ChipID] /*TODO: Check */
  //ArduinoOTA.setPassword((const char *)"123456");             //No authentication by default

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}

/*------------------------------------------------------------------------------------------------*
 * setup: The standar Arduino Framework one-time initialization routine.
 *------------------------------------------------------------------------------------------------*
 *DESCRIPTION:
 *	Called from the Arduino Framework at boot. Initializes the serial console (for status and
 *  debugging purposes), the MQTT connection , the OTA services and the network connection.
 *INPUT:
 *	None.
 *OUTPUT:
 *	None. Returns only when the network and MQTT connections are succesful.
 *NOTES:
 *  During boot of the ESP32 there can be a few garbage characters on the serial debug output.
 *------------------------------------------------------------------------------------------------*/
void setup() {
  Serial.begin(BAUDRATE);                                       //Setup the serial console (USB)
  Serial.print("\r\n \r\nBooting GateKeeper, version ");        //Send welcome message to the console
  Serial.println(APP_VERSION);

  SetupNetwork();                                               //Initialize the ethernet connection

  SetupMQTT();                                                  //Initialize the MQTT client connection

  SetupOTA();                                                   //Setup OTA update service

  pinMode(GATE_GPIO, OUTPUT);

  Serial.println("\r\nREADY\r\n");                              //We're ready to receive commands
  delay(1500);
}

/*------------------------------------------------------------------------------------------------*
 * loop: The main program loop.
 *------------------------------------------------------------------------------------------------*
 *DESCRIPTION:
 *	Called from the Arduino Framework after the setup routine. It will be called in a loop until
 *  the device is reset.
 *INPUT:
 *	None.
 *OUTPUT:
 *	None.
 *NOTES:
 *  None.
 *------------------------------------------------------------------------------------------------*/
void loop() {
  if (!hMqttClient.loop()) {
    (void)ConnectMqtt();
  }
  /*--- Check for OTA updates ---*/
  ArduinoOTA.handle();
}
