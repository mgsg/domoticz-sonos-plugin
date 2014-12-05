/*+----------------------------------------------------------------------------+*/
/*| Permission is hereby granted, free of charge, to any person obtaining a    |*/
/*| copy of this software and associated documentation files (the "Software"), |*/
/*| to deal in the Software without restriction, including without limitation  |*/
/*| the rights to use, copy, modify, merge, publish, distribute, sublicense,   |*/
/*| and/or sell copies of the Software, and to permit persons to whom the      |*/
/*| Software is furnished to do so, subject to the following conditions:       |*/
/*|                                                                            |*/
/*| The above copyright notice and this permission notice shall be included    |*/
/*| in all copies or substantial portions of the Software.                     |*/
/*|                                                                            |*/
/*| THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR |*/
/*| IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   |*/
/*| FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    |*/
/*| THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER |*/
/*| LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    |*/
/*| FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        |*/
/*| DEALINGS IN THE SOFTWARE.                                                  |*/
/*|                                                                            |*/
/*+----------------------------------------------------------------------------+*/
/*| Ideas and source code excerpts borrowed from:                              |*/
/*|     http://forums.sonos.com/showthread.php?t=20721                         |*/
/*| Integration in Domoticz done by: m_g_s_g                                   |*/
/*+----------------------------------------------------------------------------+*/
/*| See also: How did I develop a Domoticz hardware plugin                     |*/
/*| Domoticz forum: http://www.domoticz.com/forum/viewtopic.php?f=28&t=3721    |*/
/*+----------------------------------------------------------------------------+*/
/*| Planned functionality:                                                     |*/
/*+----------------------------------------------------------------------------+*/
/*| 1. Update the speaker status (on/off), to be able to trigger other events  |*/
/*|    (f.i.: lights on when I play music) - WORKING                           |*/
/*| 2. Switch them on/off                  - WORKING                           |*/
/*| 3. Set volume up/down (+-10%)          - WORKING (dimmer)                  |*/
/*| 3.1. Use dimmer for volume up / down   - WORKING                           |*/
/*| 4. Implementing a "say" command to use for home automation messages:       |*/
/*|    "Temperature in XXX over YYYºC"     - WORKING                           |*/
/*| Goal is to implement only essential functionality for home automation. The |*/
/*| rest can be done much better on Sonos controller, or other similar tools.  |*/
/*+----------------------------------------------------------------------------+*/
/*| Known bugs/limitations:                                                    |*/
/*+----------------------------------------------------------------------------+*/
/*| - Volume level ocassionally doesn't refresh                                |*/
/*| - TTS say device only enabled for Sonos speakers                           |*/
/*| - Glib instead of BOOST libraries used (much has been already migrated)    |*/
/*| - Polling inteval must be lower than 20secs; if not: you get Error:        |*/
/*|   Sonos hardware (11) thread seems to have ended unexpectedly              |*/
/*| - Play:1 temp sensor monitor disabled by default -limited interest for HA  |*/
/*+----------------------------------------------------------------------------+*/
/*| Version history:                                                           |*/
/*+----------------------------------------------------------------------------+*/
/*| v0.1 Initial version. Update speaker status.                               |*/
/*| v0.2 Update speaker status. Switch them (play/pause) from domoticz UI.     |*/
/*| v0.3 Set volume up/down using the dimmer slider                            |*/
/*| v0.4 TTS device for Sonos speakers using Google Traslate. Presets.         |*/
/*| v0.5 Play/Pause working on XBMC - possibly on other UPnP devices too       |*/
/*+----------------------------------------------------------------------------+*/
#include "stdafx.h"
#include "../main/Helper.h"
#include "../main/Logger.h"
#include "hardwaretypes.h"
#include "../main/localtime_r.h"
#include "../main/mainworker.h"
#include "../main/SQLHelper.h"
#include "../httpclient/HTTPClient.h"
#include "../httpclient/UrlEncode.h"
#include <wchar.h>
#include <sys/stat.h>

// For hash calculation
// #include <iostream>
// #include <fstream>
#include <boost/uuid/sha1.hpp>
// #include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/unordered_map.hpp>

// Conditional compiling
//  #define _DEBUG							true
//  #define PLAY1_GET_TEMPERATURE		true
#define CREATE_TTS_NON_SONOS			true
#define _DEBUG_CHANGE					true

#include "SonosPlugin.h"

// UPnP constants
#define UPNP_STATE_STOPPED				0
#define UPNP_STATE_PLAYING				1
#define UPNP_STATE_TRANSITIONING		2
#define UPNP_STATE_PAUSED				3
#define UPNP_STATE_NO_MEDIA_PRESENT		4

#define UPNP_MEDIA_RENDERER				"urn:schemas-upnp-org:device:MediaRenderer:1"
#define UPNP_MEDIA_SERVER				"urn:schemas-upnp-org:device:MediaServer:1"
#define UPNP_AV_TRANSPORT				"urn:schemas-upnp-org:service:AVTransport"
#define UPNP_RENDERING_CONTROL			"urn:schemas-upnp-org:service:RenderingControl:1"
#define UPNP_CONTENT_DIRECTORY			"urn:schemas-upnp-org:service:ContentDirectory:1"
#define UPNP_ALL						"ssdp:all"

#define UPNP_NULL						0
#define UPNP_SONOS						1
#define UPNP_PLAY1						2
#define UPNP_XBMC						3
#define UPNP_OTHERS						9

// Sonos constants
#define SONOS_SAVED_QUEUE_NAME			"SQ:24"
#define SONOS_PROTOCOL_NONE 			0
#define SONOS_PROTOCOL_HTTP 			1
#define SONOS_PROTOCOL_FILE				2
#define SONOS_PROTOCOL_LINEIN			3
#define SONOS_PROTOCOL_RADIO			4
#define SONOS_PROTOCOL_QUEUE			5

// Domoticz constants
#define sonos_sPause					0		// light2_sOn
#define sonos_sPlay						1		// light2_sOff
#define sonos_sSetLevel					2		// light2_sSetLevel
#define sonos_sSay						6
#define sonos_sPreset1					7
#define sonos_sPreset2					8

// Domoticz user variables
#define PRESET_LEVEL					5

#define USERVAR_TTS						"sonos-tts"
#define USERVAR_PRESET1					"sonos-preset-1"
#define USERVAR_PRESET2					"sonos-preset-2"

// Domoticz subdevices for a Sonos "compound device"
#define UNIT_Sonos_PlayPause			0
#define UNIT_Sonos_TempPlay1			1
#define UNIT_Sonos_Say					2
#define UNIT_Sonos_Preset1				3
#define UNIT_Sonos_Preset2				4

#define RUN_TIME						19

// Domoticz www folder
extern std::string szWWWFolder;
extern MainWorker m_mainworker;			// in Domoticz.cpp - to get www port

#if defined WIN32
#elif defined __linux__

/*+----------------------------------------------------------------------------+*/
/*| GUPnP Stuff                                                                |*/
/*+----------------------------------------------------------------------------+*/
static GMainLoop					*main_loop;
static GUPnPLastChangeParser		*lc_parser;

/* Callbacks need to be static "C" functions; didn't discover a way to migrate */
static gboolean callbackTimeout(void *data);
static void callbackDeviceDiscovered(GUPnPControlPoint *cp, 
	GUPnPDeviceProxy *proxy);
static void callbackLastChange( GUPnPServiceProxy *av_transport, 
	const char *variable_name, GValue *value, gpointer user_data);
static void callbackDeviceUnavailable(GUPnPControlPoint *cp, 
	GUPnPDeviceProxy *renderer);
static void callbackGetPlay1Temperature(RendererDeviceData *upnpdevice);
static void callbackGetTrackInfo(GUPnPDIDLLiteParser *parser, 
	GUPnPDIDLLiteObject *object, gpointer renderer);
#endif

/* Store / maps for UPnP devices */
typedef boost::unordered_map<std::string,std::string>					MediaDevicesMap;
typedef boost::unordered_map<std::string,RendererDeviceData*>			RenderersMap;
typedef boost::unordered_map<std::string,ServerDeviceData*>				ServersMap;

typedef boost::unordered_map<std::string,std::string>::iterator			IteratorMediaDevice;
typedef boost::unordered_map<std::string,RendererDeviceData*>::iterator	IteratorRenderer;
typedef boost::unordered_map<std::string,ServerDeviceData*>::iterator	IteratorServer;

static MediaDevicesMap				mediadevicesmap;	// Store MediaRenderers + MediaServers - key=udn data=long ip
static RenderersMap					renderersmap;	// Store MediaRenderers - key=long ip data=RendererDeviceData
static ServersMap					serversmap;		// Store MediaServers - key=long ip data=RendererDeviceData

std::string							m_host_ip;
std::string							m_ttsLanguage("EN");	// "ES";


/* them to C++ */
static CSonosPlugin					*thisInstance;	// Dirty trick-access CSonosPlugin 
													// methods from "C" callbacks
static int							hwIdStatic;		// idem

/* Utilities */
unsigned long helperGetIpFromLocation(const char *szLocation, char *szIP );
std::string helperCreateHash(std::string a);
std::string helperGetUserVariable(const std::string &name);
bool helperListMaps(void);
bool helperChangeProtocol(std::string &url, int type);

/*+----------------------------------------------------------------------------+*/
/*| CSonosPlugin class.                                                        |*/
/*+----------------------------------------------------------------------------+*/
CSonosPlugin::CSonosPlugin(const int ID)
{
	m_HwdID=ID;
	m_stoprequested=false;
	m_bEnabled=true;
#ifdef WIN32
	//sorry Win32 not supported for now
#endif
}


CSonosPlugin::~CSonosPlugin(void)
{
#ifdef WIN32
	//sorry Win32 not supported for now
#endif
	StopHardware();
}

/*
 *
 */
bool CSonosPlugin::StartHardware()
{
#ifdef _DEBUG
        _log.Log(LOG_ERROR,"(Sonos) Started");
#endif
	Init();

	if (m_bEnabled)
	{
		m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CSonosPlugin::Do_Work, this)));
	}
	return true;
}

/*
 *
 */
bool CSonosPlugin::StopHardware()
{
	if (!m_bEnabled)
		return false;

#ifdef __linux__
	// quit upnp loop
	g_main_loop_quit (main_loop);
#endif        

	if (m_thread!=NULL)
	{
		m_stoprequested = true;
		m_thread->join();
	}

	return true;
}

/*
 * Plugin Initialization.
 */
void CSonosPlugin::Init()
{
#ifdef __linux__

#else //  WIN32 __APPLE__
	//sorry Win32 / Apple not supported for now
	m_bEnabled=false;
	return;
#endif

	// Check if there is already hardware running for System, if no start it.
	m_lastquerytime=0;
	hwId = 0;
	std::stringstream szQuery;
	std::vector<std::vector<std::string> > result;
	szQuery << "SELECT ID,Enabled FROM Hardware WHERE (Type=='" <<HTYPE_SonosPlugin << "') LIMIT 1";
	result=m_sql.query(szQuery.str());

	if (result.size()>0) {
		std::vector<std::string> sd=result[0];
		hwId=atoi(sd[0].c_str());
		hwIdStatic = hwId;
		m_bEnabled=atoi(sd[1].c_str())!=0;
	}

	int nValue;
	m_sql.GetPreferencesVar("Language", nValue, m_ttsLanguage);
	
	_log.Log(LOG_STATUS,"(Sonos) Init(). Language %s", m_ttsLanguage.c_str());
}

