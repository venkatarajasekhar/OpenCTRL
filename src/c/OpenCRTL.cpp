#include "WProgram.h"
#include <NewSoftSerial.h>

#include "lib/protocol.h"
#include "lib/debug.h"

#include "OpenCRTL.h"

#define MASTER 1
#define SLAVE 2

#if SER_DEVICE_TYPE != MASTER && SER_DEVICE_TYPE != SLAVE
#error Device type must be MASTER or SLAVE
#endif

#if DEVICE_ID <= 0 || DEVICE_ID > 255
#error Device ID must be between 1 and 255
#endif

#define isChecksumValid() (*((uint16*)ptrChecksumStart) == nChecksum)
#define getChecksum() *((uint16*)ptrChecksumStart)

// generic bus status
bool bBusBusy = false; // set when input occures or we are ouputting
uint8 nNetworkID = isMaster() ? DEVICE_ID : 0;
uint8 nDeviceID = DEVICE_ID;
uint8 nMasterID = isMaster() ? DEVICE_ID : 0;

// serial input variables (buffers and indexes)
SPacket sInput;
uint8 *ptrInputBuffer = NULL;
uint8 *ptrInputFinished = NULL;
uint8 *ptrChecksumStart = NULL;
uint16 nChecksum = 0;
uint8 nLastChar = 0;
bool bInvalidPacket = false;

// serial output buffers
SPacket sOutput;
bool bOutputReady = false;
bool bWaitForResponse = false;
uint8 nLastPacketID = 0; // remember to check if the master recved our 'interrupt / state change'

int nTimeoutCounter = SERIAL_TIMEOUT_LIMIT;

void setup(void)
{
     dbgInitialize();
     dbgPrintln("Loading OpenCTRL...");
     initSerial();
     initOpenCTRL();
     dbgPrintln("Running as (%s) with Device ID (%d) and Network ID (%d)", isMaster() ? "MASTER" : "SLAVE", nDeviceID, nNetworkID);

     // only for startup use delay and start to make sure the bus is empty before trying to send
     delay(500);
     readSerial();
     delay(500);
     readSerial();
}

void loop(void)                     // run over and over again
{
     // main loop
     sendData();
     delay(SERIAL_WAIT_TIME);
     readSerial();
}

void initSerial()
{
     serBus.begin(9600);
     ptrInputBuffer = (uint8 *)&sInput;
     ptrChecksumStart = &sInput.data[SER_MAX_DATA_LENGTH - 1];
}

void initOpenCTRL()
{
// TODO only when device is not set master!!! If is set master start initial ping to all nods... 
// BUG How does the device know when it's newly added to the BUS or it sufferd from power loss, not all MCU's have Brown Out Register
#ifdef MAX485_PIN
     pinMode(MAX485_PIN, OUTPUT);
#endif

     if (isMaster()) // TODO check master pin
	  return; // scanNetwork()
     else
	  sendHello();
}

