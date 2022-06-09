#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <ArduinoHttpClient.h>
#include "arduino_secrets.h"


const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

unsigned int localPort = 8888; // local port to listen for UDP packets
const char timeServer[] = "pl.pool.ntp.org";
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets

const char grafanaHostname[] = "graphite-prod-01-eu-west-0.grafana.net";

EthernetUDP Udp;
EthernetClient c;
HttpClient http(c, grafanaHostname);

// send an NTP request to the time server at the given address
void sendNTPpacket(const char *address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;          // Stratum, or type of clock
  packetBuffer[2] = 6;          // Polling Interval
  packetBuffer[3] = 0xEC;       // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

// Get current time from NTP server
unsigned long time()
{

  sendNTPpacket(timeServer); // send an NTP packet to a time server

  // wait to see if a reply is available
  while (Udp.parsePacket() == 0)
  {
    delay(10);
  }

  // We've received a packet, read the data from it
  Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

  // the timestamp starts at byte 40 of the received packet and is four bytes,
  // or two words, long. First, extract the two words:

  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;
  Serial.print("Seconds since Jan 1 1900 = ");
  Serial.println(secsSince1900);

  // now convert NTP time into everyday time:
  Serial.print("Unix time = ");
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  unsigned long epoch = secsSince1900 - seventyYears;

  return epoch;
}

// Print time on serial console
void printTime(unsigned long ts)
{
  // print the hour, minute and second:
  Serial.print("The UTC time is ");   // UTC is the time at Greenwich Meridian (GMT)
  Serial.print((ts % 86400L) / 3600); // print the hour (86400 equals secs per day)
  Serial.print(':');
  if (((ts % 3600) / 60) < 10)
  {
    // In the first 10 minutes of each hour, we'll want a leading '0'
    Serial.print('0');
  }
  Serial.print((ts % 3600) / 60); // print the minute (3600 equals secs per minute)
  Serial.print(':');
  if ((ts % 60) < 10)
  {
    // In the first 10 seconds of each minute, we'll want a leading '0'
    Serial.print('0');
  }
  Serial.println(ts % 60); // print the second
}

void sendHeartbeat(unsigned long ts)
{
  String payload = String("[{\"name\": \"sandbox.a1.heartbeat\", \"time\":" + String(ts) + ", \"interval\":60, \"value\":1}]");

  http.beginRequest();
  http.post("/graphite/metrics");
  http.sendBasicAuth(GRAPHITE_USER, GRAPHITE_KEY);
  http.sendHeader(HTTP_HEADER_CONTENT_TYPE, "application/json");
  http.sendHeader(HTTP_HEADER_CONTENT_LENGTH, payload.length());
  http.beginBody();
  http.write((const byte *) payload.c_str(), payload.length());
  http.endRequest();

  int statusCode = http.responseStatusCode();
  String response = http.responseBody();

  Serial.print("Heartbet Status code: ");
  Serial.println(statusCode);
}

void setup()
{
  Ethernet.init(10); // Most Arduino shields

  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial)
  {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // start Ethernet and UDP
  if (Ethernet.begin(mac) == 0)
  {
    Serial.println("Failed to configure Ethernet using DHCP");
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware)
    {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
    }
    else if (Ethernet.linkStatus() == LinkOFF)
    {
      Serial.println("Ethernet cable is not connected.");
    }
    // no point in carrying on, so do nothing forevermore:
    while (true)
    {
      delay(1);
    }
  }
  Udp.begin(localPort);
}

void loop() {
  unsigned long ts = time();
  printTime(ts);
  sendHeartbeat(ts);
  delay(60000);
  Ethernet.maintain();
}