void CSonosPlugin::Do_Work()
{
	// Discover Sonos devices
	SonosInit();

	_log.Log(LOG_STATUS,"(Sonos) Stopped...");			
}

/*+----------------------------------------------------------------------------+*/
/*| WriteToHardware - Code to write from Domoticz UI to the hardware layer     |*/
/*+----------------------------------------------------------------------------+*/
void CSonosPlugin::WriteToHardware(const char *pdata, const unsigned char length)
{
	tRBUF *pCmd=(tRBUF*) pdata;

	if (pCmd->LIGHTING2.packettype == pTypeLighting2) {
		unsigned int id1, id2, id3, id4;
		unsigned long ulIpAddress;

		// Decode from id1-id4 something that allows us to access the specific hardware
        ulIpAddress = (pCmd->LIGHTING2.id1 << 24) + (pCmd->LIGHTING2.id2 << 16) + 
			(pCmd->LIGHTING2.id3 << 8) + pCmd->LIGHTING2.id4;
		std::stringstream ss;
		ss << std::uppercase << std::setfill('0') << std::setw(8) << std::hex <<  ulIpAddress;
		std::string deviceID(ss.str());

		/* Look up the renderer device data from IP */
	    RendererDeviceData *upnpdevice;
		IteratorRenderer it = renderersmap.find(deviceID);
		upnpdevice = (RendererDeviceData *)it->second;

		int unit = (pCmd->LIGHTING2.unitcode);
		int cmnd = (pCmd->LIGHTING2.cmnd);
		int level = (pCmd->LIGHTING2.level);
		
		if (pCmd->LIGHTING2.unitcode == UNIT_Sonos_PlayPause ) {
			if (pCmd->LIGHTING2.cmnd==sonos_sPause) {
				// STOP
				SonosActionPause( upnpdevice );
				upnpdevice->level	= SonosActionGetVolume( upnpdevice  );
				upnpdevice->prev_state = UPNP_STATE_STOPPED;
#ifdef _DEBUG
				_log.Log(LOG_NORM,"(Sonos) WriteToHardware Pause devid %8X cmnd %d unit %d level %d", 
					ulIpAddress, cmnd, unit, level);
#endif
			} else if (pCmd->LIGHTING2.cmnd==sonos_sSetLevel) {
				SonosActionSetVolume( upnpdevice, level );
#ifdef _DEBUG
				_log.Log(LOG_NORM,"(Sonos) WriteToHardware %s Volume from %d to %d", 
					upnpdevice->name.c_str(), upnpdevice->level, level);
#endif			
				upnpdevice->level	= level;
			} else if (pCmd->LIGHTING2.cmnd==sonos_sPlay) {
				// PLAY
				if (upnpdevice->prev_state == UPNP_STATE_STOPPED) {
					SonosActionSetVolume( upnpdevice, upnpdevice->level );
				}
				upnpdevice->prev_state = UPNP_STATE_PLAYING;

				// Check if there's something to play
				std::string currenturi;
				SonosActionGetPositionInfo(upnpdevice, currenturi);
				if (currenturi != "") {
					// Play!
					SonosActionPlay( upnpdevice );
#ifdef _DEBUG
					_log.Log(LOG_NORM,"(Sonos) WriteToHardware Play devid %8X cmnd %d unit %d level %d", 
						ulIpAddress, cmnd, unit, level);
#endif
				} else {
#ifdef _DEBUG
					_log.Log(LOG_NORM,"(Sonos) WriteToHardware Nothing to play!", 
						ulIpAddress, cmnd, unit, level);
#endif
				}
			}
		} else if ((pCmd->LIGHTING2.unitcode == UNIT_Sonos_Preset1) || 
			       (pCmd->LIGHTING2.unitcode == UNIT_Sonos_Preset2) ||
				   (pCmd->LIGHTING2.unitcode == UNIT_Sonos_Say )) {
			std::string sURL;

			if (pCmd->LIGHTING2.unitcode == UNIT_Sonos_Say ) {
				std::string uservar(USERVAR_TTS);
				std::string text = helperGetUserVariable(uservar);
				if (text == "")
					text = "Welcome to the Domoticz home";
				sURL = "http://192.168.1.63:8888/media/tts-text.mp3";
				bool ret;

				ret = SonosActionSay( text, sURL, upnpdevice->type );
				ret = true;
				if (ret) {
					_log.Log(LOG_NORM,"(Sonos) Say \'%s\' saved to \'%s\'", text.c_str(), sURL.c_str());
				}
			} else {
				std::string preset_station;
				if (pCmd->LIGHTING2.unitcode==UNIT_Sonos_Preset1)
					preset_station = std::string(USERVAR_PRESET1);
				else {
					preset_station = std::string(USERVAR_PRESET2);
#ifdef _DEBUG
					helperListMaps();
#endif
				}

				sURL = helperGetUserVariable(preset_station);
				if (upnpdevice->type == UPNP_SONOS) {
					if (sURL == "")
						sURL = "x-rincon-mp3radio://radioclasica.rtve.stream.flumotion.com/rtve/radioclasica.mp3.m3u";
				} else {
					if (sURL == "")
						sURL = "http://radioclasica.rtve.stream.flumotion.com/rtve/radioclasica.mp3.m3u";
				}
			}

			helperChangeProtocol(sURL, upnpdevice->type);

			// Save state - before you "Unlink" a ZP, you have to figure out if it's 
			//   a) linked to another ZP
			//   b) streaming radio
			//   c) streaming an audio line in or 
			//   d) playing its own queue of stuff. 
			// Depending on which (a through d) it is doing, when it's time to "restore" 
			// that ZP back to its original state, you have to take very different steps.

			if (upnpdevice->type == UPNP_SONOS) {
				/* Save current uri and state */
				std::string state;
				SonosActionGetTransportInfo(upnpdevice, state);

				std::string currenturi;
				SonosActionGetPositionInfo(upnpdevice, currenturi);

				// d) Save Queue / State
				SonosActionSaveQueue(upnpdevice);

				// Set current and next URI and play - UNLINK!
				SonosActionSetURI(upnpdevice, sURL );

				SonosActionPlay(upnpdevice);

				// Restore session
				upnpdevice->restore_state = true;


			} else {

				// Set current and next URI and play - UNLINK!
				SonosActionSetURI(upnpdevice, sURL );

				SonosActionPlay(upnpdevice);
			}

#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) WriteToHardware Preset devid %8X cmnd %d unit %d level %d", 
						ulIpAddress, cmnd, unit, level);
#endif
		}
	} else {
		_log.Log(LOG_ERROR,"(Sonos) WriteToHardware packet type %d or subtype %d unknown", pCmd->LIGHTING2.packettype, pCmd->LIGHTING2.subtype);
	}

	return;
}


