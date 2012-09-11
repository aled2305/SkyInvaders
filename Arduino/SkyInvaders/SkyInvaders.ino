/*
 * SkyInvaders firmware, Copyright (C) 2012 michael vogt <michu@neophob.com>
 *
 * This file is part of SkyInvaders.
 *
 * ExpeditInvaders is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * ExpeditInvaders is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 */

// FEATURES

//WOL needs 1164 bytes 
//#define USE_WOL 1

//SD card needs 7778 bytes and is unfinished!
//#define USE_SD

//use DHCP server OR static IP, dhcp needs 3148 bytes
//#define USE_DHCP 1

//use serial debug or not, needs 3836 bytes
#define USE_SERIAL_DEBUG 1

//#define USE_OSC 1

#include <avr/pgmspace.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Dns.h>

#ifdef USE_SD
#include <SD.h>
#endif

#ifdef USE_OSC
//make sure you enabled strings (#define _USE_STRING_) in OSCcommon.h
#include <ArdOSC.h>
#endif

//used as we need to send out raw socket stuff
#ifdef USE_WOL
#include <EthernetUdp.h>
#include <utility/w5100.h>
#include <utility/socket.h>
#endif

//default Pixels
#define NR_OF_PIXELS 96
//160

// maximal sleep time for the color animation
#define MAX_SLEEP_TIME 250.0f

//SkyInvaders knows 3 Animation Mode (Static Color, Color Fade, Serverimage)
#define MAX_NR_OF_MODES 3

// define strip hardware, use only ONE hardware type
#define USE_WS2801 1
//#define USE_LPD8806 1

#ifdef USE_WS2801
  #include <WS2801.h>
#endif
#ifdef USE_LPD8806
  #include <LPD8806.h>
#endif  

/**************
 * STRIP
 **************/
//output pixels data/clock3/2
#define dataPin 7
#define clockPin 6

//dummy init the pixel lib
#ifdef USE_WS2801
WS2801 strip = WS2801(); 
#endif
#ifdef USE_LPD8806
LPD8806 strip = LPD8806(); 
#endif  

/**************
 * NETWORK
 **************/
#ifndef USE_DHCP
byte myIp[]  = { 
  192, 168, 111, 177 };
#endif

byte myMac[] = { 
  0xde, 0xad, 0xbe, 0xef, 0xbe, 0x01 };

EthernetUDP Udp;

byte broadcastAddress[] = { 255, 255, 255, 255 }; 
#define REMOTE_UDP_DEBUG_PORT 8080

#define ARDUINO_LISTENING_ENCRYPTION_PORT 7999
EthernetServer server(ARDUINO_LISTENING_ENCRYPTION_PORT);

/**************
 * OSC
 **************/
#ifdef USE_OSC

const int serverPort  = 10000;
OSCServer oscServer;

byte oscServerIp[]  = { 
  0,0,0,0 };

IPAddress serverIp;
OSCClient client;
OSCMessage globalMes;

#endif 

//TODO changeme
#define OSC_SERVER "192.168.111.21" 

//change display mode, 3 modes: static color, color fade, serverimage
#define OSC_MSG_MODE "/mode" 

//change color of static color mode
#define OSC_MSG_STATIC_COL_R "/colr" 
#define OSC_MSG_STATIC_COL_G "/colg" 
#define OSC_MSG_STATIC_COL_B "/colb" 

#define OSC_MSG_COLFADE_COLORSET "/colorSet" 

//generic animation speed
#define OSC_MSG_GENERIC_SPEED "/speed" 

#define OSC_MSG_WOL "/wol" 

#define OSC_MSG_UPDATE_PIXEL "/pxl" 

/**************
 * SD CARD
 **************/
#ifdef USE_SD
File sdFileRead;
#endif

/**************
 * BUSINESS LOGIC
 **************/
const uint8_t MODE_STATIC_COLOR = 0;
const uint8_t MODE_COLOR_FADE = 1;
const uint8_t MODE_SERVER_IMAGE = 2;

//animation mode
uint8_t oscMode=MODE_STATIC_COLOR;

//which color set is selected?
uint8_t oscColorSetNr=0;

//static colorvalues
uint8_t oscR=255, oscG=255, oscB=255;
uint32_t staticColor=0xffffff;

//update strip after DELAY (in ms)
uint8_t oscDelay = 20;

//current delay value
uint8_t currentDelay = 0;

uint16_t frame = 0;
/**************
 * MISC
 **************/
const uint8_t ledPin = 9;

/**************
 * SETUP
 **************/