void readSerial()
{
     while (serBus.available())
     {
	  nTimeoutCounter = SERIAL_TIMEOUT_LIMIT;
	  bBusBusy = true;
	  
	  // bits... go parse!
	  *ptrInputBuffer++ = nLastChar = (char)serBus.read();

	  dbgPrintln("Read char: %u - %c", nLastChar, nLastChar);

	  if (ptrInputBuffer < ptrChecksumStart) // only add non checksum to checksum ;)
	       nChecksum += nLastChar;
	  
	  if (ptrInputBuffer == &sInput.data[0]) // input buffer is now starting to write data so we have the header
	  {
	       // we have a header! Awesome! Calculate length and only parse it when whole packet is in!
	       // got whole packet when memory address == data start + packetlength + checksum (2 bytes)
	       // calculate end address of packet
	       ptrChecksumStart = &sInput.data[0] + (sInput.header.m_nPacketLength <= SER_MAX_DATA_LENGTH ? sInput.header.m_nPacketLength : 0);
	       ptrInputFinished = ptrChecksumStart + CHECKSUM_SIZE;
	  }
	  else if (ptrInputBuffer == ptrInputFinished) // we got the whole packet let's parse
	  {
	       dbgPrintln("Checksum should be: %d", getChecksum());
	       dbgPacket(&sInput);

	       if (!bInvalidPacket)
	       {
		    // we got the whole packet! Check if we have anything to do with it...
		    if (
			 (sInput.header.m_nDestinationNetwork == nNetworkID && sInput.header.m_nDestinationDevice == nDeviceID) // directed at us
			 || (sInput.header.m_nDestinationNetwork == nNetworkID && sInput.header.m_nDestinationDevice == 0 && sInput.header.m_nSourceDeviceID == nMasterID) // broadcast msg from master
			 ) // end of if
		    {
			 // packet is directed at us \o/
			 if (isChecksumValid())
			 {
			      // if good parse rest of the packet
			      if (sInput.header.m_nPacketLength > SER_MAX_DATA_LENGTH)
			      {
				   handleProtocolPacket(); // protocol packet parse the fucker
				   recFinished(); // data handled clear buffers
			      }
			 }
			 else
			   dbgPrintln("Checksum didn't match!");
		    }
		    else
		    {
			 // we got nothing to do with it reset buffer for new packet
			 recFinished();
			 dbgPrintln("Packet not for us, ignoring...");
		    }
	       }
	       else
	       {
		    // invalid packet reset buffers
		    recFinished();
		    dbgPrintln("Invalid packet! To bad!");
	       }
	  }
	  else if (ptrInputBuffer == &sInput.data[SER_MAX_DATA_LENGTH])
	  {
	       // packet longer then RFC mark invalid and reset pointer to prefent buffer overflow ;)
	       ptrInputBuffer = &sInput.data[0]; // overwrite previous data invalid anywayzz
	       bInvalidPacket = true;
	       dbgPrintln("WARNING!! Input buffer overflow!");
	  }

	  // just another char... we don't care ^.^
     }
     
     if (bBusBusy) // bus is busy but we didn't recieve any data 
     {
	  timeoutProtection();
     }
}

inline void timeoutProtection()
{
     if (--nTimeoutCounter == 0)
     {
	  dbgPrintln("WARNING! Packet timeout!");
	  recFinished();
     }
}

// make this some struct array for automated function calling
int handleProtocolPacket(void)
{
     switch (sInput.header.m_nPacketLength)
     {
     case OCTRL_PING:
	  sendPong();
	  break;

//     case OCTRL_PONG: // Should be placed in the firmware not in the driver...
//	  recvPong();
//	  break;

     case OCTRL_HELLO:
	  sendWelcome();
	  break;

     case OCTRL_WELCOME:
	  recvWelcome();
	  break;
     }
}

//int sendData(char *header, char *sendData, char nDataLength)
int sendData(void )
{
     if (!bBusBusy && bOutputReady)
     {
	  dbgPrintln("Trying to send data!");

	  // if bus full don't send yet and just return and keep buffers
	  // else we can send the data
	  uint8 *ptrOutputBuffer = (uint8 *)&sOutput; // output buffer start
	  uint8 *ptrOutputFinished = (ptrOutputBuffer + sizeof(SSerialHeader) + (sOutput.header.m_nPacketLength > SER_MAX_DATA_LENGTH ? 0 : sOutput.header.m_nPacketLength));
	  uint16 *ptrChecksum = (uint16 *)ptrOutputFinished;
	  uint16 *ptrChecksumFinished = (uint16 *)(ptrOutputFinished + 2); // got 16 bit checksum...
	  *ptrChecksum = 0; // initialize @ 0

#ifdef MAX485_PIN
	  digitalWrite(MAX485_PIN, HIGH);
#endif

	  // barf whole buffer to bus...
	  while (ptrOutputBuffer < ptrOutputFinished)
	  {
	       *ptrChecksum += *ptrOutputBuffer;
	       serBus.print(*ptrOutputBuffer++, BYTE);
	  }
	  
	  // write checksum to bus
#if BIG_ENDIAN
	  ptrOutputBuffer = (uint8 *)ptrChecksumFinished;
	  while (ptrOutputBuffer > (uint8 *)ptrChecksum)
	  {
	       serBus.print(*ptrOutputBuffer--, BYTE);
	  }
#else
	  while (ptrOutputBuffer < (uint8 *)ptrChecksumFinished)
	  {
	       serBus.print(*ptrOutputBuffer++, BYTE);
	  }
#endif

	  dbgPacket(&sOutput, *ptrChecksum);

#ifdef MAX485_PIN
	  delay(100);
	  digitalWrite(MAX485_PIN, LOW);
#endif

	  if (! bWaitForResponse)
	       sendFinished();
     }
}