/*+----------------------------------------------------------------------------+*/
/*| UpdateValueEasy - Update domoticz device status for the Sonos devices      |*/
/*+----------------------------------------------------------------------------+*/
void CSonosPlugin::UpdateValueEasy(int qType, 
                                   const std::string& devId, 
								   const std::string& devName, 
								   const std::string& devValue,
								   int level)
{
	int dtype=0, dsubtype=-1, dunit;
	std::string suffixName;
	std::stringstream szQuery;
	std::vector<std::vector<std::string> > result;			// resultAux;
	unsigned long ulIdx=0;
	unsigned long ulIpAddress;

	if (!hwId) {
	   _log.Log(LOG_ERROR,"(Sonos) Id not found!");
	   return;
	}

	// A sonos speaker is a compound device with:
	// - A PlayPause switch with a dimmer for volume
	// - A Say pushbutton, to play a text (stored in user variable)
	// - Multiple preset pushbuttons, each one pointing to a uri (stored in user variable)
	// - A Temperature Sensor, just for Play:1 speakers - not very useful - always around 38o Celsius
	// - More could be feasible, but not very useful for HA (shuffle, )...
	if (qType==UNIT_Sonos_PlayPause) {
		dtype=pTypeLighting2;
		dsubtype=sTypeAC;
		dunit=qType;
		suffixName = " Play/Pause";
	} else if (qType==UNIT_Sonos_TempPlay1) {
		dtype=80; // pTypeGeneral;
		dsubtype=1; // sTypeSystemTemp;
		dunit=qType;
		suffixName = " Temp";
	} else if (qType==UNIT_Sonos_Say) {
		dtype=pTypeLighting2;
		dsubtype=0x04;		// sTypeSonos
		dunit=qType;
		suffixName = " TTS";
	} else if (qType==UNIT_Sonos_Preset1) {
		dtype=pTypeLighting2;
		dsubtype=0x04;		// sTypeSonos
		dunit=qType;
		suffixName = " P1";	
	} else if (qType==UNIT_Sonos_Preset2) {
		dtype=pTypeLighting2;
		dsubtype=0x04;		// sTypeSonos
		dunit=qType;
		suffixName = " P2";	
	} else {
		// Unsupported
		return;
	}

	std::istringstream iss(devId);
	iss >> std::hex >> ulIpAddress;
#ifdef _DEBUG
//	_log.Log(LOG_NORM,"(Sonos) Update1 devId %s LIP %8X Value %s", devId.c_str(), ulIpAddress, devValue.c_str());
#endif
	szQuery << "SELECT ID, Name FROM DeviceStatus WHERE (DeviceID=='" << devId << "' AND Unit=" << dunit << ")";
	result=m_sql.query(szQuery.str());
	
	// Device is not already added to device list - insert
	if (result.size()<1)		
	{
		szQuery.clear();
		szQuery.str("");

		// We use a easy devname
		std::stringstream ssName;
		ssName << devName << suffixName;
		std::string devNameEasy = ssName.str();
		szQuery << 
			"INSERT INTO DeviceStatus (HardwareID, DeviceID, Unit, Type, SubType, SignalLevel, BatteryLevel, Name, nValue, sValue) "
			"VALUES (" << hwId << ",'" << devId << "',"<< dunit << "," << dtype << "," <<dsubtype << ",12,255,'" << devNameEasy << "'," << devValue << ",'" << devValue << "')";
		result=m_sql.query(szQuery.str());
		if (result.size()<0) {
			_log.Log(LOG_ERROR,"UpdateValueEasy: database error, inserting devID %s", devId.c_str());
			return;
		}

		// Get newly created ID
		szQuery.clear();
		szQuery.str("");
		szQuery << 
			"SELECT ID FROM DeviceStatus WHERE (HardwareID=" << hwId <<" AND DeviceID='" << devId << "' AND Unit=" << dunit << " AND Type=" << dtype << " AND SubType=" << dsubtype <<")";
		result=m_sql.query(szQuery.str());
		if (result.size()<0) {
			_log.Log(LOG_ERROR,"UpdateValueEasy: database error, problem getting ID from DeviceStatus for devID %s!", devId.c_str());
			return;
		}

		// Read index in DB
		sscanf(result[0][0].c_str(), "%d", &ulIdx );
		_log.Log(LOG_NORM,"(Sonos) Insert devID %s devName \'%s\' devIdx %d LIP %x", 
			devId.c_str(), devNameEasy.c_str(), ulIdx, ulIpAddress );
	} else {
		// Device already added to device list - update

		// get idx and name
		sscanf(result[0][0].c_str(), "%d", &ulIdx );
		std::string strDevName(result[0][1]);

		time_t now = time(0);
		struct tm ltime;
		localtime_r(&now,&ltime);

		char szLastUpdate[40];
		sprintf(szLastUpdate,"%04d-%02d-%02d %02d:%02d:%02d",ltime.tm_year+1900,ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Update devID %s devName \'%s\' devIdx %d LIP %8x Value %s", 
			devId.c_str(), strDevName.c_str(), ulIdx, ulIpAddress, devValue.c_str() );
#endif
		szQuery.clear();
		szQuery.str("");
		szQuery << "UPDATE DeviceStatus SET HardwareID = " << hwId << ", nValue=" << devValue << ", sValue ='" << devValue << "', LastUpdate='" << szLastUpdate << "' WHERE (DeviceID == '" << devId << "' AND Unit=" << dunit << ")";
		m_sql.query(szQuery.str());

		// UNIT_Sonos_TempPlay1 - Temperature Device for Play:1
		if (qType == UNIT_Sonos_TempPlay1) {
			// Maybe I had to use TEMP like in the UNIT_Sonos_PlayPause case...
			// but TEMP just has id1 and id2... No way to code all the IP in two bytes
			int temperature = atoi(devValue.c_str());

			/*
			tRBUF lcmd;
			memset(&lcmd, 0, sizeof(RBUF));
			lcmd.TEMP.packetlength = sizeof(lcmd.TEMP) - 1;
			lcmd.TEMP.packettype = pTypeTEMP;
			lcmd.TEMP.subtype = sTypeTEMP1;		// THR128/138,THC138
			lcmd.TEMP.id1 = (ulIpAddress>> 24) & 0xFF;
			lcmd.TEMP.id2 = (ulIpAddress>> 16) & 0xFF;
			lcmd.TEMP.tempsign = 1;
			lcmd.TEMP.temperatureh = (temperature >> 8) & 0XFF;
			lcmd.TEMP.temperaturel = temperature & 0xFF;
			lcmd.TEMP.rssi = 12;
			lcmd.TEMP.battery_level = 12;
			
			// THIS is a very important method: it magically feeds the device state to the internal Domoticz bus
			sDecodeRXMessage(this, (const unsigned char *)&lcmd.TEMP); //decode message
			*/

			m_sql.CheckAndHandleNotification(hwId, devId, dunit, dtype, dsubtype, NTYPE_TEMPERATURE, (const float)temperature);

		} else if (qType == UNIT_Sonos_PlayPause) {
			int nValue=0;
			nValue = (const int)atoi(devValue.c_str());

			//Add Lighting log
			m_sql.m_LastSwitchID=devId;
			m_sql.m_LastSwitchRowID=ulIdx;

			szQuery.clear();
			szQuery.str("");

			szQuery << 
				"INSERT INTO LightingLog (DeviceRowID, nValue, sValue) "
				"VALUES ('" << ulIdx <<"', '" << nValue <<"', '" << nValue*15 << "')";
			m_sql.query(szQuery.str());

			// Send as Lighting 2
			tRBUF lcmd;
			memset(&lcmd, 0, sizeof(RBUF));
			lcmd.LIGHTING2.packetlength = sizeof(lcmd.LIGHTING2) - 1;
			lcmd.LIGHTING2.packettype = dtype;
			lcmd.LIGHTING2.subtype = dsubtype;

			lcmd.LIGHTING2.id1 = (ulIpAddress>> 24) & 0xFF;
			lcmd.LIGHTING2.id2 = (ulIpAddress>> 16) & 0xFF;
			lcmd.LIGHTING2.id3 = (ulIpAddress>> 8) & 0xFF;
			lcmd.LIGHTING2.id4 = (ulIpAddress) & 0xFF;

			lcmd.LIGHTING2.unitcode = dunit;
			if (nValue == 0) {
				lcmd.LIGHTING2.cmnd = sonos_sPause;
			} else {
				lcmd.LIGHTING2.cmnd = sonos_sPlay;         
			}

			if (level != -1) {
				lcmd.LIGHTING2.level = level;
			} else {
				if (nValue == 0)
					lcmd.LIGHTING2.level = 0;
				else
					lcmd.LIGHTING2.level = 15;
			}

			lcmd.LIGHTING2.filler = 0;
			lcmd.LIGHTING2.rssi = 12;

			/* THIS is a very important method: it magically feeds the device state to the internal Domoticz bus */ 
			sDecodeRXMessage(this, (const unsigned char *)&lcmd.LIGHTING2); //decode message
		} else if ((qType == UNIT_Sonos_Say) || (qType == UNIT_Sonos_Preset1) || (qType == UNIT_Sonos_Preset2)) {
			int nValue=0;
			nValue = (const int)atoi(devValue.c_str());

			//Add Lighting log
			m_sql.m_LastSwitchID=devId;
			m_sql.m_LastSwitchRowID=ulIdx;

			szQuery.clear();
			szQuery.str("");

			szQuery << 
				"INSERT INTO LightingLog (DeviceRowID, nValue, sValue) "
				"VALUES ('" << ulIdx <<"', '" << nValue <<"', '" << nValue*15 << "')";
			m_sql.query(szQuery.str());

			/* Push button!!! */
			tRBUF lcmd;
			memset(&lcmd, 0, sizeof(RBUF));
			lcmd.LIGHTING2.packetlength = sizeof(lcmd.LIGHTING2) - 1;
			lcmd.LIGHTING2.packettype = dtype;
			lcmd.LIGHTING2.subtype = dsubtype;

			lcmd.LIGHTING2.id1 = (ulIpAddress>> 24) & 0xFF;
			lcmd.LIGHTING2.id2 = (ulIpAddress>> 16) & 0xFF;
			lcmd.LIGHTING2.id3 = (ulIpAddress>> 8) & 0xFF;
			lcmd.LIGHTING2.id4 = (ulIpAddress) & 0xFF;

			lcmd.LIGHTING2.unitcode = dunit;		// UNIT_Sonos_Say

			if (qType == UNIT_Sonos_Say)
        		lcmd.LIGHTING2.cmnd = sonos_sSay;		// New type of command
			else if (qType == UNIT_Sonos_Preset1)
        		lcmd.LIGHTING2.cmnd = sonos_sPreset1;		// New type of command
			else if (qType == UNIT_Sonos_Preset2)
        		lcmd.LIGHTING2.cmnd = sonos_sPreset2;		// New type of command

			int level = PRESET_LEVEL;
			lcmd.LIGHTING2.level = level;			// 0-15
			lcmd.LIGHTING2.filler = 0;
			lcmd.LIGHTING2.rssi = 12;

			/* THIS is a very important method: it magically feeds the device state to the internal Domoticz bus */ 
			sDecodeRXMessage(this, (const unsigned char *)&lcmd.LIGHTING2); //decode message
		}
	}

	return;
}

/*
 *
 */
std::string helperGetUserVariable(const std::string &name)
{
	std::stringstream szQuery;
	std::vector<std::vector<std::string> > result;

	szQuery << "SELECT ID,Name,ValueType,Value FROM UserVariables WHERE (Name==\'" << name << "\')";
	result = m_sql.query(szQuery.str());
	if (result.size()>0) {
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) User variable %s %s %s %s", result[0][0].c_str(), result[0][1].c_str(), result[0][2].c_str(), result[0][3].c_str());
#endif
		return std::string(result[0][3]);
	} else {
		_log.Log(LOG_ERROR,"(Sonos) helperGetUserVariable not found res=%d", result.size()); 
	}
	return std::string("");
}


#if defined WIN32
	//sorry Win32 not supported for now