void setup(){ 

#ifdef USE_SERIAL_DEBUG
  Serial.begin(115200);  
#endif

// --init strip-------------------------------------

#ifdef USE_WS2801
  #ifdef USE_SERIAL_DEBUG
    Serial.println("WS2801");
  #endif
  strip = WS2801(NR_OF_PIXELS, dataPin, clockPin);   
#endif

#ifdef USE_LPD8806
  #ifdef USE_SERIAL_DEBUG
    Serial.println("INV8806");
  #endif
  strip = LPD8806(NR_OF_PIXELS, dataPin, clockPin); 
#endif

  #ifdef USE_SERIAL_DEBUG
    Serial.print("# Pixel: ");
    Serial.println(NR_OF_PIXELS, DEC);
  #endif


// --DHCP-------------------------------------
#ifdef USE_DHCP

  #ifdef USE_SERIAL_DEBUG
    Serial.println("Use DHCP");
  #endif

  //start Ethernet library using dhcp
  if (Ethernet.begin(myMac) == 0) {
    #ifdef USE_SERIAL_DEBUG
      Serial.println("No DHCP Server found");
    #endif
    // no point in carrying on, so do nothing forevermore:
    for(;;);
  }
#else
// --manual ip-------------------------------------

  #ifdef USE_SERIAL_DEBUG
    Serial.println("Use Manual IP");
  #endif

  //Manual IP
  Ethernet.begin(myMac, myIp);
#endif 


#ifdef USE_SERIAL_DEBUG
  Serial.print("IP: ");
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    // print the value of each byte of the IP address:
    Serial.print(Ethernet.localIP()[thisByte], DEC);
    Serial.print(".");
  }
  Serial.println();
#endif

// --dns-------------------------------------
#ifdef USE_OSC
  //get ip from dns name, used to find osc server
  DNSClient dns;
  
  dns.begin(Ethernet.dnsServerIP());
  int ret = dns.getHostByName(OSC_SERVER, serverIp);
  if (ret == 1) {
    
#ifdef USE_SERIAL_DEBUG
    Serial.print("DNS IP:");
    for (byte thisByte = 0; thisByte < 4; thisByte++) {
      // print the value of each byte of the IP address:
      Serial.print(serverIp[thisByte], DEC);

      //get ip 
      oscServerIp[thisByte] = serverIp[thisByte];
      Serial.print("."); 
    }
    Serial.println();
 #endif 
  //dns osc client done

    
  } else {
#ifdef USE_SERIAL_DEBUG    
    Serial.print("Failed to resolve hostname: ");
    Serial.println(ret, DEC);
#endif    
  }
#endif


// --led strip-------------------------------------
#ifdef USE_SERIAL_DEBUG    
    Serial.println("Start LED Strip");
#endif
  //start strips 
  strip.begin();
  strip.show();


// --start udp server-------------------------------------

#ifdef USE_WOL
    #ifdef USE_SERIAL_DEBUG    
        Serial.println("Start UDP Server");
    #endif
    
    //init UDP, used for WOL
    Udp.begin(7);
#endif

// --start osc server-------------------------------------
#ifdef USE_OSC

#ifdef USE_SERIAL_DEBUG    
    Serial.println("Start OSC Server");
#endif

  //start osc server
  oscServer.begin(serverPort);
  //register OSC callback function
  oscServer.addCallback(OSC_MSG_STATIC_COL_R, &oscCallbackR); //PARAMETER: 1, float value 0..1
  oscServer.addCallback(OSC_MSG_STATIC_COL_G, &oscCallbackG); //PARAMETER: 1, float value 0..1
  oscServer.addCallback(OSC_MSG_STATIC_COL_B, &oscCallbackB); //PARAMETER: 1, float value 0..1
  oscServer.addCallback(OSC_MSG_MODE, &oscCallbackChangeMode); //PARAMETER: 1, float value 0..1
  oscServer.addCallback(OSC_MSG_COLFADE_COLORSET, &oscCallbackColorSet); //PARAMETER: 1, float value 0..1  
  oscServer.addCallback(OSC_MSG_GENERIC_SPEED, &oscCallbackSpeed); //PARAMETER: 1, float value 0..1  
#ifdef USE_WOL  
  oscServer.addCallback(OSC_MSG_WOL, &oscCallbackWol); //PARAMETER: 1, float value 0..1  
#endif
  oscServer.addCallback(OSC_MSG_UPDATE_PIXEL, &oscCallbackPixel); //PARAMETER: 2, int offset, 4xlong
#endif
// --start animation mode-------------------------------------

  //just to be sure!
  loadColorSet(0);
  //init animation mode
  initAnimationMode();

#ifdef USE_SERIAL_DEBUG
  Serial.print("Init TCP Server on Port ");
  Serial.println(ARDUINO_LISTENING_ENCRYPTION_PORT, DEC);
  
//  server.start
#endif

// --sd-------------------------------------

#ifdef USE_SD
  initSdCardInformation();
#endif

  //let the onboard arduino led blink
  pinMode(ledPin, OUTPUT);  
  synchronousBlink();

#ifdef USE_SERIAL_DEBUG
  Serial.print("Free Mem: ");
  Serial.println(freeRam(), DEC);
#endif  
}




/**************
 * LOOP
 **************/
void loop(){  
#ifdef USE_OSC
  if (oscServer.aviableCheck()>0){
    //we need to call available check to update the osc server
  }
#endif

  //check if the effect should be updated or not
  if (currentDelay>0) {
    //delay not finished yet - do not modify the strip but read network messages
    currentDelay--;
    delay(1);
    
#ifdef USE_OSC    
    //get encrypted tcp traffic 
    handleEncryptedTraffic();
#endif
  } 
  else {
    //delay finished, update the strip content
    currentDelay=oscDelay;    
    loopAnimationMode();
    strip.show();
    frame++;
    
#ifdef USE_OSC    
    if (frame%50000==1) {
#ifdef USE_SERIAL_DEBUG
  Serial.println("OSC Ping");
#endif  
      sendOscPingToServer();
    }
#endif

  }
  
}