int recFinished(void)
{
     dbgPrintln("Clearing recording buffer");
     bBusBusy = false;
     nChecksum = 0;
     nTimeoutCounter = SERIAL_TIMEOUT_LIMIT;
     bInvalidPacket = false;
     ptrInputBuffer = ptrInputFinished = (uint8 *)&sInput;
     ptrChecksumStart = &sInput.data[SER_MAX_DATA_LENGTH - 1];
}

int sendFinished(void)
{
     bOutputReady = false;
     bWaitForResponse = false;
}

int recvWelcome(void)
{
     if (bWaitForResponse && sInput.header.m_nPacketID == nLastPacketID)
     {
	  dbgPrintln("Valid WELCOME packet");
	  nMasterID = sInput.header.m_nSourceDeviceID;
	  nNetworkID = sInput.header.m_nSourceNetwork;

	  dbgPrintln("Now joined bus (%d) with master (%d)", sInput.header.m_nSourceNetwork, sInput.header.m_nSourceDeviceID);

	  // we got our network \o/
	  sendFinished();
     }
     else
     {
	  dbgPrintln("Invalid WELCOME packet... ignoring!");
     }
}

int sendWelcome(void)
{
     if (! bOutputReady)
     {
	  dbgPrintln("Perparing WELCOME packet");
	  // set welcome type
	  sOutput.header.m_nPacketLength = OCTRL_WELCOME;
	  
	  // set output header
	  sOutput.header.m_nDestinationNetwork = nNetworkID;
	  sOutput.header.m_nDestinationDevice = sInput.header.m_nSourceDeviceID;
	  sOutput.header.m_nSourceDeviceID = nDeviceID;
	  sOutput.header.m_nSourceNetwork = nNetworkID;
	  
	  // set packet ID to recv packet id... for obious reasons
	  sOutput.header.m_nPacketID = sInput.header.m_nPacketID;

	  // only send, if it failes the device will ask again ;)
	  bOutputReady = true;
     }
     else
     {
	  dbgPrintln("sendWelcome(): Output buffer already filled!");
     }
}

int sendHello(void)
{
     if (! bOutputReady)
     {
	  dbgPrintln("Preparing HELLO packet");

	  // send HELLO packet to bus se we can identify ourselfs
	  sOutput.header.m_nPacketLength = OCTRL_HELLO;
	  
	  // set address
	  sOutput.header.m_nDestinationDevice = 0;
	  sOutput.header.m_nDestinationNetwork = 0;
	  sOutput.header.m_nSourceNetwork = 0;
	  sOutput.header.m_nSourceDeviceID = nDeviceID;
	  
	  // set packet ID
	  sOutput.header.m_nPacketID = nLastPacketID = 4; //TODO this is a truely random number chosen by the roll of a dice
	  
	  bOutputReady = true;
	  bWaitForResponse = true;
	  // set the buffer filled flag so sendData will be called later on... why w8? First we need to monitor to check if the bus is free! =)
     }
     else
     {
	  dbgPrintln("sendHello(): Output buffer already filled!");
     }
}

int sendPong(void)
{
     if (! bOutputReady)
     {
	  dbgPrintln("Preparing PONG packet");
	  // send PONG packet to bus
	  sOutput.header.m_nPacketLength = OCTRL_PONG;
	  
	  // set address
	  sOutput.header.m_nDestinationDevice = sInput.header.m_nSourceDeviceID;
	  sOutput.header.m_nDestinationNetwork = sInput.header.m_nSourceNetwork;
	  sOutput.header.m_nSourceNetwork = nNetworkID;
	  sOutput.header.m_nSourceDeviceID = nDeviceID;
	  
	  // set packet ID
	  sOutput.header.m_nPacketID = sInput.header.m_nPacketID; 
	  
	  bOutputReady = true;
     }
     else
     {
	  dbgPrintln("sendPong(): Output buffer already filled!");
     }
}