#elif defined __linux__
    /*+------------------------------------------------------------------------+*/
    /*| CALLBACKS - Cannot be part of the class AFAIK - Somebody?              |*/
    /*+------------------------------------------------------------------------+*/
	/* 
	 * Callback method that executes every time the timeout expires - return true - executes again and again
     */
    static gboolean callbackTimeout(void *data)
    {
		time_t atime=mytime(NULL);
		struct tm ltime;
		localtime_r(&atime,&ltime);
#ifdef _DEBUG_DOUBLE
		_log.Log(LOG_STATUS,"(Sonos) callbackTimeout ->");
#endif

		// Fetch Temperatures
		for(IteratorRenderer it = renderersmap.begin(); it != renderersmap.end(); ++it)
			callbackGetPlay1Temperature(it->second);

		mytime(&thisInstance->m_LastHeartbeat);
#ifdef _DEBUG_DOUBLE
 		_log.Log(LOG_NORM,"(Sonos) Heartbeat working");
#endif
        return true;
    }

	/* 
	 * Change protocols.
     */
	bool helperChangeProtocol(std::string &url, int type)
	{
		// If protocol == x-rincon...: and type not sonos, then change to http:
		return true;
	}

	/* 
	 * Callback method that executes every time the timeout expires - return true - executes again and again
     */
    bool helperListMaps(void)
    { 
		int i=0;

		// Fetch Renderers
		for(IteratorRenderer itr = renderersmap.begin(); itr != renderersmap.end(); ++itr) {
			std::string key = (std::string)itr->first;
			RendererDeviceData *upnprenderer = (RendererDeviceData *)itr->second;
			GUPnPDeviceInfo* info  = GUPNP_DEVICE_INFO(upnprenderer->renderer);
 			_log.Log(LOG_NORM,"(Sonos) Renderer %i id=%s Name=%s", ++i, key.c_str(), upnprenderer->name.c_str());

			// List services available
			GList *services, *s;
	 		int j;
	 		services = gupnp_device_info_list_service_types (info);
	 		for (s = services, j = 0; s; s = s->next, j++) {
	 			char *type = (char *)s->data;	 	
	 			_log.Log(LOG_STATUS,"(Sonos) Renderer %i service [%d]: %s", i, j, type);

	 			g_free (type);
	 		}
	 		g_list_free (services);
		}

		i=0;

		// Fetch Servers
		for(IteratorServer its = serversmap.begin(); its != serversmap.end(); ++its) {
			std::string key = (std::string)its->first;
			ServerDeviceData *upnpserver = (ServerDeviceData *)its->second;
			GUPnPDeviceInfo* info  = GUPNP_DEVICE_INFO(upnpserver->server);
 			_log.Log(LOG_NORM,"(Sonos) Server %i id=%s Name=%s", ++i, key.c_str(), upnpserver->name.c_str());

			// List services available
			GList *services, *s;
	 		int j;
	 		services = gupnp_device_info_list_service_types (info);
	 		for (s = services, j = 0; s; s = s->next, j++) {
	 			char *type = (char *)s->data;	 	
	 			_log.Log(LOG_STATUS,"(Sonos) Server %i service [%d]: %s", i, j, type);

	 			g_free (type);
	 		}
	 		g_list_free (services);
		}

		i=0;

		// Fetch MediaDevices
		for(IteratorMediaDevice itmd = mediadevicesmap.begin(); itmd != mediadevicesmap.end(); ++itmd) {
			std::string key = (std::string)itmd->first;
			std::string sip = (std::string)itmd->second;
 			_log.Log(LOG_NORM,"(Sonos) MediaDevice %i udn=%s id=%s", ++i, key.c_str(), sip.c_str());
		}

        return true;
    }

	/*
	 * Utility function to get play1 temperature
	 */
	static void callbackGetPlay1Temperature(RendererDeviceData *upnpdevice) { 	
	   std::string temperature;

	   if (upnpdevice->type != UPNP_PLAY1) {
			return;
	   }

#ifdef _DEBUG
		_log.Log(LOG_STATUS,"(Sonos) callbackGetPlay1Temperature ->");
#endif

	   if (thisInstance->SonosActionGetPlay1Temperature(upnpdevice, temperature) != true) {
			_log.Log(LOG_ERROR,"(Sonos) Get Play:1 devID %s", upnpdevice->name.c_str() );	   
			thisInstance->UpdateValueEasy(UNIT_Sonos_TempPlay1, upnpdevice->id, NULL, temperature, -1);
	   }

#ifdef _DEBUG
	   _log.Log(LOG_NORM,"(Sonos) Get Play:1 devID %s Temp %s", upnpdevice->name.c_str(), temperature.c_str());	   
#endif
	}

	/*
	 * Helper function to get UPNP IP from Location
	 */
	char *helperRTrim(char *s)
	{
		if (strcmp(s, "") == 0)
			return NULL;

		char* back = s + strlen(s);
		while(isspace(*--back));
		*(back+1) = '\0';
		return s;
	}

	/*
	 * Helper function to get UPNP IP from Location
	 */
	unsigned long helperGetIpFromLocation(const char *szLocation, char *szIP )
	{
		char szTemp[255];
		char *pch;
		int i;

		// Extract IP. Sample: http://play1ip:1400/xml/device_description.xml
		strcpy(szTemp, szLocation);
		pch = strtok (szTemp,":/");
		i=0;
		while (pch != NULL)
		{
			if (i==1) {
				strcpy(szIP, pch);
				break;
			}
			pch = strtok (NULL, ":/");
			i++;
		}

		return inet_addr (szIP);
	}

	/* This is our callback method to process av transport "last change" events         
	 * This function parses the "TransportState" information from the XML user data, 
	 * looks up the renderer from our GHashTable using the UDN
	 */
	static void callbackLastChange( GUPnPServiceProxy *av_transport,
		const char        *variable_name,
		GValue            *value,
		gpointer           user_data)
	{
		const char					*last_change_xml;
		char						*state_name = NULL;
		char						*metadata = NULL;
		char						*duration;
		GError						*error;
		const char					*udn;
		static GUPnPDeviceProxy*	renderer;
		GUPnPDIDLLiteParser			*parser;
		unsigned long				longIP;
		bool						success;
		int							new_state;

#ifdef _DEBUG_CHANGE
//		_log.Log(LOG_NORM,"(Sonos) Change1 [%s]", variable_name);
#endif
		// The data returned is an XML document describing the transport state
		last_change_xml = g_value_get_string (value);
		parser = gupnp_didl_lite_parser_new ();
		error = NULL;
		state_name = NULL;
		metadata = NULL;
		duration = NULL;
		success = gupnp_last_change_parser_parse_last_change(lc_parser,
				0,
				last_change_xml,
				&error,
				"TransportState", G_TYPE_STRING, &state_name,
				"CurrentTrackMetaData", G_TYPE_STRING, &metadata,
				NULL);
		if (!success) {
			if (error) {
				_log.Log(LOG_ERROR, "(Sonos) Change2 error %d %s", error, error->message);
				g_error_free (error);
			}

			// Free resources
			if (state_name != NULL) 
				g_free (state_name);
			if (metadata != NULL) 
				g_free (metadata);		
			if (parser != NULL) 
				g_object_unref (parser);
			return;
		}

		std::string brand, model, name;
		bool isFirstTime = false;

		/* Look up the UDN in our hashtable to get the renderer */
		udn = gupnp_service_info_get_udn(GUPNP_SERVICE_INFO (av_transport));
		IteratorMediaDevice itrnd = mediadevicesmap.find(std::string(udn));
		std::string deviceID = (std::string)itrnd->second;

		/* Look up the renderer device data from IP */
	    RendererDeviceData *upnpdevice;
		IteratorRenderer it = renderersmap.find(deviceID);
		upnpdevice = (RendererDeviceData *)it->second;

		/* Only first time - Get extra device information to store in device list */
		if (upnpdevice->type == UPNP_NULL) {
			thisInstance->SonosGetDeviceData(upnpdevice, brand, model, name);
			_log.Log(LOG_NORM,"(Sonos) %s(%s) [%s] [%s] [%s] [%s] [%s]", 
				state_name, upnpdevice->name.c_str(), deviceID.c_str(), udn, brand.c_str(), model.c_str(), name.c_str());
			isFirstTime = true;
		}

		/* Possible states: STOPPED PLAYING PAUSED TRANSITIONING NO_MEDIA_PRESENT */

		/* XBMC/Kodi results in a (null) state_name sometimes!!! */
		bool break_change = false;
		if (state_name == NULL) {
			_log.Log(LOG_ERROR,"(Sonos) (NULL State) (%s)!!", upnpdevice->name.c_str());
			new_state = UPNP_STATE_PLAYING;
//			break_change = true;
		} else {
			if (strcmp(state_name, "TRANSITIONING") == 0) {
				new_state = UPNP_STATE_TRANSITIONING;
#ifdef _DEBUG_CHANGE
				if (new_state != upnpdevice->prev_state)
					_log.Log(LOG_NORM,"(Sonos) %s(%s) ignored", state_name, upnpdevice->name.c_str() );
#endif
				break_change = true;
			} else if (strcmp(state_name, "NO_MEDIA_PRESENT") == 0) {
				new_state = UPNP_STATE_NO_MEDIA_PRESENT;
#ifdef _DEBUG_CHANGE
				_log.Log(LOG_NORM,"(Sonos) %s(%s) ignored", state_name, upnpdevice->name.c_str() );
#endif
				break_change = true;
			} else if (strcmp(state_name, "STOPPED") == 0) {
				new_state = UPNP_STATE_STOPPED;
			} else if (strcmp(state_name, "PLAYING") == 0) {
				new_state = UPNP_STATE_PLAYING;			
			} else if (strcmp(state_name, "PAUSED") == 0) {
				new_state = UPNP_STATE_PAUSED;
			}
		}

		/* Return */
		if (break_change) {
			// Free resources
			if (state_name != NULL) 
				g_free (state_name);
			if (metadata != NULL) 
				g_free (metadata);		
			if (parser != NULL) 
				g_object_unref (parser);
			return;
		}

		/* Get current track info from metadata - Only when starting to play or playing something different */
		if (new_state == UPNP_STATE_PLAYING) {
			if (metadata != NULL) {
				int meta_length = strlen(metadata);
				if (meta_length > 5) {
					GError     *lc_error;

					lc_error = NULL;
#ifdef _DEBUG
					//				_log.Log(LOG_NORM,"(Sonos) Change7 Metadata len: %d", meta_length);
#endif
					g_signal_connect (parser, "object-available", G_CALLBACK (callbackGetTrackInfo), (gpointer) upnpdevice);
					gupnp_didl_lite_parser_parse_didl (parser, metadata, &lc_error);
					if (lc_error) {
						_log.Log(LOG_ERROR,"(Sonos) Change7 Parse DIDL %s\n", lc_error->message);
						g_error_free (lc_error);
					}
				} else if (meta_length != 0) {
					_log.Log(LOG_NORM,"(Sonos) Change8 Metadata too short %d \'%s\' %x", meta_length, metadata, metadata[0]);
				}
			} else {
				_log.Log(LOG_NORM,"(Sonos) PLAYING but no metadata");
			}

			/* GetPositionInfo */
			std::string currenturi;
			thisInstance->SonosActionGetPositionInfo(upnpdevice, currenturi);

#ifdef _DEBUG_CHANGE
			_log.Log(LOG_NORM,"(Sonos) Change10 GetPositionInfo %s URI=%s", upnpdevice->name.c_str(), currenturi.c_str() );
#endif		
		}
				
		/* Get volume level */
		int level = thisInstance->SonosActionGetVolume(upnpdevice);

		/* The first time, add a Play/Pause/Volume device for each Sonos speaker */
		/* Later, adjust state and volume level                                  */
		if (new_state == UPNP_STATE_PLAYING)
			thisInstance->UpdateValueEasy(UNIT_Sonos_PlayPause, deviceID, name, "1", level);
		else
			thisInstance->UpdateValueEasy(UNIT_Sonos_PlayPause, deviceID, name, "0", level);

		/* Only first time - create the extra domoticz devices - nothing to change afterwards */
		/* @@@Future Change ONLY NEEDED IF DEVICE IS NOT ALREADY CREATED IN DATABASE!! */
		if (isFirstTime) {
			/* Add a Say pushbutton for each Sonos speaker */
//				if ((upnpdevice->type == UPNP_SONOS) || (upnpdevice->type == UPNP_PLAY1))
				thisInstance->UpdateValueEasy(UNIT_Sonos_Say, deviceID, name, "0", -1);

			/* Add Presets pushbuttons for each Sonos speaker */
			thisInstance->UpdateValueEasy(UNIT_Sonos_Preset1, deviceID, name, "0", -1);
			thisInstance->UpdateValueEasy(UNIT_Sonos_Preset2, deviceID, name, "0", -1);

			/* Add a temperature sensor for each Play:1 */
			if (upnpdevice->type == UPNP_PLAY1) {
				std::string temperature;

				// Create/update the Temp sensor
				if (thisInstance->SonosActionGetPlay1Temperature(upnpdevice, temperature) != false)	
					thisInstance->UpdateValueEasy(UNIT_Sonos_TempPlay1, deviceID, name, temperature, -1);
			}
		}

		/* If STOPPED and restore state pending*/
		if (new_state == UPNP_STATE_STOPPED) {
			if (upnpdevice->restore_state) {
				ServerDeviceData *upnpserverdevice = NULL;
				IteratorServer its = serversmap.find(upnpdevice->id);
				if (its != serversmap.end()) {
					upnpserverdevice = (ServerDeviceData *)its->second;

					std::string sURLQueue;
					thisInstance->SonosActionLoadQueue(upnpserverdevice, sURLQueue);
					_log.Log(LOG_NORM,"(Sonos) Change11 RestoreState Queue %s", upnpdevice->name.c_str());
				}
				upnpdevice->restore_state = false;
			}
		}

		// Store previous state
		upnpdevice->prev_state = new_state;

		// Free resources
#ifdef _DEBUG_CHANGE
//		_log.Log(LOG_NORM, "(Sonos) Change12 end S[%x] M[%x] P[%x]", state_name, metadata, parser);
#endif
		if (state_name != NULL) 
			g_free (state_name);
		if (metadata != NULL) 
			g_free (metadata);		
		if (parser != NULL) 
			g_object_unref (parser);
	}


    /* 
	 * Callback method to handle new devices which have been discovered. 
     */
    static void callbackDeviceDiscovered(GUPnPControlPoint *cp, GUPnPDeviceProxy *deviceproxy)
    {
        GUPnPServiceProxy *av_transport;
        GUPnPDeviceInfo* info  = GUPNP_DEVICE_INFO(deviceproxy);
		bool isRenderer = false;

		/* Only allow MediaRenderers and MediaServers */
		char *device_type = NULL;
		device_type = (char *)gupnp_device_info_get_device_type (info);
		if (strcmp(device_type, UPNP_MEDIA_RENDERER) == 0) 
			isRenderer = true;
		else if (strcmp(device_type, UPNP_MEDIA_SERVER) == 0)
			isRenderer = false;
		else
			return;
		
		/* Get UDN */
		std::string udn(gupnp_device_info_get_udn(info));

#ifdef _DEBUG
		_log.Log(LOG_STATUS,"(Sonos) Discovered %s Model[%s] Type[%s]", 
			udn.c_str(), gupnp_device_info_get_model_name (info), gupnp_device_info_get_device_type (info) );
#endif

		/* Extract IP and add device ip to list of current device ips */
		char szIP[30];
		unsigned long longIP = helperGetIpFromLocation(gupnp_device_info_get_location (info), szIP);
		std::stringstream ss;
		ss << std::uppercase << std::setfill('0') << std::setw(8) << std::hex <<  longIP;
		std::string deviceID(ss.str());

		/* Save all info in UPnP renderer structure */
		/* If device already saved, retrieve it - else create a new one */
		if (isRenderer) {
			RendererDeviceData *upnpdevice;

			IteratorRenderer it = renderersmap.find(deviceID);
			if (it != renderersmap.end()) {
				upnpdevice = (RendererDeviceData *)it->second;
				_log.Log(LOG_NORM,"(Sonos) Discovered %s duplicate renderer!", upnpdevice->name.c_str());
			} else {
				upnpdevice = new(RendererDeviceData);
				upnpdevice->ip = longIP;
				upnpdevice->type = UPNP_NULL;			// Not already identified
				upnpdevice->prev_state = UPNP_STATE_STOPPED;
				upnpdevice->restore_state = false;
				upnpdevice->level = 0;
				upnpdevice->id = deviceID;
				upnpdevice->udn = udn;
				upnpdevice->name = "";

				/* Get AVTransport service for device */
				upnpdevice->av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(info, UPNP_AV_TRANSPORT));
				upnpdevice->renderer = deviceproxy;

				/* Add device to list of current devices */
				renderersmap.insert(RenderersMap::value_type(std::string(upnpdevice->id), upnpdevice));
				mediadevicesmap.insert(MediaDevicesMap::value_type(std::string(upnpdevice->udn), upnpdevice->id));

				/* Add "LastChange" to the list of states we want to be notified about and turn on event subscription */
				gupnp_service_proxy_add_notify( upnpdevice->av_transport,
											"LastChange", G_TYPE_STRING, callbackLastChange,
											NULL);
				gupnp_service_proxy_set_subscribed (upnpdevice->av_transport, TRUE);
			}
		} else {
			ServerDeviceData *upnpdevice;

			IteratorServer it = serversmap.find(deviceID);
			if (it != serversmap.end()) {
				upnpdevice = (ServerDeviceData *)it->second;
				_log.Log(LOG_STATUS,"(Sonos) Discovered %s duplicate server!", upnpdevice->name.c_str());
			} else {
				upnpdevice = new(ServerDeviceData);
				upnpdevice->ip = longIP;
				upnpdevice->type = UPNP_NULL;			// Not already identified
				upnpdevice->id = deviceID;
				upnpdevice->udn = udn;
				upnpdevice->name = std::string(gupnp_device_info_get_model_name(info));

				/* Get AVTransport service for device */
				upnpdevice->av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(info, UPNP_AV_TRANSPORT));
				upnpdevice->server = deviceproxy;

				/* Add device to list of current devices */
				serversmap.insert(ServersMap::value_type(std::string(upnpdevice->id), upnpdevice));
			}
		}

		return;
    }

	/*
	 * SonosGetDeviceData
	 * Get extra information to create the domoticz devices 
	 */
	bool CSonosPlugin::SonosGetDeviceData(RendererDeviceData *upnpdevice, std::string& brand, std::string& model, std::string& name ) {
		GUPnPDeviceProxy	*renderer;

		renderer = upnpdevice->renderer;

		// Extract Model. Sample: Sonos PLAY:1
		//                Sample: XBMC Media Center
		std::stringstream  stream(gupnp_device_info_get_model_name(GUPNP_DEVICE_INFO(renderer)));
//		_log.Log(LOG_NORM,"(Sonos) GetDeviceData model-name \'%s\' IP %8X", stream.str().c_str(), upnpdevice->ip);

		stream >> brand;
//		_log.Log(LOG_NORM,"(Sonos) GetDeviceData brand \'%s\'", brand.c_str() );
		if (brand.compare(0,5, "Sonos") == 0) {
			upnpdevice->type = UPNP_SONOS;
		} else if (brand.compare(0,4, "XBMC") == 0) {
			upnpdevice->type = UPNP_XBMC;
		}

		// Rest is the model name
		stream >> model;
//		_log.Log(LOG_NORM,"(Sonos) GetDeviceData model \'%s\'", model.c_str());

		// Extract Name. Sample: Family Office - Sonos ZP100 Media Renderer
		//               Sample: XBMC (pc)
		std::string friendly_name(gupnp_device_info_get_friendly_name(GUPNP_DEVICE_INFO(renderer)));
		int fr_len = friendly_name.length();
//		_log.Log(LOG_NORM,"(Sonos) GetDeviceData friendly-name \'%s\'", friendly_name.c_str());

		if (upnpdevice->type == UPNP_SONOS) {
			std::size_t pos = friendly_name.find("-");
			if (pos != std::string::npos) {
				if (pos > 1)
					name = std::string(friendly_name, 0, pos-1);
				else
					name = friendly_name;
			} else {
				name = friendly_name;
			}

#ifdef PLAY1_GET_TEMPERATURE
			if (model.compare(0,6, "PLAY:1") == 0)
				upnpdevice->type = UPNP_PLAY1;
#endif
		} else {
			name = friendly_name;

			if (upnpdevice->type == UPNP_NULL)
				upnpdevice->type = UPNP_OTHERS;
		}

		// Store friendly name for each device
		if (upnpdevice->name == "") {
			upnpdevice->name = name;
		}

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) GetDeviceData brand \'%s\' model \'%s\' name \'%s\'", brand.c_str(), model.c_str(), name.c_str());
#endif
		return true;
	}

	/* This is our callback method to handle devices
	 * which are removed from the network
	 */
	static void callbackDeviceUnavailable(GUPnPControlPoint *cp, GUPnPDeviceProxy *deviceproxy)
	{
        GUPnPDeviceInfo* info  = GUPNP_DEVICE_INFO(deviceproxy);
		RendererDeviceData *upnprenderer;
		ServerDeviceData *upnpserver;
		IteratorRenderer itr;
		IteratorServer its;
		bool isRenderer = false;
		bool isLastMediaDevice = false;

		/* Only allow MediaRenderers and MediaServers */
		char *device_type = NULL;
		device_type = (char *)gupnp_device_info_get_device_type (info);
		if (strcmp(device_type, UPNP_MEDIA_RENDERER) == 0) {
			isRenderer = true;
		} else if (strcmp(device_type, UPNP_MEDIA_SERVER) == 0) {
			isRenderer = false;
		} else
			return;

		/* Extract IP and add device ip to list of current device ips */
		char szDeviceID[30];
		unsigned long longIP = helperGetIpFromLocation(gupnp_device_info_get_location (info), szDeviceID);
		std::stringstream ss;
		ss << std::uppercase << std::setfill('0') << std::setw(8) << std::hex <<  longIP;
		std::string deviceID(ss.str());

		/* Erase Renderer */
		if (isRenderer == true) {
			itr = renderersmap.find(deviceID);
			if (itr != renderersmap.end()) {
				upnprenderer = (RendererDeviceData *)itr->second;
#ifdef _DEBUG
				if (upnprenderer->name != "")
					_log.Log(LOG_STATUS,"(Sonos) Renderer Unavailable %s", upnprenderer->name.c_str());
				else
					_log.Log(LOG_STATUS,"(Sonos) Renderer Unavailable name-unavailable %s", upnprenderer->id.c_str());
#endif

				/* Remove device from list of current devices */
				renderersmap.erase(deviceID);
			}

			its = serversmap.find(deviceID);
			if (its == serversmap.end())
				isLastMediaDevice = true;
		} else {
			/* Erase Server */
			its = serversmap.find(deviceID);
			if (its != serversmap.end()) {
				upnpserver = (ServerDeviceData *)its->second;
#ifdef _DEBUG
				if (upnpserver->name != "")
					_log.Log(LOG_STATUS,"(Sonos) Server Unavailable %s", upnpserver->name.c_str());
				else
					_log.Log(LOG_STATUS,"(Sonos) Server Unavailable name-unavailable %s", upnpserver->id.c_str());
#endif

				/* Remove device from list of current devices */
				serversmap.erase(deviceID);
			}

			itr = renderersmap.find(deviceID);
			if (itr == renderersmap.end()) 
				isLastMediaDevice = true;
		}

		if (isLastMediaDevice) {
			_log.Log(LOG_STATUS,"(Sonos) Server/Renderer Unavailable %s", deviceID.c_str());
			mediadevicesmap.erase(std::string(gupnp_device_info_get_udn(info)));
		}
	}

	/* Callback method to print out key details about
     * the current track. This comes as a "DIDL" object
     */
	static void callbackGetTrackInfo(GUPnPDIDLLiteParser *parser,
		GUPnPDIDLLiteObject *object,
		gpointer             pointer) 
	{
		SoupURI				*aa_uri;
		SoupURI				*url_base;
		RendererDeviceData	*upnpdevice;

		upnpdevice = (RendererDeviceData *)pointer;

#ifdef _DEBUG
//		_log.Log(LOG_STATUS,"(Sonos) Track Info ->");
#endif

		/* Get the URL base of the renderer */
		url_base = (SoupURI *) gupnp_device_info_get_url_base(GUPNP_DEVICE_INFO((GUPnPDeviceProxy *)upnpdevice->renderer));

		/* to link with libsoup add soup-2.4 to cmake!!! */
		/* Sometimes this errror message is received: */
		/* (process:24938): libsoup-CRITICAL **: soup_uri_new_with_base: assertion 'uri_string != NULL' failed ¿¿?? - everything continues working */		
		aa_uri = soup_uri_new_with_base(url_base, gupnp_didl_lite_object_get_album_art(object));
		if (aa_uri != NULL) {
			char *artist, *title, *album, *album_art;
			std::stringstream trackinfo;

			trackinfo << "[";

			title = (char*)gupnp_didl_lite_object_get_title(object);
			if (title != NULL)
				trackinfo << title << "] ";

			artist = (char*)gupnp_didl_lite_object_get_creator(object);
			if (artist != NULL)
				trackinfo << "[" << artist << "] ";

			album = (char*)gupnp_didl_lite_object_get_album(object);
//			if (album != NULL) trackinfo << "[" << album << "] ";

			album_art = (char*)soup_uri_to_string (aa_uri, FALSE);
//			if (album_art != NULL) trackinfo << "]" << album_art << "]";

			_log.Log(LOG_NORM, "(Sonos) Track: %s", trackinfo.str().c_str());
		}

		return;
	}

    /*+------------------------------------------------------------------------+*/
    /*| UPnP AVTransport Actions                                               |*/
    /*+------------------------------------------------------------------------+*/

	/*
	 * UPnP Action method to pause AVT Transport on device
	 */
	bool CSonosPlugin::SonosActionPause(RendererDeviceData *upnpdevice) {
		 GError *error = NULL;
		 gboolean success;

		 /* Send action */
		 success = gupnp_service_proxy_send_action (upnpdevice->av_transport, "Pause", &error,
													"InstanceID", G_TYPE_UINT, 0, NULL,
													NULL);
		if (!success) {
			if (error) {
				_log.Log(LOG_ERROR,"(Sonos) Pause error %s", error->message);
				g_error_free (error);
			}
			return false;
		}

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Pause success!");
#endif
		return true;
	}

	/*
	 * UPnP AV Transport Action method to play next track on device
	 */
	bool CSonosPlugin::SonosActionNext(RendererDeviceData *upnpdevice) {
		 GError *error = NULL;
		 gboolean success;

		 /* Send action */
		 success = gupnp_service_proxy_send_action (upnpdevice->av_transport, "Next", &error,
													"InstanceID", G_TYPE_UINT, 0, NULL,
													NULL);
		if (!success) {
			if (error) {
				_log.Log(LOG_ERROR,"(Sonos) Next error %s", error->message);
				g_error_free (error);
				return false;
			}
		}

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Next success!");
#endif
		return true;
	}

	/*
	 * UPnP AV Transport Action method to play previous track on device
	 */
	bool CSonosPlugin::SonosActionPrevious(RendererDeviceData *upnpdevice) {		 
		 GError *error = NULL;
		 gboolean success;

		 /* Send action */
		 success = gupnp_service_proxy_send_action (upnpdevice->av_transport, "Previous", &error,
													"InstanceID", G_TYPE_UINT, 0, NULL,
													NULL);
		if (!success) {
			if (error) {
				_log.Log(LOG_ERROR,"(Sonos) Prev error %s", error->message);
				g_error_free (error);
				return false;
			}
		}

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Prev success!");
#endif
		return true;
	}

	/*
	 * UPnP Action method to play AVT Transport on device
	 */
	bool CSonosPlugin::SonosActionPlay(RendererDeviceData *upnpdevice) {
		 GError *error = NULL;
		 gboolean success;

		 /* Send action */
		 success = gupnp_service_proxy_send_action (upnpdevice->av_transport, "Play", &error,
													"InstanceID", G_TYPE_UINT, 0,
													"Speed", G_TYPE_UINT, 1,
													NULL,
													NULL);
		if (!success) {
			_log.Log(LOG_ERROR,"(Sonos) Play error %s", error->message);
		    g_error_free (error);
			return false;
		}
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Play success!");
#endif
		return true;
	}

	/*
	 * UPnP AV Action method to set URI to play on Media Renderer
	 */
	bool CSonosPlugin::SonosActionSetURI(RendererDeviceData *upnpdevice, const std::string& uri ) {
		GError *error;
		gboolean success;
		std::string metadata;

		/* Build metadata, this is very hacky but is good to show raw data */
		std::string metadata1("&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot;"
			" xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot;");
		std::string metadata2(" xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot;");
		std::string metadata3(" xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;"
			"&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;"
			"&lt;res protocolInfo=&quot;http-get:*:audio/mp3:*&quot; &gt;");
		std::string metadata4("&lt;/res&gt;"
			"&lt;dc:title&gt;Domoticz%20TTS&lt;/dc:title&gt;"
			"&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;"
			"&lt;/item&gt;"
			"&lt;/DIDL-Lite&gt;");

		/* metadata is metadata1 + uri + metadata2 */
		std::stringstream temp;
		if (upnpdevice->type == UPNP_SONOS) {
			temp << metadata1 << metadata2 << metadata3 << uri << metadata4;
		} else {
//			temp << metadata1 << metadata3 << uri << metadata4;
			temp << uri;
		}
		metadata = temp.str();

		 /* Send action */
		success = gupnp_service_proxy_send_action( upnpdevice->av_transport, "SetAVTransportURI", &error,
			"InstanceID", G_TYPE_UINT, 0,
			"CurrentURI", G_TYPE_STRING, uri.c_str(),
			"CurrentURIMetaData", G_TYPE_STRING, metadata.c_str(), 
			NULL,
			NULL);
		if (!success) {
			if (error) {
				_log.Log(LOG_ERROR,"(Sonos) SetURI Error %d. Msg: %s", error->code, error->message);
				g_error_free (error);
				return false;
			}
		}
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) SetURI Ok playing %s", uri.c_str());
#endif
		return true;
	}

	/*
	 * UPnP AV Action method to set next URI to play on Media Renderer
	 * Not many UPnP devices support that command...
	 */
	bool CSonosPlugin::SonosActionSetNextURI(RendererDeviceData *upnpdevice, const std::string& uri ) {
		GError *error;
		gboolean success;
		std::string metadata;

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) SetNextURI [%s]", uri.c_str());
#endif
	
		/* Build metadata, this is very hacky but is good to show raw data */
		std::string metadata1("&lt;DIDL-Lite xmlns:dc=&quot;http://purl.org/dc/elements/1.1/&quot;"
			" xmlns:upnp=&quot;urn:schemas-upnp-org:metadata-1-0/upnp/&quot;");
		std::string metadata2(" xmlns:r=&quot;urn:schemas-rinconnetworks-com:metadata-1-0/&quot;");
		std::string metadata3(" xmlns=&quot;urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/&quot;&gt;"
			"&lt;item id=&quot;-1&quot; parentID=&quot;-1&quot; restricted=&quot;true&quot;&gt;"
			"&lt;res protocolInfo=&quot;http-get:*:audio/mp3:*&quot; &gt;");
		std::string metadata4("&lt;/res&gt;"
			"&lt;dc:title&gt;Domoticz%20TTS&lt;/dc:title&gt;"
			"&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;"
			"&lt;/item&gt;"
			"&lt;/DIDL-Lite&gt;");

		/* metadata is metadata1 + uri + metadata2 */
		std::stringstream temp;
		if (upnpdevice->type == UPNP_SONOS) {
			temp << metadata1 << metadata2 << metadata3 << uri << metadata2;
		} else {
			temp << metadata1 << metadata3 << uri << metadata2;
		}
		metadata = temp.str();

		 /* Send action */
		success = gupnp_service_proxy_send_action( upnpdevice->av_transport, "SetNextAVTransportURI", &error,
			"InstanceID", G_TYPE_UINT, 0,
			"NextURI", G_TYPE_STRING, uri.c_str(),
			"NextURIMetaData", G_TYPE_STRING, metadata.c_str(), 
			NULL,
			NULL);
		if (!success) {
			if (error) {
				_log.Log(LOG_ERROR,"(Sonos) SetNextURI Error %d. Msg: %s", error->code, error->message);
				g_error_free (error);
				return false;
			}
		}
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) SetNextURI Ok playing %s", uri.c_str());
#endif
		return true;
	}

	/*
	 * UPnP AV Transport Action method to leave group
	 *
	 * For linked zones, the commands have to be sent to the coordinator.  
	 * BecomeCoordinatorOfStandaloneGroup is ~ "Leave Group".  
	 * To join a group set AVTransportURI to x-rincon:<udn>
	 */
	bool CSonosPlugin::SonosActionLeaveGroup(RendererDeviceData *upnpdevice) {		 
		 GError *error = NULL;
		 gboolean success;

		 /* Send action */
		 success = gupnp_service_proxy_send_action (upnpdevice->av_transport, "BecomeCoordinatorOfStandaloneGroup", &error,
													"InstanceID", G_TYPE_UINT, 0, NULL,
													NULL);
		if (!success) {
			if (error) {
				_log.Log(LOG_ERROR,"(Sonos) Leave group error %s", error->message);
				g_error_free (error);
				return false;
			}
		}

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Leave group success!");
#endif
		return true;
	}


	/*
	 * UPnP AV Action method to Get Transport Info for device
	 */
	bool CSonosPlugin::SonosActionGetTransportInfo(RendererDeviceData *upnpdevice, std::string& state) {
		 GError *error = NULL;
		 gboolean success;

		 /* Send action */
		 char *CurrentTransportState;
		 char *CurrentTransportStatus;
		 char *CurrentSpeed;
		 success = gupnp_service_proxy_send_action (upnpdevice->av_transport, "GetTransportInfo", &error,
													"InstanceID", G_TYPE_UINT, 0, 
													NULL,
													"CurrentTransportState", G_TYPE_STRING, &CurrentTransportState,
													"CurrentTransportStatus", G_TYPE_STRING, &CurrentTransportStatus,
													"CurrentSpeed", G_TYPE_STRING, &CurrentSpeed,
													NULL);
		if (!success) {
			_log.Log(LOG_ERROR,"(Sonos) GetTransportInfo error %s", error->message);
			return false;
		}

		std::string currentstate(CurrentTransportState);
		state = currentstate;

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) GetTransportInfo St[%s] Stu[%s] Sp[%s]", 
			CurrentTransportState, CurrentTransportStatus, CurrentSpeed );
#endif
		return true;
	}

	/*
	 * UPnP AV Action method to Get Position Info for device
	 */
	bool CSonosPlugin::SonosActionGetPositionInfo(RendererDeviceData *upnpdevice, std::string& currenturi) {
		 GError *error = NULL;
		 gboolean success;

		 /* Send action */
		 char *CurrentTrack;
		 char *CurrentTrackDuration;
		 char *CurrentTrackMetaData;
		 char *CurrentTrackURI;
		 int RelativeTimePosition, AbsoluteTimePosition, RelativeCounterPosition, AbsoluteCounterPosition;
		 success = gupnp_service_proxy_send_action (upnpdevice->av_transport, "GetPositionInfo", &error,
													"InstanceID", G_TYPE_UINT, 0, 
													NULL,
													"Track", G_TYPE_STRING, &CurrentTrack,
													"TrackDuration", G_TYPE_STRING, &CurrentTrackDuration,
													"TrackMetaData", G_TYPE_STRING, &CurrentTrackMetaData,
													"TrackURI", G_TYPE_STRING, &CurrentTrackURI,
													"RelTime", G_TYPE_STRING, &RelativeTimePosition,
													"AbsTime", G_TYPE_STRING, &AbsoluteTimePosition,
													"RelCount", G_TYPE_UINT, &RelativeCounterPosition,
													"AbsCount", G_TYPE_UINT, &AbsoluteCounterPosition,
													NULL);
		if (!success) {
			_log.Log(LOG_ERROR,"(Sonos) GetPositionInfo error %s", error->message);
			return false;
		}

		if (strcmp(CurrentTrackMetaData, "NOT_IMPLEMENTED") == 0) {
			if (strncmp(CurrentTrackURI, "x-rincon:", 9) == 0) {
				//  this means that this zone is a slave to the master or group coordinator with the given id RINCON_xxxxxxxxx
				std::string currentrackuri(CurrentTrackURI);
				std::string zone_coordinator = currentrackuri.substr(9, std::string::npos);
				_log.Log(LOG_NORM,"(Sonos) Track - same as zone coordinator [%s]", zone_coordinator.c_str());
				upnpdevice->coordinator = zone_coordinator;
			}
		} else {
			upnpdevice->coordinator = "";
		}

		/* Get protocol */
		/* On Sonos AbsoluteTimePosition is always - NOT_IMPLEMENTED */
		if (strncmp(CurrentTrackURI, "x-sonosapi-stream:", 18) == 0) {
			upnpdevice->protocol = SONOS_PROTOCOL_LINEIN;
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Info LineIn URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
			CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		} else if (strncmp(CurrentTrackURI, "x-sonos-http:", 9) == 0) {
			upnpdevice->protocol = SONOS_PROTOCOL_FILE;
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Info File/Queue URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
			CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		} else if (strncmp(CurrentTrackURI, "x-rincon-mp3radio:", 18) == 0) {
			upnpdevice->protocol = SONOS_PROTOCOL_RADIO;
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Info Radio URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
			CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		} else {
			upnpdevice->protocol = SONOS_PROTOCOL_NONE;
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) GetPositionInfo URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
			CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		}

		std::string trackuri(CurrentTrackURI);
		currenturi = trackuri;

		return true;
	}

    /*+------------------------------------------------------------------------+*/
    /*| RENDERING CONTROL methods                                              |*/
    /*+------------------------------------------------------------------------+*/

	/*
	 * UPnP Action method to get volume from device
	 */
	int CSonosPlugin::SonosActionGetVolume(RendererDeviceData *upnpdevice) {
		 /* Get rendering control service for device */
		 GUPnPServiceProxy *rendering_control;
		 rendering_control = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
                       UPNP_RENDERING_CONTROL));
		 if (rendering_control == 0) {
			_log.Log(LOG_ERROR,"(Sonos) GetVolume error getting rendering control for device %s", upnpdevice->name.c_str());
			return -1;
		 }

		 /* GetVolume */
		 bool success;
		 GError *error = NULL;
		 int volume = 0;
		 success = (bool)gupnp_service_proxy_send_action (rendering_control, "GetVolume", &error,
													"InstanceID", G_TYPE_UINT, 0,
													"Channel", G_TYPE_STRING, "Master",
													NULL,
													"CurrentVolume", G_TYPE_UINT, &volume,
													NULL);
		if (!success) {
			_log.Log(LOG_ERROR,"(Sonos) GetVolume error %s", error->message);
		    g_error_free (error);
			return -1;
		}
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) GetVolume Current volume=%d for %s", volume, upnpdevice->name.c_str());
#endif
		return(volume);
	}

	/*
	 * UPnP Action method to set volume on device
	 */
	bool CSonosPlugin::SonosActionSetVolume(RendererDeviceData *upnpdevice, int level) {
		/* Get rendering control service for device */
		GUPnPServiceProxy *rendering_control;
		rendering_control = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
			UPNP_RENDERING_CONTROL));
		if (rendering_control == 0) {
			_log.Log(LOG_ERROR,"(Sonos) SetVolume error getting rendering control for device %s", 
				upnpdevice->name.c_str());
			return false;
		}

		/* SetVolume */
		GError *error = NULL;
		bool success;
		int volume = (int)((level*100) / 15);
		success = (bool)gupnp_service_proxy_send_action (rendering_control, "SetVolume", &error,
			"InstanceID", G_TYPE_UINT, 0,
			"Channel", G_TYPE_STRING, "Master",													
			"DesiredVolume", G_TYPE_UINT, volume,
			NULL,
			NULL);
		if (!success) {
			_log.Log(LOG_ERROR,"(Sonos) SetVolume error %s", error->message);
			g_error_free (error);
			return false;
		}
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) SetVolume %s %d success!", upnpdevice->name.c_str(), volume);
#endif
		return true;
	}

    /*+------------------------------------------------------------------------+*/
    /*| CONTENT DIRECTORY methods                                              |*/
    /*+------------------------------------------------------------------------+*/
	/*
	 * UPnP Action method to browse Content Directory
	 */
	bool CSonosPlugin::SonosActionLoadQueue(ServerDeviceData *upnpdevice, std::string& sURL) {
		GUPnPServiceProxy *content_directory;
		GUPnPDeviceInfo *info = GUPNP_DEVICE_INFO(upnpdevice->server);
		 
 		// List services available
		// Typically for Sonos:
		// (Sonos) LoadQueue service [0]: urn:schemas-upnp-org:service:ConnectionManager:1
		// (Sonos) LoadQueue service [1]: urn:schemas-upnp-org:service:ContentDirectory:1
//		GList *services, *s;
//	 	int i;
//	 	services = gupnp_device_info_list_service_types (info);
//	 	for (s = services, i = 0; s; s = s->next, i++) {
//	 		char *type = (char *)s->data;	 	
//	 		_log.Log(LOG_STATUS,"(Sonos) LoadQueue service [%d]: %s", i, type);
//	 		g_free (type);
//		}
//	 	g_list_free (services);

		/* Get content directory service for device */
		GUPnPServiceInfo *proxy = gupnp_device_info_get_service(info, UPNP_CONTENT_DIRECTORY);
		if (proxy == NULL) {
			_log.Log(LOG_ERROR, "(Sonos) LoadQueue service info %x", proxy);
			return false;
		}

		content_directory = GUPNP_SERVICE_PROXY (proxy);
		if (content_directory == NULL) {
			_log.Log(LOG_ERROR, "(Sonos) LoadQueue error getting content directory for device %s", 
				upnpdevice->id.c_str());
			return false;
		}

		/* Browse - Get a count of what's in the queue */
		/* look for the childCount attribute in the returned XML */
		bool success;
		GError *error = NULL;
		char *result;
		int NumberReturned, TotalMatches, UpdateID;
		success = (bool)gupnp_service_proxy_send_action (content_directory, "Browse", &error,
													"ObjectID", G_TYPE_STRING, "Q:",
													"BrowseFlag", G_TYPE_STRING, "BrowseDirectChildren",
													"Filter", G_TYPE_STRING, "*",
													"StartingIndex", G_TYPE_UINT, 0,
													"RequestedCount", G_TYPE_UINT, 1,
													"SortCriteria", G_TYPE_STRING, NULL,
													NULL,
													"NumberReturned", G_TYPE_UINT, &NumberReturned,
													"TotalMatches", G_TYPE_UINT, &TotalMatches,
													"UpdateID", G_TYPE_UINT, &UpdateID,
													"Result", G_TYPE_STRING, &result,
													NULL);
		if (!success) {
			_log.Log(LOG_ERROR,"(Sonos) LoadQueue error %d %s", error, error->message);
		    g_error_free (error);
			return false;
		}
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) LoadQueue %s NR[%d] TM[%d] UID[%d] %s", upnpdevice->id.c_str(),
			NumberReturned, TotalMatches, UpdateID, result );
#endif

		/* Browse - Get what's in the queue */
		/* [lots of XML returned about what’s in the queue]ObjectID = SQ:24 */
		error = NULL;
		success = (bool)gupnp_service_proxy_send_action (content_directory, "Browse", &error,
													"ObjectID", G_TYPE_STRING, "SQ:24",
													"BrowseFlag", G_TYPE_STRING, "BrowseMetadata",
													"Filter", G_TYPE_STRING, NULL,
													"StartingIndex", G_TYPE_UINT, 0,
													"RequestedCount", G_TYPE_UINT, 1,
													"SortCriteria", G_TYPE_STRING, NULL,
													NULL,
													"NumberReturned", G_TYPE_UINT, &NumberReturned,
													"TotalMatches", G_TYPE_UINT, &TotalMatches,
													"UpdateID", G_TYPE_UINT, &UpdateID,
//													"Result", G_TYPE_STRING, &result,
													NULL);
		if (!success) {
			_log.Log(LOG_ERROR,"(Sonos) CD Browse error %d %s", error, error->message);
		    g_error_free (error);
			return false;
		}

		// Save what's in Result <res> </res> and pass this as the URI
		sURL = std::string(result);
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) CD Browse %s NR[%d] TM[%d] UID[%d] \n------------------\nresult\n-------------------", 
			upnpdevice->id.c_str(), NumberReturned, TotalMatches, UpdateID, result );
#endif

		return(true);
	}

	/*
	 * UPnP Action method to browse Content Directory
	 */
	bool CSonosPlugin::SonosActionSaveQueue(RendererDeviceData *upnpdevice) {
		GError *error = NULL;
		gboolean success;

		/* Send action */
		if (upnpdevice->av_transport == NULL) {
			_log.Log(LOG_ERROR,"(Sonos) SaveQueue av_transport (null) for %s", upnpdevice->name.c_str());
		} else {
			success = gupnp_service_proxy_send_action (upnpdevice->av_transport, "SaveQueue", &error,
				"InstanceID", G_TYPE_UINT, 0, 
				"Title", G_TYPE_STRING, SONOS_SAVED_QUEUE_NAME, 
				"ObjectID", G_TYPE_STRING, NULL,
				NULL,
				NULL);
			if (!success) {
				_log.Log(LOG_ERROR,"(Sonos) SaveQueue error %d %s", error, error->message);
				g_error_free (error);
				return false;
			}

#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) SaveQueue for %s", upnpdevice->name.c_str());
#endif
		}

		return true;
	}

    /*+------------------------------------------------------------------------+*/
    /*| OTHER ACTIONS                                                          |*/
    /*+------------------------------------------------------------------------+*/

	/*
	 * SonosActionGetPlay1Temperature
	 * See http://www.hifi-forum.de/viewthread-100-623-4.html 
	 */
	bool CSonosPlugin::SonosActionGetPlay1Temperature(RendererDeviceData *upnpdevice, std::string& temperature)
	{
		unsigned long ulIpAddress=0;
		unsigned int id1=0, id2=0, id3=0, id4=0;

		// IP unsigned long to string
		std::istringstream iss(upnpdevice->id);
		iss >> std::hex >> ulIpAddress;
		id1 = (ulIpAddress>> 24) & 0xFF;
		id2 = (ulIpAddress>> 16) & 0xFF;
		id3 = (ulIpAddress>> 8) & 0xFF;
		id4 = (ulIpAddress) & 0xFF;
#ifdef _DEBUG
		_log.Log(LOG_NORM,"GetPlay1Temperature: deviceID %s uLong %8X id %d.%d.%d.%d", 
			upnpdevice->name.c_str(), ulIpAddress, id4, id3, id2, id1);
#endif	   

		std::string sResult;
		std::stringstream ss;
		ss << "http://" << id4 << "." << id3 <<"."<< id2 <<"."<< id1 <<":1400/status/proc/driver/temp-sensor";
		bool bret;
		std::string sURL=ss.str();

#ifdef _DEBUG
		_log.Log(LOG_NORM,"GetPlay1Temperature: get Url %s", sURL.c_str());
#endif
		bret=HTTPClient::GET(sURL,sResult);
		if (!bret) {
			_log.Log(LOG_ERROR,"GetPlay1Temperature: Error getting http data %8X (%s)!", 
				ulIpAddress, sURL.c_str());
			temperature = "";
			return false;
		}

		/* */
	   	std::size_t found = sResult.find("Celsius");
		found -= 3;
	   	std::string sTemperature(sResult, found, 2);
	    
		temperature = sTemperature;
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) GetPlay1Temperature: Temp %s", temperature.c_str());
#endif
	    // Sample data read: 004e:    41 Celsius: CPU Temperature sensor (fault 86, warn 0)
	    return true;
	}

	/*
	 * helperCreateHash
	 */
	std::string helperCreateHash(std::string a) {
		boost::uuids::detail::sha1 s;
		char hash[21];
		s.process_bytes(a.c_str(), a.size());
		unsigned int digest[5];
		s.get_digest(digest);
		for(int i = 0; i < 5; ++i)
		{
			const char* tmp = reinterpret_cast<char*>(digest);
			hash[i*4] = tmp[i*4+3];
			hash[i*4+1] = tmp[i*4+2];
			hash[i*4+2] = tmp[i*4+1];
			hash[i*4+3] = tmp[i*4];
		}
		hash[20] = 0x00;

		std::string ret(hash);
		return (ret);
	}

	/*
	 * UPnP Action method to say text to speech using Google translate
	 */
	bool CSonosPlugin::SonosActionSay(const std::string& sText, std::string& url, int type) {
		std::string sResult;
		bool bret;
		CURLEncode curl;

		// Sample url: http://translate.google.com/translate_tts?ie=UTF-8&q=Welcome%20to%20the%20Domoticz%20home&tl=en
		std::stringstream ssURL("");
		ssURL << "http://translate.google.com/translate_tts?ie=UTF-8&q=" << curl.URLEncode(sText) << "&tl=" << m_ttsLanguage;		
		std::string sURLTTS(ssURL.str());

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Say start %s", sURLTTS.c_str());
#endif

		// --------------------------------------------------------------------
		// TEST1: Downloaded audio for text and serve it through the domoticz webserver
		// SONOS: OK
		// XMBC:    ERROR: DVDPlayerCodec::Init: Error opening file http://X.X.X.X:8888/media/tts_text.mp3
		//          ERROR: CAudioDecoder: Unable to Init Codec while loading file http://X.X.X.X:8888/media/tts_text.mp3

		// Construct a filesystem neutral filename
//		std::stringstream strFilename(curl.URLDecode(sText));
//		strFilename << curl.URLEncode(strFilename.str()) << "-" << m_ttsLanguage;
//		strFilename << helperCreateHash(strFilename.str()) << ".mp3";
//		std::string filename = strFilename.str();

		std::string filename("tts_text.mp3");

		std::stringstream strFilepath;
		strFilepath << std::string(szWWWFolder) << "/media/" << filename;
		std::string filepath = strFilepath.str();

		// If not already downloaded request translation
//		struct stat buffer;   
//		if (stat (filepath.c_str(), &buffer) != 0) { 
//			_log.Log(LOG_NORM,"(Sonos) Downloading new tts message file to %s", filepath.c_str());

		bret=HTTPClient::GETBinaryToFile(sURLTTS,filepath);
		if (!bret) {
			_log.Log(LOG_ERROR,"(Sonos) Say: Error getting http (%s)!", sURLTTS.c_str());
			return false;
		}
//		} else {
//			_log.Log(LOG_NORM,"(Sonos) Say: Using cached tts message file: %s", filepath.c_str());
//		}


		ssURL.clear();
		ssURL.str("");
		ssURL << "http://" << m_host_ip << ":" << m_mainworker.GetWebserverPort() <<"/media/" << filename;
		url = ssURL.str();

		// --------------------------------------------------------------------
		// TEST 2: Return a url to a M3U file
		// XBMC: ERROR: Playlist Player: skipping unplayable item: 0, path [http://X.X.X.X:8888/media/tts_text.mp3.m3u]
		// Save m3u file
		// std::ofstream myfile;
		// myfile.open(filepath.c_str());
		// myfile << sURLTTS;
		// myfile.close();
		// url = ssURL.str();

		// --------------------------------------------------------------------
		// TEST 3: Return Google URL directly
		// SONOS: ok from Sonos manager creating a radio with http:// - on logs has been transformed to x-rincon-mp3radio
		// XBMC: ERROR: Sink DIRECTSOUND:{5164DEFA-341A-4CF8-A514-59DA4A1E915B} returned invalid buffer size: 240
		// Don't store anything... Just return Google URL
		// ssURL.clear();
		// ssURL.str("");
		// ssURL << "stack://" << sURLTTS;
		// url = std::string(ssURL.str());

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Say success (%s)!", url.c_str());
#endif
		return true;
	}
#endif

    /*+------------------------------------------------------------------------+*/
    /*| SonosInit - This is the start discovery method                         |*/
    /*+------------------------------------------------------------------------+*/
    void CSonosPlugin::SonosInit(void)
    {
        /* Save this class instance for use from helper/caller methods */
        thisInstance = this;

#if defined WIN32
#elif defined __linux__
        /* Create a new parser to help with decoding the AV XML data */
        lc_parser = gupnp_last_change_parser_new();

        /* Create a new GUPnP Context.  By here we are using the default GLib main
            context, and connecting to the current machine's default IP on an
            automatically generated port. */
        context = gupnp_context_new (NULL, NULL, 0, NULL);

		/* Save host ip - didn't find this info elsewhere in Domoticz... */
		std::string ip(gupnp_context_get_host_ip (context));
		m_host_ip = ip;
		_log.Log(LOG_NORM,"(Sonos) SonosInit host IP %s", m_host_ip.c_str());        

        /* Create Control Points targeting UPnP AV MediaRenderer and MediaServer devices */
        cpmr = gupnp_control_point_new(context, UPNP_MEDIA_RENDERER);	 // "upnp:rootdevice");	//   // GSSDP_ALL_RESOURCES 
        cpms = gupnp_control_point_new(context, UPNP_MEDIA_SERVER);	 

        /* The device-proxy-available signal is emitted when target devices are found - connect to it */
        g_signal_connect (cpmr, "device-proxy-available", G_CALLBACK (callbackDeviceDiscovered), NULL);
        g_signal_connect (cpms, "device-proxy-available", G_CALLBACK (callbackDeviceDiscovered), NULL);

        /* The device-proxy-unavailable signal is emitted when target devices are removed - connect to it */
        g_signal_connect( cpmr, "device-proxy-unavailable", G_CALLBACK (callbackDeviceUnavailable), NULL);
        g_signal_connect( cpms, "device-proxy-unavailable", G_CALLBACK (callbackDeviceUnavailable), NULL);
        
        /* Tell the Control Points to start searching */
        gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (cpmr), TRUE);
        gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (cpms), TRUE);
        
        /* Set a timeout of RUN_TIME seconds to do some things */
		g_timeout_add_seconds( RUN_TIME, callbackTimeout, NULL);
        
        /* Enter the main loop. This will start the search and result in callbacks to
            callbackDeviceDiscovered and callbackDeviceUnavailable. 
			This call will run forever waiting for new devices to appear*/
        main_loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (main_loop);

		/* Clean up */
        g_main_loop_unref (main_loop);
        g_object_unref (cpmr);
        g_object_unref (cpms);
        g_object_unref (context);
#endif        

        return;
    }



#ifdef DOCUMENTATION_FOR_NEXT_ADDITIONS
/*+----------------------------------------------------------------------------+*/
	INVOKE ACTIONS FOR WINDOWS/CURL:
http://forum.fibaro.com/printview.php?t=1196&start=0&sid=8977271b6abeedf2a69f931be259283d

Ungroup zone option 1
Code:	
POST /MediaRenderer/AVTransport/Control HTTP/1.1
Content-Length: 310
SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#BecomeCoordinatorOfStandaloneGroup"

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
<s:Body>
<u:BecomeCoordinatorOfStandaloneGroup xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
<InstanceID>0</InstanceID>
</u:BecomeCoordinatorOfStandaloneGroup>
</s:Body></s:Envelope>0x0D0x0A0x0D0x0A	


Ungroup zone option 2
This one proved to be a bit more stable for some users.
Code:	
POST /MediaRenderer/AVTransport/Control HTTP/1.1
Content-Length: 344
SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI"

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID>,<CurrentURI></CurrentURI>,<CurrentURIMetaData></CurrentURIMetaData></u:SetAVTransportURI></s:Body></s:Envelope>0x0D0x0A0x0D0x0A	


Group to zone
First check <your sonos ip>:1400/status/topology the UUID of your main zone, e.g: RINCON_000E58226B1601400

Code:	
POST /MediaRenderer/AVTransport/Control HTTP/1.1
Content-Length: 377
SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI"

<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID>,<CurrentURI>x-rincon:RINCON_000E58226B1601400</CurrentURI>,<CurrentURIMetaData></CurrentURIMetaData></u:SetAVTransportURI></s:Body></s:Envelope>0x0D0x0A0x0D0x0A	

/*+----------------------------------------------------------------------------+*/
CURL WITH ADDITIONAL HEADERS
<?php $ch=curl_init('http://192.168.0.39:1400'); 
$header_array = array( 'POST: /MediaRenderer/AVTransport/Control HTTP/1.1', 
	'CONNECTION: close', 
	'ACCEPT-ENCODING: gzip', 
	'CONTENT-TYPE: text/xml; charset="utf-8"', 
	'SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#Stop"' ); 
curl_setopt($ch,CURLOPT_HTTPHEADER,$header_array); 
curl_exec($ch); 
curl_close($ch); 
?>

/*+----------------------------------------------------------------------------+*/
QUEUES:
HINT 1:
* Clear queue (if you choose replace)
* Invoke AddURIToQueue with the approriate URI and metadata, representing the stored Sonos playlist.
*Do a Seek to position 1 if cleared, or the position where the playlist was added (if Play now was chosen).

HINT2:
Get the resource from the playlist id (eg "SQ:1") then

AddURIToQueue(0, resource, 0, true );
Seek( 0, "TRACK_NR", 0 );
Play(0, "1");

HINT 3:
POST /MediaRenderer/AVTransport/Control HTTP/1.1 
CONNECTION: close 
HOST: 192.168.66.110:1400 
CONTENT-LENGTH: 1420 
CONTENT-TYPE: text/xml; charset="utf-8"
SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#AddURIToQueue" 
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
<s:Body>
<u:AddURIToQueue xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
	<InstanceID>0</InstanceID>
	<EnqueuedURI>file:///jffs/settings/savedqueues.rsq#1</EnqueuedURI>
	<EnqueuedURIMetaData></EnqueuedURIMetaData>
	<DesiredFirstTrackNumberEnqueued>0</DesiredFirstTrackNumberEnqueued>
	<EnqueueAsNext>1</EnqueueAsNext>
</u:AddURIToQueue>
</s:Body>
</s:Envelope>

HINT 4:
Browse -> SQ:24
BrowseMetadata (not DirectChildren)
Parse the <res></res> out of the metadata for SQ:24 and pass this as the URI.
#endif

