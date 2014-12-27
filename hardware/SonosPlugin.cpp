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
#include "../webserver/Base64.h"
#include <wchar.h>
#include <sys/stat.h>

// For hash calculation
// #include <iostream>
// #include <fstream>
// #include <boost/filesystem.hpp>
#include <boost/uuid/sha1.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/unordered_map.hpp>

// Conditional compiling
#define _DEBUG							true
#define _DEBUG_2						true
#define _DEBUG_3						true
#define BELKIN							true
//  #define PLAY1_GET_TEMPERATURE		true

#include "SonosPlugin.h"

// UPnP constants
#define UPNP_STATE_NULL					0
#define UPNP_STATE_PLAYING				1
#define UPNP_STATE_TRANSITIONING		2
#define UPNP_STATE_PAUSED				3
#define UPNP_STATE_STOPPED				4
#define UPNP_STATE_NO_MEDIA_PRESENT		5

// UPnP Device Interfaces
#define UPNP_MEDIARENDERER				0
#define UPNP_MEDIASERVER				1
#define UPNP_BELKINWEMO					2

#define UPNP_DEVICE_MEDIARENDERER		"urn:schemas-upnp-org:device:MediaRenderer:1"
#define UPNP_DEVICE_MEDIASERVER			"urn:schemas-upnp-org:device:MediaServer:1"
#define UPNP_DEVICE_BELKINWEMO			"urn:Belkin:device:controllee:1"
#define UPNP_DEVICE_ALL					"ssdp:all"

// UPnP Services
#define UPNP_SRV_AV_TRANSPORT			"urn:schemas-upnp-org:service:AVTransport"
#define UPNP_SRV_RENDERING_CONTROL		"urn:schemas-upnp-org:service:RenderingControl:1"
#define UPNP_SRV_CONTENT_DIRECTORY		"urn:schemas-upnp-org:service:ContentDirectory:1"
#define UPNP_SRV_BELKINBASIC			"urn:Belkin:service:basicevent:1"

// Type of device
#define UPNP_TYPE_NULL					0
#define UPNP_TYPE_SONOS					1
#define UPNP_TYPE_PLAY1					2
#define UPNP_TYPE_XBMC					3
#define UPNP_TYPE_TV					4
#define UPNP_TYPE_ONKYO					5
#define UPNP_TYPE_BELKIN				6
#define UPNP_TYPE_OTHERS				9

// Sonos constants
#define SONOS_SAVED_QUEUE_NAME			"SQ:24"

#define UPNP_SONOS_SOURCE_NONE			0
#define UPNP_SONOS_SOURCE_LINEIN		1
#define UPNP_SONOS_SOURCE_RADIO			2
#define UPNP_SONOS_SOURCE_FILE			3
#define UPNP_SONOS_SOURCE_HTTP			4
#define UPNP_SONOS_SOURCE_QUEUE			5
#define UPNP_SONOS_SOURCE_MASTER		6

// Domoticz device command constants
#define sonos_sPause					0		// light2_sOn
#define sonos_sPlay						1		// light2_sOff
#define sonos_sSetVolume				2		// light2_sSetLevel
#define sonos_sSay						6

#define sonos_sDebug					0x10
#define sonos_sPreset					0x11	// Higher values are presets

// Domoticz user variables
#define USERVAR_TTS						"sonos-tts"
#define USERVAR_PRESET					"sonos-preset-"

// Domoticz devices for a Sonos "compound device"
#define UNIT_Sonos_PlayPause			0
#define UNIT_Sonos_TempPlay1			1
#define UNIT_Sonos_Say					2
#define UNIT_Sonos_Preset				3

// Domoticz devices for a Belkin Wemo
#define UNIT_Belkin_OnOff				5

// Miscelaneous
#define PRESET_LEVEL					5
#define NO_VOLUME_CHANGE				-1
#define NO_LEVEL_CHANGE					-1
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
static void callbackGetPlay1Temperature(UPnPDevice *upnpdevice);
static void callbackGetTrackInfo(GUPnPDIDLLiteParser *parser, 
	GUPnPDIDLLiteObject *object, gpointer renderer);
#endif

/* Store / maps for UPnP devices */
typedef boost::unordered_map<std::string,std::string>					UPnPNamesMap;
typedef boost::unordered_map<std::string,UPnPDevice*>					UPnPDevicesMap;
typedef boost::unordered_map<std::string,DeviceData*>					WemoMap;

typedef boost::unordered_map<std::string,std::string>::iterator			IteratorNames;
typedef boost::unordered_map<std::string,UPnPDevice*>::iterator			IteratorDevices;
typedef boost::unordered_map<std::string,DeviceData*>::iterator			IteratorWemos;

static UPnPNamesMap					upnpnamesmap;	// Store MediaRenderers + MediaServers - key=udn data=long ip
static UPnPDevicesMap				devicesmap;	// Store MediaRenderers - key=long ip data=UPnPDevice
static WemoMap						wemosmap;		// Store Belkin Wemo - key=long ip data=DeviceData

std::string							m_host_ip;
std::string							m_ttsLanguage("EN");	// "ES";

/* Access C++ class from C callbacks */
static CSonosPlugin					*thisInstance;	// Dirty trick-access CSonosPlugin 
// methods from "C" callbacks
static int							hwIdStatic;		// idem

/* Utilities */
unsigned long helperGetIpFromLocation(const char *szLocation, char *szIP );
std::string helperCreateHash(std::string a);
std::string helperGetUserVariable(const std::string &name);
bool helperChangeProtocol(std::string &url, int type);

/*+----------------------------------------------------------------------------+*/
/*| helperCreateHash.                                                          |*/
/*+----------------------------------------------------------------------------+*/
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

/*+----------------------------------------------------------------------------+*/
/*| helperGetUserVariable                                                      |*/
/*+----------------------------------------------------------------------------+*/
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

/*+----------------------------------------------------------------------------+*/
/*| Change protocols.                                                          |*/
/*+----------------------------------------------------------------------------+*/
bool helperChangeProtocol(std::string &url, int type)
{
	// If protocol == x-rincon...: and type not sonos, then change to http:
	return true;
}

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


/*+----------------------------------------------------------------------------+*/
/*| CSonosPlugin class destructor.                                             |*/
/*+----------------------------------------------------------------------------+*/
CSonosPlugin::~CSonosPlugin(void)
{
#ifdef WIN32
	//sorry Win32 not supported for now
#endif
	StopHardware();
}

/*+----------------------------------------------------------------------------+*/
/*| StartHardware.                                                             |*/
/*+----------------------------------------------------------------------------+*/
bool CSonosPlugin::StartHardware()
{
#ifdef _DEBUG
	_log.Log(LOG_STATUS,"(Sonos) Started");
#endif
	Init();

	if (m_bEnabled)
	{
		m_thread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&CSonosPlugin::Do_Work, this)));
	}
	return true;
}

/*+----------------------------------------------------------------------------+*/
/*| StopHardware.                                                              |*/
/*+----------------------------------------------------------------------------+*/
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

/*+----------------------------------------------------------------------------+*/
/*| Plugin Initialization.                                                     |*/
/*+----------------------------------------------------------------------------+*/
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

/*+----------------------------------------------------------------------------+*/
/*| Do_Work.                                                                   |*/
/*+----------------------------------------------------------------------------+*/
void CSonosPlugin::Do_Work()
{
	// Discover Sonos devices
	SonosInit();

	_log.Log(LOG_STATUS,"(Sonos) Stopped...");			
}

/*+----------------------------------------------------------------------------+*/
/*| WriteToHardware - Write from Domoticz UI to the hardware layer             |*/
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
		UPnPDevice *upnpdevice;
		IteratorDevices it = devicesmap.find(deviceID);
		upnpdevice = (UPnPDevice *)it->second;

		int unit = pCmd->LIGHTING2.unitcode;
		int volume = pCmd->LIGHTING2.level;
		int command = pCmd->LIGHTING2.cmnd;

		// @@@ Command is not preserved ?¿
		if ((unit == UNIT_Sonos_Say) && (command == sonos_sPlay))
			command = sonos_sSay;
		else if ((unit == UNIT_Sonos_Preset) && (command == sonos_sPlay))
			command = sonos_sPreset;
		else if ((unit == (UNIT_Sonos_Preset+1)) && (command == sonos_sPlay))
			command = sonos_sPreset+1;
		_log.Log(LOG_ERROR,"(Sonos) WriteToHardware unit %d command %d new-command %d", 
			unit, pCmd->LIGHTING2.cmnd, command);

		if (command==sonos_sPause) {
			// STOP
			upnpdevice->Action( "Pause" );

			// Save in device level the last known volume
			upnpdevice->volume	= upnpdevice->GetVolume();
			upnpdevice->prev_state = UPNP_STATE_STOPPED;
#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) WriteToHardware %s Pause unit %d", 
				upnpdevice->name.c_str(), unit );
#endif
		} else if (pCmd->LIGHTING2.cmnd==sonos_sSetVolume) {
			upnpdevice->SetVolume( volume );
#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) WriteToHardware %s Volume from %d to %d", 
				upnpdevice->name.c_str(), upnpdevice->volume, volume);
#endif			
			upnpdevice->volume	= volume;

		} else if (command==sonos_sPlay) {
			// PLAY
			if (upnpdevice->prev_state == UPNP_STATE_STOPPED) {
				upnpdevice->SetVolume( upnpdevice->volume );
			}
			upnpdevice->prev_state = UPNP_STATE_PLAYING;

			// Check if there's something to play
			std::string currenturi;
			upnpdevice->GetPositionInfo(currenturi);
			if (currenturi != "") {
				// Play!
				upnpdevice->Play();
#ifdef _DEBUG
				_log.Log(LOG_NORM,"(Sonos) WriteToHardware %s Play unit %d volume %d", 
					upnpdevice->name.c_str(), unit, volume);
#endif
			} else {
#ifdef _DEBUG
				_log.Log(LOG_NORM,"(Sonos) WriteToHardware %s Nothing to play!", upnpdevice->name.c_str());
#endif
			}

		} else if (command==sonos_sSay) {
			std::string sURL;
			std::string uservar(USERVAR_TTS);
			std::string text = helperGetUserVariable(uservar);
			if (text == "")
				text = "Welcome to the Domoticz home";

			if (upnpdevice->Say( text, sURL, upnpdevice->type ) == true) {
				_log.Log(LOG_NORM,"(Sonos) Say \'%s\' saved to \'%s\'", text.c_str(), sURL.c_str());
			} else {
				sURL = "http://192.168.1.63:8888/media/tts-text.mp3";
				_log.Log(LOG_NORM,"(Sonos) Say error. Playing last TTS message");
			}

			// Save state - before you "Unlink" a ZP, you have to figure out if it's 
			//   a) linked to another ZP
			//   b) streaming radio
			//   c) streaming an audio line in or 
			//   d) playing its own queue of stuff. 
			// Depending on which (a through d) it is doing, when it's time to "restore" 
			// that ZP back to its original state, you have to take very different steps.
			if (upnpdevice->type == UPNP_TYPE_SONOS) {
				// Save session
				//				if (upnpdevice->source == UPNP_SONOS_SOURCE_QUEUE) {
				/* Save current uri and state */
				//					std::string state;
				//					upnpdevice->GetTransportInfo(upnpdevice, state);

				//					std::string currenturi;
				//					int source;
				//					upnpdevice->GetPositionInfo(currenturi);

				// d) Save Queue / State
				//					upnpdevice->SaveQueue();
				//				}

				// other possible states: UPNP_SONOS_SOURCE_LINEIN UPNP_SONOS_SOURCE_RADIO UPNP_SONOS_SOURCE_FILE

				// Set current and next URI and play - UNLINK!
				upnpdevice->SetURI( sURL );

				// Restore session
				//				if (upnpdevice->source == UPNP_SONOS_SOURCE_QUEUE) {
				//					upnpdevice->restore_state = true;
				//				}
			} else {
				// Set current and next URI and play - UNLINK!
				upnpdevice->SetURI( sURL );
			}
			upnpdevice->Play();

		} else if (command == sonos_sDebug) {
			ListMaps();

		} else if (command >= sonos_sPreset) {
			std::string sURL;
			int preset_number=(command - sonos_sPreset)+1;

			std::stringstream s; 
			s << std::string(USERVAR_PRESET) << preset_number; 
			std::string preset_station = s.str();

			sURL = helperGetUserVariable(preset_station);
			if (upnpdevice->type == UPNP_TYPE_SONOS) {
				if (sURL == "")
					sURL = "x-rincon-mp3radio://radioclasica.rtve.stream.flumotion.com/rtve/radioclasica.mp3.m3u";
			} else {
				if (sURL == "")
					sURL = "http://radioclasica.rtve.stream.flumotion.com/rtve/radioclasica.mp3.m3u";
			}
			helperChangeProtocol(sURL, upnpdevice->type);

			// Set current and next URI and play - UNLINK!
			upnpdevice->SetURI( sURL );
			upnpdevice->Play();
#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) WriteToHardware Preset devid %8X cmnd %d unit %d volume %d", 
				ulIpAddress, pCmd->LIGHTING2.cmnd, unit, volume);
#endif
		} else {
			_log.Log(LOG_ERROR,"(Sonos) WriteToHardware packet type %d, subtype %d or command %d unknown", 
				pCmd->LIGHTING2.packettype, pCmd->LIGHTING2.subtype, pCmd->LIGHTING2.cmnd);
		}
	}

	return;
}

/*+----------------------------------------------------------------------------+*/
/*| UpdateRendererValue - Update domoticz state in database and UI           |*/
/*+----------------------------------------------------------------------------+*/
void CSonosPlugin::UpdateRendererValue(	int qType, 
	UPnPDevice &p_reference,
	const std::string& devValue,
	int volume)
{
	std::vector<std::vector<std::string> > result;
	int					dtype=0;
	int					dsubtype=-1;
	int					dunit;
	std::string			suffixName;
	std::stringstream	szQuery;
	unsigned long		ulIdx=0;
	int					preset_number;
	UPnPDevice			*upnpdevice = &p_reference;		// &p_reference
	std::string			devName = upnpdevice->name; 

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
		if (upnpdevice->type == UPNP_TYPE_SONOS)
			dsubtype=sTypeSonos;
		else
			dsubtype=sTypeUPnP;
		dunit=qType;
		suffixName = " Play/Pause";
	} else if (qType==UNIT_Sonos_TempPlay1) {
		dtype=80; // pTypeGeneral;
		dsubtype=1; // sTypeSystemTemp;
		dunit=qType;
		suffixName = " Temp";
	} else if (qType==UNIT_Sonos_Say) {
		dtype=pTypeLighting2;
		if (upnpdevice->type == UPNP_TYPE_SONOS)
			dsubtype=sTypeSonos;
		else
			dsubtype=sTypeUPnP;
		dunit=qType;
		suffixName = " TTS";
	} else if (qType >= UNIT_Sonos_Preset) {
		preset_number=(qType - UNIT_Sonos_Preset)+1;

		dtype=pTypeLighting2;
		if (upnpdevice->type == UPNP_TYPE_SONOS)
			dsubtype=sTypeSonos;
		else
			dsubtype=sTypeUPnP;
		dunit=qType;

		std::stringstream s; 
		s << " P" << preset_number; 
		suffixName = std::string(s.str());	
	}

#ifdef _DEBUG
	//	_log.Log(LOG_NORM,"(Sonos) Update1 devId %s LIP %8X Value %s", upnpdevice->id.c_str(), upnpdevice->ip, devValue.c_str());
#endif
	szQuery << "SELECT ID, Name FROM DeviceStatus WHERE (DeviceID=='" << upnpdevice->id << "' AND Unit=" << dunit << ")";
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
			"INSERT INTO DeviceStatus (HardwareID, DeviceID, Unit, Type, SubType, SwitchType, SignalLevel, BatteryLevel, Name, nValue, sValue) "
			"VALUES (" << hwId << ",'" << upnpdevice->id << "',"<< dunit << "," << dtype << "," <<dsubtype << ",17,12,255,'" << devNameEasy << "'," << devValue << ",'" << devValue << "')";
		result=m_sql.query(szQuery.str());
		if (result.size()<0) {
			_log.Log(LOG_ERROR,"(Sonos) Insert: database error, inserting devID %s", upnpdevice->id.c_str());
			return;
		}

		// Get newly created ID
		szQuery.clear();
		szQuery.str("");
		szQuery << 
			"SELECT ID FROM DeviceStatus WHERE (HardwareID=" << hwId <<" AND DeviceID='" << upnpdevice->id << "' AND Unit=" << dunit << " AND Type=" << dtype << " AND SubType=" << dsubtype <<")";
		result=m_sql.query(szQuery.str());
		if (result.size()<0) {
			_log.Log(LOG_ERROR,"(Sonos) Insert: database error, problem getting ID from DeviceStatus for devID %s!", upnpdevice->id.c_str());
			return;
		}

		// Read index in DB
		sscanf(result[0][0].c_str(), "%d", &ulIdx );
		_log.Log(LOG_NORM,"(Sonos) Insert: devID %s devName \'%s\' devIdx %d LIP %x", 
			upnpdevice->id.c_str(), devNameEasy.c_str(), ulIdx, upnpdevice->ip );
	} else {
		// Device already added to device list - update

		// get idx and name @@@
		sscanf(result[0][0].c_str(), "%d", &ulIdx );
		upnpdevice->name = std::string(result[0][1]);

		time_t now = time(0);
		struct tm ltime;
		localtime_r(&now,&ltime);

		char szLastUpdate[40];
		sprintf(szLastUpdate,"%04d-%02d-%02d %02d:%02d:%02d",ltime.tm_year+1900,ltime.tm_mon+1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Update: Id=%s Name=\'%s\' Idx=%d LIP=%8x Value=%s", 
			upnpdevice->id.c_str(), upnpdevice->name.c_str(), ulIdx, upnpdevice->ip, devValue.c_str() );
#endif
		szQuery.clear();
		szQuery.str("");
		szQuery << "UPDATE DeviceStatus SET HardwareID = " << hwId << ", nValue=" << devValue << ", sValue ='" << devValue << 
			"', LastUpdate='" << szLastUpdate << "', StrParam1='" << upnpdevice->title << 
			"', StrParam2='" << upnpdevice->device_icon <<
			"' WHERE (DeviceID == '" << upnpdevice->id << "' AND Unit=" << dunit << ")";
		m_sql.query(szQuery.str());

		// UNIT_Sonos_TempPlay1 - Temperature Device for Play:1
		if (qType == UNIT_Sonos_TempPlay1) {
			// Maybe I had to use TEMP like in the UNIT_Sonos_PlayPause case...
			// but TEMP just has id1 and id2... No way to code all the IP in two bytes
			int temperature = atoi(devValue.c_str());

			// NO NEED TO USE sDecodeRXMessage in this case
			//			sDecodeRXMessage(this, (const unsigned char *)&lcmd.TEMP); //decode message

			m_sql.CheckAndHandleNotification(hwId, upnpdevice->id, dunit, dtype, dsubtype, NTYPE_TEMPERATURE, (const float)temperature);

		} else if (qType == UNIT_Sonos_PlayPause) {
			int nValue=0;
			nValue = (const int)atoi(devValue.c_str());

			//Add Lighting log
			m_sql.m_LastSwitchID=upnpdevice->id;
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

			lcmd.LIGHTING2.id1 = (upnpdevice->ip>> 24) & 0xFF;
			lcmd.LIGHTING2.id2 = (upnpdevice->ip>> 16) & 0xFF;
			lcmd.LIGHTING2.id3 = (upnpdevice->ip>> 8) & 0xFF;
			lcmd.LIGHTING2.id4 = (upnpdevice->ip) & 0xFF;

			lcmd.LIGHTING2.unitcode = dunit;
			if (nValue == 0) {
				lcmd.LIGHTING2.cmnd = sonos_sPause;
			} else {
				lcmd.LIGHTING2.cmnd = sonos_sPlay;         
			}

			if (volume != NO_VOLUME_CHANGE) {
				upnpdevice->volume = volume;
			}

			lcmd.LIGHTING2.level = upnpdevice->volume;
			lcmd.LIGHTING2.filler = 0;
			lcmd.LIGHTING2.rssi = 12;

			/* THIS is a very important method: feeds device state to the internal Domoticz "bus" */ 
			sDecodeRXMessage(this, (const unsigned char *)&lcmd.LIGHTING2); //decode message
		} else if ((qType == UNIT_Sonos_Say) || (qType >= UNIT_Sonos_Preset) ) {
			int nValue=0;
			nValue = (const int)atoi(devValue.c_str());

			// Add Lighting log
			m_sql.m_LastSwitchID=upnpdevice->id;
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

			lcmd.LIGHTING2.id1 = (upnpdevice->ip>> 24) & 0xFF;
			lcmd.LIGHTING2.id2 = (upnpdevice->ip>> 16) & 0xFF;
			lcmd.LIGHTING2.id3 = (upnpdevice->ip>> 8) & 0xFF;
			lcmd.LIGHTING2.id4 = (upnpdevice->ip) & 0xFF;

			lcmd.LIGHTING2.unitcode = dunit;		// UNIT_Sonos_Say

			if (qType == UNIT_Sonos_Say)
				lcmd.LIGHTING2.cmnd = sonos_sSay;		
			else if (qType >= UNIT_Sonos_Preset)
				lcmd.LIGHTING2.cmnd = sonos_sPreset + preset_number - 1;		

			lcmd.LIGHTING2.level = 0;
			lcmd.LIGHTING2.filler = 0;
			lcmd.LIGHTING2.rssi = 12;

			/* THIS is a very important method: feeds device state to the internal Domoticz "bus" */ 
			sDecodeRXMessage(this, (const unsigned char *)&lcmd.LIGHTING2); //decode message
		}
	}

	return;
}

/*+----------------------------------------------------------------------------+*/
/*| UpdateSwitchValue - Update domoticz state in database and UI               |*/
/*+----------------------------------------------------------------------------+*/
void CSonosPlugin::UpdateSwitchValue(	int qType, 
	DeviceData *upnpdevice,
	const std::string& devValue,
	int level)
{
	std::vector<std::vector<std::string> > result;
	int					dtype=0;
	int					dsubtype=-1;
	int					dunit;
	std::string			suffixName;
	std::stringstream	szQuery;

	unsigned long		ulIdx=0;
	std::string			devName = upnpdevice->name; 

	if (!hwId) {
		_log.Log(LOG_ERROR,"(Belkin) Id not found!");
		return;
	}

	if (qType==UNIT_Belkin_OnOff) {
		dtype=pTypeLighting2;
		dsubtype=sTypeAC;
		dunit=0;
		suffixName = " On/Off";
		return; 
	} else {
		// Unsupported
		return;
	}

#ifdef _DEBUG
	//	_log.Log(LOG_NORM,"(Belkin) Update1 devId %s LIP %8X Value %s", upnpdevice->id.c_str(), upnpdevice->ip, devValue.c_str());
#endif
	szQuery << "SELECT ID, Name FROM DeviceStatus WHERE (DeviceID=='" << upnpdevice->id << "' AND Unit=" << dunit << ")";
	result=m_sql.query(szQuery.str());

	// Device is not already added to device list - insert
	if (result.size()<1)		
	{
		szQuery.clear();
		szQuery.str("");

		// We use a easy devname
		// @@@ If unit = 0 then switchtype=Dimmer else switchtype=PushOn
		std::stringstream ssName;
		ssName << devName << suffixName;
		std::string devNameEasy = ssName.str();
		szQuery << 
			"INSERT INTO DeviceStatus (HardwareID, DeviceID, Unit, Type, SubType, SignalLevel, BatteryLevel, Name, nValue, sValue) "
			"VALUES (" << hwId << ",'" << upnpdevice->id << "',"<< dunit << "," << dtype << "," <<dsubtype << ",12,255,'" << devNameEasy << "'," << devValue << ",'" << devValue << "')";
		result=m_sql.query(szQuery.str());
		if (result.size()<0) {
			_log.Log(LOG_ERROR,"(Belkin) Insert: database error, inserting devID %s", upnpdevice->id.c_str());
			return;
		}

		// Get newly created ID
		szQuery.clear();
		szQuery.str("");
		szQuery << 
			"SELECT ID FROM DeviceStatus WHERE (HardwareID=" << hwId <<" AND DeviceID='" << upnpdevice->id << "' AND Unit=" << dunit << " AND Type=" << dtype << " AND SubType=" << dsubtype <<")";
		result=m_sql.query(szQuery.str());
		if (result.size()<0) {
			_log.Log(LOG_ERROR,"(Belkin) Insert: database error, problem getting ID from DeviceStatus for devID %s!", upnpdevice->id.c_str());
			return;
		}

		// Read index in DB
		sscanf(result[0][0].c_str(), "%d", &ulIdx );
		_log.Log(LOG_NORM,"(Belkin) Insert: devID %s devName \'%s\' devIdx %d LIP %x", 
			upnpdevice->id.c_str(), devNameEasy.c_str(), ulIdx, upnpdevice->ip );
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
		_log.Log(LOG_NORM,"(Belkin) Update: devID %s devName \'%s\' devIdx %d LIP %8x Value %s", 
			upnpdevice->id.c_str(), strDevName.c_str(), ulIdx, upnpdevice->ip, devValue.c_str() );
#endif
		szQuery.clear();
		szQuery.str("");
		szQuery << "UPDATE DeviceStatus SET HardwareID = " << hwId << ", nValue=" << devValue << ", sValue ='" << devValue << "', LastUpdate='" << szLastUpdate << "' WHERE (DeviceID == '" << upnpdevice->id << "' AND Unit=" << dunit << ")";
		m_sql.query(szQuery.str());

		if (qType == UNIT_Belkin_OnOff) {
			int nValue=0;
			nValue = (const int)atoi(devValue.c_str());

			// Add Lighting log
			m_sql.m_LastSwitchID=upnpdevice->id;
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

			lcmd.LIGHTING2.id1 = (upnpdevice->ip>> 24) & 0xFF;
			lcmd.LIGHTING2.id2 = (upnpdevice->ip>> 16) & 0xFF;
			lcmd.LIGHTING2.id3 = (upnpdevice->ip>> 8) & 0xFF;
			lcmd.LIGHTING2.id4 = (upnpdevice->ip) & 0xFF;

			lcmd.LIGHTING2.unitcode = dunit;
			if (nValue == 0) {
				lcmd.LIGHTING2.cmnd = light2_sOn;
			} else {
				lcmd.LIGHTING2.cmnd = light2_sOff;         
			}

			if (level != NO_LEVEL_CHANGE) {
				lcmd.LIGHTING2.level = level;
			} else {
				lcmd.LIGHTING2.level = upnpdevice->level;
			}

			lcmd.LIGHTING2.filler = 0;
			lcmd.LIGHTING2.rssi = 12;

			/* THIS is a very important method: feeds device state to the internal Domoticz "bus" */ 
			sDecodeRXMessage(this, (const unsigned char *)&lcmd.LIGHTING2); //decode message
		} 
	}

	return;
}

#if defined WIN32
//sorry Win32 not supported for now
#elif defined __linux__
/*+------------------------------------------------------------------------+*/
/*| CALLBACKS - Cannot be part of the class AFAIK - Somebody?              |*/
/*+------------------------------------------------------------------------+*/
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
	bool						bVolumeChanged = false;
	int							new_state;

#ifdef _DEBUG_3
	_log.Log(LOG_NORM,"(Sonos) Change1 [%s]", variable_name);
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
		} else {
			_log.Log(LOG_ERROR, "(Sonos) Change2 parse error" );
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

	/* Look up the UDN in our hashtable to get the renderer */
	udn = gupnp_service_info_get_udn(GUPNP_SERVICE_INFO (av_transport));
	IteratorNames itrnd = upnpnamesmap.find(std::string(udn));
	std::string deviceID = (std::string)itrnd->second;

	/* Look up the renderer device data from IP */
	UPnPDevice *upnpdevice;
	IteratorDevices it = devicesmap.find(deviceID);
	upnpdevice = (UPnPDevice *)it->second;

	// Possible states: STOPPED PLAYING PAUSED PAUSED_PLAYBACK TRANSITIONING NO_MEDIA_PRESENT NULL
	// XBMC/Kodi results in a (null) state_name sometimes!!!
	bool ignore_change = false;
	if (state_name == NULL) {
		_log.Log(LOG_ERROR,"(Sonos) (NULL State) (%s)!!", upnpdevice->name.c_str());
		//			new_state = UPNP_STATE_PLAYING;
		upnpdevice->prev_state = UPNP_STATE_NULL;
		ignore_change = true;
	} else {
		if (strcmp(state_name, "TRANSITIONING") == 0) {
			new_state = UPNP_STATE_TRANSITIONING;
#ifdef _DEBUG_3
			if (new_state != upnpdevice->prev_state)
				_log.Log(LOG_NORM,"(Sonos) %s(%s) ignored", state_name, upnpdevice->name.c_str() );
#endif
			// More than one subsequent TRANSITIONING are frequent 
			upnpdevice->prev_state = new_state;
			ignore_change = true;
		} else if (strcmp(state_name, "NO_MEDIA_PRESENT") == 0) {
			new_state = UPNP_STATE_NO_MEDIA_PRESENT;
#ifdef _DEBUG_3
			_log.Log(LOG_NORM,"(Sonos) %s(%s) ignored", state_name, upnpdevice->name.c_str() );
#endif
			upnpdevice->prev_state = new_state;
			ignore_change = true;
		} else if (strcmp(state_name, "STOPPED") == 0) {
			new_state = UPNP_STATE_STOPPED;
		} else if (strcmp(state_name, "PLAYING") == 0) {
			new_state = UPNP_STATE_PLAYING;
		} else if (strcmp(state_name, "PAUSED") == 0) {
			new_state = UPNP_STATE_PAUSED;
			_log.Log(LOG_ERROR,"(Sonos) %s(%s) unexpected state ignored", state_name, upnpdevice->name.c_str() );
		} else if (strcmp(state_name, "PAUSED_PLAYBACK") == 0) {
			new_state = UPNP_STATE_PAUSED;			
		} else {
			_log.Log(LOG_ERROR,"(Sonos) %s(%s) unexpected state ignored", state_name, upnpdevice->name.c_str() );
			ignore_change = true;
		}
	}

	/* Return */
	if (ignore_change) {
		// Free resources
		if (state_name != NULL) 
			g_free (state_name);
		if (metadata != NULL) 
			g_free (metadata);		
		if (parser != NULL) 
			g_object_unref (parser);
		return;
	}

	/* Get volume level */
	int volume = upnpdevice->GetVolume();
	if (volume != upnpdevice->volume) {
		bVolumeChanged = true;
#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) %s(%s) Change volume %d to %d", state_name, upnpdevice->name.c_str(), upnpdevice->volume, volume );
#endif		
	}

	// Get current track info from metadata
	// - Only when starting to play or playing something different
	if (new_state == UPNP_STATE_PLAYING) {
		if (metadata != NULL) {
			int meta_length = strlen(metadata);
			if (meta_length > 5) {
				GError     *lc_error;

				lc_error = NULL;
#ifdef _DEBUG
				//	_log.Log(LOG_NORM,"(Sonos) Change6 Metadata len: %d", meta_length);
#endif
				g_signal_connect (parser, "object-available", G_CALLBACK (callbackGetTrackInfo), (gpointer) upnpdevice);
				gupnp_didl_lite_parser_parse_didl (parser, metadata, &lc_error);
				if (lc_error) {
					_log.Log(LOG_ERROR,"(Sonos) Change7 Parse DIDL error %s\n", lc_error->message);
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
		upnpdevice->GetPositionInfo(currenturi);

		/* Adjust state and volume level for each Sonos speaker */
		thisInstance->UpdateRendererValue(UNIT_Sonos_PlayPause, *upnpdevice, "1", volume);

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) %s(%s) Change track to %s", state_name, upnpdevice->name.c_str(), currenturi.c_str() );
#endif		
	} else if ((new_state == UPNP_STATE_STOPPED) || (new_state == UPNP_STATE_PAUSED)) {
		thisInstance->UpdateRendererValue(UNIT_Sonos_PlayPause, *upnpdevice, "0", volume);

		/* If STOPPED and restore state pending -> restorestate */
		if (upnpdevice->restore_state) {
			std::string sURLQueue;
			upnpdevice->LoadQueue(sURLQueue);
			_log.Log(LOG_NORM,"(Sonos) Change11 RestoreState Queue %s", upnpdevice->name.c_str());
			upnpdevice->restore_state = false;
		}
	}

	// Store previous state
	upnpdevice->prev_state = new_state;

	// Free resources
#ifdef _DEBUG_3
	//		_log.Log(LOG_NORM, "(Sonos) Change12 end S[%x] M[%x] P[%x]", state_name, metadata, parser);
#endif
	if (state_name != NULL) 
		g_free (state_name);
	if (metadata != NULL) 
		g_free (metadata);		
	if (parser != NULL) 
		g_object_unref (parser);
}

/*+------------------------------------------------------------------------+*/
/*| Callback method to handle new devices which have been discovered       |*/
/*+------------------------------------------------------------------------+*/
static void callbackDeviceDiscovered(GUPnPControlPoint *cp, GUPnPDeviceProxy *deviceproxy)
{
	GUPnPServiceProxy *av_transport;
	GUPnPDeviceInfo* info  = GUPNP_DEVICE_INFO(deviceproxy);
	int deviceType = UPNP_MEDIARENDERER;

	/* Only allow MediaRenderers and MediaServers */
	char *device_type = NULL;
	device_type = (char *)gupnp_device_info_get_device_type (info);
	if (strcmp(device_type, UPNP_DEVICE_MEDIARENDERER) == 0) 
		deviceType = UPNP_MEDIARENDERER;
	else if (strcmp(device_type, UPNP_DEVICE_MEDIASERVER) == 0)
		deviceType = UPNP_MEDIASERVER;
	else if (strcmp(device_type, UPNP_DEVICE_BELKINWEMO) == 0)
		deviceType = UPNP_BELKINWEMO;
	else {
		// @@@ New error log
		_log.Log(LOG_ERROR,"(Sonos) Discovered device with non-supported type %s", device_type); 
		return;
	}

	/* Get UDN */
	std::string udn(gupnp_device_info_get_udn(info));

	/* Extract IP and add device ip to list of current device ips */
	char szIP[30];
	unsigned long longIP = helperGetIpFromLocation(gupnp_device_info_get_location (info), szIP);
#ifdef _DEBUG
	_log.Log(LOG_STATUS,"(Sonos) Discovered %s Model[%s] Type[%s] Loc[%s] Manu[%s]", 
		udn.c_str(), 
		gupnp_device_info_get_model_name (info), 
		gupnp_device_info_get_device_type (info),
		gupnp_device_info_get_location (info),
		gupnp_device_info_get_manufacturer(info) );
#endif

	std::stringstream ss;
	ss << std::uppercase << std::setfill('0') << std::setw(8) << std::hex <<  longIP;
	std::string deviceID(ss.str());

	/* Save all info in UPnP renderer structure */
	if (deviceType == UPNP_MEDIARENDERER) {
		UPnPDevice *upnpdevice;
		bool isInMap = false;
		bool isServerCreated = false;
		bool isRendererCreated = false;

		/* If device already saved, retrieve it - else create a new one */
		IteratorDevices it = devicesmap.find(deviceID);
		if (it != devicesmap.end()) {
			upnpdevice = (UPnPDevice *)it->second;
			isInMap = true;

			// Device already in map for media renderer
			if (upnpdevice->renderer != NULL) {
				_log.Log(LOG_NORM,"(Sonos) Discovered %s duplicate renderer!", upnpdevice->name.c_str());
				isRendererCreated = true;
			}

			// Device already in map for media server
			if (upnpdevice->server != NULL)
				isServerCreated = true;
		} else {
			// Device still not created neither for media server or renderer
			upnpdevice = new(UPnPDevice);
			upnpdevice->server = NULL;
		}

		/* Obtain extra device data */
		std::string brand, model, name;

		upnpdevice->renderer = deviceproxy;
		upnpdevice->ip = longIP;
		upnpdevice->id = deviceID;
		upnpdevice->udn = udn;
		upnpdevice->GetDeviceData(brand, model, name);
		_log.Log(LOG_NORM,"(Sonos) %s Discovered Br[%s] Mod[%s]", upnpdevice->name.c_str(), brand.c_str(), model.c_str());
		upnpdevice->name = name;
		upnpdevice->type = UPNP_TYPE_NULL;			// Not already identified
		upnpdevice->prev_state = UPNP_STATE_STOPPED;
		upnpdevice->restore_state = false;
		upnpdevice->volume = 0;

		/* Add device to list of current devices */
		if (!isInMap) {
			devicesmap.insert(UPnPDevicesMap::value_type(std::string(upnpdevice->id), upnpdevice));
			upnpnamesmap.insert(UPnPNamesMap::value_type(std::string(upnpdevice->udn), upnpdevice->id));
		}

		/* Create User Interface devices for renderer devices */
		if (!isRendererCreated) {
			/* Add a Play/Pause/Volume device for each Sonos speaker */
			thisInstance->UpdateRendererValue(UNIT_Sonos_PlayPause, *upnpdevice, "0", 0);

			/* Add Say and Preset pushbutton for each Sonos speaker */
//			thisInstance->UpdateRendererValue(UNIT_Sonos_Say, *upnpdevice, "0", NO_VOLUME_CHANGE);
//			thisInstance->UpdateRendererValue(UNIT_Sonos_Preset, *upnpdevice, "0", NO_VOLUME_CHANGE);
//			thisInstance->UpdateRendererValue(UNIT_Sonos_Preset+1, *upnpdevice, "0", NO_VOLUME_CHANGE);

			// Add Temp sensor for PLAY:1
			if (upnpdevice->type == UPNP_TYPE_PLAY1) {
				std::string temperature;
				if (upnpdevice->GetPlay1Temperature(temperature) != false)	
					thisInstance->UpdateRendererValue(UNIT_Sonos_TempPlay1, *upnpdevice, temperature, NO_VOLUME_CHANGE);
			}

			/* Add "LastChange" to the list of states we want to be notified about and turn on event subscription */
			GUPnPServiceProxy *av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(info, UPNP_SRV_AV_TRANSPORT));
			if (av_transport != NULL) {
				if (gupnp_service_proxy_add_notify( av_transport,
					"LastChange", G_TYPE_STRING, callbackLastChange,
					NULL))
					gupnp_service_proxy_set_subscribed (av_transport, TRUE);
			} else {
				_log.Log(LOG_ERROR,"(Sonos) No AV_TRANSPORT for %x", longIP);
			}
		}
	} else if (deviceType == UPNP_MEDIASERVER) {
		UPnPDevice *upnpdevice;
		bool isInMap = false;
		bool isServerCreated = false;
		bool isRendererCreated = false;

		/* If device already saved, retrieve it - else create a new one */
		IteratorDevices it = devicesmap.find(deviceID);
		if (it != devicesmap.end()) {
			upnpdevice = (UPnPDevice *)it->second;
			isInMap = true;
			if (upnpdevice->renderer != NULL)
				isRendererCreated = true;
			if (upnpdevice->server != NULL) {
				_log.Log(LOG_NORM,"(Sonos) Discovered %s duplicate server!", upnpdevice->name.c_str());
				isServerCreated = true;
			}
		} else {
			upnpdevice = new(UPnPDevice);
			upnpdevice->renderer = NULL;
		}

		/* Obtain extra device data */
		upnpdevice->server = deviceproxy;
		upnpdevice->ip = longIP;
		upnpdevice->id = deviceID;
		upnpdevice->udn = udn;
		upnpdevice->name = std::string(gupnp_device_info_get_model_name(info));
		upnpdevice->type = UPNP_TYPE_NULL;			// Not already identified
		upnpdevice->prev_state = UPNP_STATE_STOPPED;
		upnpdevice->restore_state = false;
		upnpdevice->volume = 0;

		/* Add device to list of current devices */
		if (!isInMap) {
			devicesmap.insert(UPnPDevicesMap::value_type(std::string(upnpdevice->id), upnpdevice));
			upnpnamesmap.insert(UPnPNamesMap::value_type(std::string(upnpdevice->udn), upnpdevice->id));
		}
	} else if (deviceType == UPNP_BELKINWEMO) {
		DeviceData *upnpdevice;

		IteratorWemos it = wemosmap.find(deviceID);
		if (it != wemosmap.end()) {
			upnpdevice = (DeviceData *)it->second;
			_log.Log(LOG_STATUS,"(Belkin) Discovered %s duplicate WeMo!", upnpdevice->name.c_str());
		} else {
			upnpdevice = new(DeviceData);
			upnpdevice->ip = longIP;
			upnpdevice->type = UPNP_TYPE_NULL;			// Not already identified
			upnpdevice->id = deviceID;
			upnpdevice->udn = udn;
			upnpdevice->name = std::string(gupnp_device_info_get_model_name(info));

			/* Get Belkin Basic service for device */
			upnpdevice->service = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(info, UPNP_SRV_BELKINBASIC));
			upnpdevice->device = deviceproxy;

			/* Add device to list of current devices */
			wemosmap.insert(WemoMap::value_type(std::string(upnpdevice->id), upnpdevice));

			/* Add a On/Off device for each Belkin Wemo */
			thisInstance->UpdateSwitchValue(UNIT_Belkin_OnOff, upnpdevice, "0", NO_LEVEL_CHANGE);
		}
	}

	return;
}

/*+------------------------------------------------------------------------+*/
/*| Callback method that executes every time the timeout expires           |*/
/*| - return true - executes again and again                               |*/
/*+------------------------------------------------------------------------+*/
static gboolean callbackTimeout(void *data)
{
	time_t atime=mytime(NULL);
	struct tm ltime;
	localtime_r(&atime,&ltime);
#ifdef _DEBUG_DOUBLE
	_log.Log(LOG_STATUS,"(Sonos) callbackTimeout ->");
#endif

	// Fetch Temperatures
	for(IteratorDevices it = devicesmap.begin(); it != devicesmap.end(); ++it)
		callbackGetPlay1Temperature(it->second);

	mytime(&thisInstance->m_LastHeartbeat);
#ifdef _DEBUG_DOUBLE
	_log.Log(LOG_NORM,"(Sonos) Heartbeat working");
#endif
	return true;
}

/*+------------------------------------------------------------------------+*/
/*| Utility function to get play1 temperature.                             |*/
/*+------------------------------------------------------------------------+*/
static void callbackGetPlay1Temperature(UPnPDevice *upnpdevice) { 	
	std::string temperature;

	if (upnpdevice->type != UPNP_TYPE_PLAY1) {
		return;
	}

#ifdef _DEBUG
	_log.Log(LOG_STATUS,"(Sonos) callbackGetPlay1Temperature ->");
#endif

	if (upnpdevice->GetPlay1Temperature(temperature) != true) {
		_log.Log(LOG_ERROR,"(Sonos) Get Play:1 name %s", upnpdevice->name.c_str() );	   
		thisInstance->UpdateRendererValue(UNIT_Sonos_TempPlay1, *upnpdevice, temperature, NO_VOLUME_CHANGE);
	}

#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) Get Play:1 name %s Temp %s", upnpdevice->name.c_str(), temperature.c_str());	   
#endif
}

/*+------------------------------------------------------------------------+*/
/*| Utility R trim function.                                               |*/
/*+------------------------------------------------------------------------+*/
char *helperRTrim(char *s)
{
	if (strcmp(s, "") == 0)
		return NULL;

	char* back = s + strlen(s);
	while(isspace(*--back));
	*(back+1) = '\0';
	return s;
}

/*+------------------------------------------------------------------------+*/
/*| Helper function to get UPNP IP from Location.                          |*/
/*+------------------------------------------------------------------------+*/
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


/*+------------------------------------------------------------------------+*/
/*| This is our callback method to handle devices which are removed from   |*/
/*| the network.                                                           |*/
/*+------------------------------------------------------------------------+*/
static void callbackDeviceUnavailable(GUPnPControlPoint *cp, GUPnPDeviceProxy *deviceproxy)
{
	GUPnPDeviceInfo* info  = GUPNP_DEVICE_INFO(deviceproxy);
	int deviceType = -1;

	/* Only allow MediaRenderers and MediaServers */
	char *device_desc = NULL;
	device_desc = (char *)gupnp_device_info_get_device_type (info);
	if (strcmp(device_desc, UPNP_DEVICE_MEDIARENDERER) == 0) {
		deviceType = UPNP_MEDIARENDERER;
	} else if (strcmp(device_desc, UPNP_DEVICE_MEDIASERVER) == 0) {
		deviceType = UPNP_MEDIASERVER;
	} else if (strcmp(device_desc, UPNP_DEVICE_BELKINWEMO) == 0) {
		deviceType = UPNP_BELKINWEMO;
	} else
		return;

	/* Extract IP and add device ip to list of current device ips */
	char szDeviceID[30];
	unsigned long longIP = helperGetIpFromLocation(gupnp_device_info_get_location (info), szDeviceID);
	std::stringstream ss;
	ss << std::uppercase << std::setfill('0') << std::setw(8) << std::hex <<  longIP;
	std::string deviceID(ss.str());

	/* Erase Renderer */
	if ((deviceType == UPNP_MEDIARENDERER) || (deviceType == UPNP_MEDIASERVER)){
		UPnPDevice *upnpdevice;
		IteratorDevices itr;

		itr = devicesmap.find(deviceID);
		if (itr != devicesmap.end()) {
			upnpdevice = (UPnPDevice *)itr->second;
#ifdef _DEBUG
			if (deviceType == UPNP_MEDIARENDERER) {
				if (upnpdevice->name != "")
					_log.Log(LOG_STATUS,"(Sonos) Renderer Unavailable %s", upnpdevice->name.c_str());
				else
					_log.Log(LOG_STATUS,"(Sonos) Renderer Unavailable name-unavailable %s", upnpdevice->id.c_str());
			} else {
				if (upnpdevice->name != "")
					_log.Log(LOG_STATUS,"(Sonos) Server Unavailable %s", upnpdevice->name.c_str());
				else
					_log.Log(LOG_STATUS,"(Sonos) Server Unavailable name-unavailable %s", upnpdevice->id.c_str());
			}
#endif

			/* Remove device from list of current devices */
			if (deviceType == UPNP_MEDIARENDERER)
				upnpdevice->renderer = NULL;
			else if (deviceType == UPNP_MEDIASERVER)
				upnpdevice->server = NULL;

			if ((upnpdevice->server == NULL) && (upnpdevice->renderer == NULL)) {
				_log.Log(LOG_STATUS,"(Sonos) Server&Renderer Unavailable %s", deviceID.c_str());
				devicesmap.erase(deviceID);
				upnpnamesmap.erase(std::string(gupnp_device_info_get_udn(info)));
			}
		}
	} else if (deviceType == UPNP_BELKINWEMO) {
		IteratorWemos itw;
		DeviceData *upnpwemo;

		/* Erase Wemos */
		itw = wemosmap.find(deviceID);
		if (itw != wemosmap.end()) {
			upnpwemo = (DeviceData *)itw->second;
#ifdef _DEBUG
			if (upnpwemo->name != "")
				_log.Log(LOG_STATUS,"(Sonos) Wemo Unavailable %s", upnpwemo->name.c_str());
			else
				_log.Log(LOG_STATUS,"(Sonos) Wemo Unavailable name-unavailable %s", upnpwemo->id.c_str());
#endif

			/* Remove device from list of current devices */
			wemosmap.erase(deviceID);

			// Belkin Wemo devices totally unrelated to Server/Renderers - we can delete them from upnp devices map directly
			upnpnamesmap.erase(std::string(gupnp_device_info_get_udn(info)));
		}
	}
}

/*+------------------------------------------------------------------------+*/
/*| Callback method to print out key details about                         |*/
/*| the current track. This comes as a "DIDL" object.                      |*/
/*+------------------------------------------------------------------------+*/
static void callbackGetTrackInfo(GUPnPDIDLLiteParser *parser,
	GUPnPDIDLLiteObject *object,
	gpointer             pointer) 
{
	SoupURI				*aa_uri;
	SoupURI				*url_base;
	UPnPDevice	*upnpdevice;

	upnpdevice = (UPnPDevice *)pointer;

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
		std::stringstream temp;

		artist = (char*)gupnp_didl_lite_object_get_creator(object);
		if (artist != NULL)
			temp << std::string(artist) << " - ";

		title = (char*)gupnp_didl_lite_object_get_title(object);
		if (title != NULL)
			temp << std::string(title) << " - ";

		album = (char*)gupnp_didl_lite_object_get_album(object);
		if (album != NULL) 
			temp << std::string(album) << " - ";

		album_art = (char*)soup_uri_to_string (aa_uri, FALSE);
		if (album_art != NULL) 
			temp << std::string(album_art);

		upnpdevice->title = std::string(temp.str());
#ifdef _DEBUG
		_log.Log(LOG_STATUS,"(Sonos) Track Info -> %s", upnpdevice->title.c_str());
#endif
	}

	return;
}

/*+----------------------------------------------------------------------------+*/
/*| UPnPDevice class                                                   |*/
/*+----------------------------------------------------------------------------+*/

/*+----------------------------------------------------------------------------+*/
/*| UPnPDevice class.                                                  |*/
/*+----------------------------------------------------------------------------+*/
UPnPDevice::UPnPDevice(void)
{
}

/*+----------------------------------------------------------------------------+*/
/*| UPnPDevice class.                                                  |*/
/*+----------------------------------------------------------------------------+*/
UPnPDevice::~UPnPDevice(void)
{
}

/*+------------------------------------------------------------------------+*/
/*| SonosGetDeviceData                                                     |*/
/*| Get extra information to create the domoticz devices.                  |*/
/*| Here the device name is completed.                                     |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::GetDeviceData(std::string& brand, std::string& model, std::string& name ) {
	UPnPDevice *upnpdevice = this;
	GUPnPDeviceProxy	*renderer = upnpdevice->renderer;

#ifdef _DEBUG_2
	std::string manufacturer( gupnp_device_info_get_manufacturer(GUPNP_DEVICE_INFO(renderer)));
	_log.Log(LOG_NORM,"(Sonos) GetDeviceData Manufacturer \'%s\'", manufacturer.c_str());
#endif

	// Model. Sample: Sonos PLAY:1
	//        Sample: XBMC Media Center
	std::stringstream  stream(gupnp_device_info_get_model_name(GUPNP_DEVICE_INFO(renderer)));
#ifdef _DEBUG_2
	_log.Log(LOG_NORM,"(Sonos) GetDeviceData model-name \'%s\'", stream.str().c_str() );
#endif
	stream >> brand;
	//		_log.Log(LOG_NORM,"(Sonos) GetDeviceData brand \'%s\'", brand.c_str() );
	if (brand.compare(0,5, "Sonos") == 0) {
		upnpdevice->type = UPNP_TYPE_SONOS;
	} else if (brand.compare(0,4, "XBMC") == 0) {
		upnpdevice->type = UPNP_TYPE_XBMC;
	} else if (brand.compare(0,5, "Onkyo") == 0) {
		upnpdevice->type = UPNP_TYPE_ONKYO;
	}

	// Model name.
	stream >> model;
#ifdef _DEBUG_2
	_log.Log(LOG_NORM,"(Sonos) GetDeviceData model \'%s\'", model.c_str());
#endif

	// Name. Sample: Family Office - Sonos ZP100 Media Renderer
	//       Sample: XBMC (pc)
	std::string friendly_name(gupnp_device_info_get_friendly_name(GUPNP_DEVICE_INFO(renderer)));
	int fr_len = friendly_name.length();
#ifdef _DEBUG_2
	_log.Log(LOG_NORM,"(Sonos) GetDeviceData friendly-name \'%s\'", friendly_name.c_str());
#endif
	if (upnpdevice->type == UPNP_TYPE_SONOS) {
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
			upnpdevice->type = UPNP_TYPE_PLAY1;
#endif
	} else {
		name = friendly_name;

		if (upnpdevice->type == UPNP_TYPE_NULL)
			upnpdevice->type = UPNP_TYPE_OTHERS;
	}

	// Store friendly name for each device
	if (upnpdevice->name == "") {
		upnpdevice->name = name;
	}

	// Icon
	char *device_icon = gupnp_device_info_get_icon_url(GUPNP_DEVICE_INFO(renderer),"image/png",32,120,120,1,NULL,NULL, NULL, NULL);
	if (device_icon !=NULL)
		upnpdevice->device_icon = std::string(device_icon);
	else
		upnpdevice->device_icon = "";

#ifdef _DEBUG_2
	_log.Log(LOG_NORM,"(Sonos) GetDeviceData brand [%s] model [%s] name [%s] icon [%s]", brand.c_str(), model.c_str(), name.c_str(), upnpdevice->device_icon.c_str());
#endif
	return true;
}

/*+------------------------------------------------------------------------+*/
/*| UPnP AV Transport Action method.                                       |*/
/*| Action possible values:                                                |*/
/*| - Pause                                                                |*/
/*| - Previous/Next                                                        |*/
/*| - BecomeCoordinatorOfStandaloneGroup                                   |*/
/*|   For linked zones, the commands have to be sent to the coordinator.   |*/
/*|   BecomeCoordinatorOfStandaloneGroup is ~ "Leave Group".               |*/
/*|   To join a group set AVTransportURI to x-rincon:<udn>.                |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::Action(std::string action) {
	UPnPDevice *upnpdevice = this; 
	GError *error = NULL;
	bool success=false;

	/* Get AV_TRANSPORT service for device */
	GUPnPServiceProxy *av_transport;
	av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
		UPNP_SRV_AV_TRANSPORT));
	if (av_transport == NULL) {
		_log.Log(LOG_ERROR,"(Sonos) %s error getting AV_TRANSPORT for %s", action.c_str(), upnpdevice->name.c_str());
		return false;
	}

	/* Send action */
	success = (bool)gupnp_service_proxy_send_action (av_transport, action.c_str(), &error,
		"InstanceID", G_TYPE_UINT, 0, NULL,
		NULL);
	if (!success) {
		if (error) {
			_log.Log(LOG_ERROR,"(Sonos) %s error %d %s", action.c_str(), error->code, error->message);
			g_error_free (error);
		} else 
			_log.Log(LOG_ERROR,"(Sonos) %s error no AV_TRANSPORT", action.c_str());
		return false;
	}

#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) %s success!", action.c_str());
#endif
	return true;
}

/*+------------------------------------------------------------------------+*/
/*| UPnP Action method to play AVT Transport on device.                    |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::Play() {
	UPnPDevice *upnpdevice = this;
	GError *error = NULL;
	bool success=false;

#ifdef _DEBUG_2
	_log.Log(LOG_NORM,"(Sonos) Play ->");
#endif

	/* Get AV_TRANSPORT service for device */
	GUPnPServiceProxy *av_transport;
	av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
		UPNP_SRV_AV_TRANSPORT));
	if (av_transport == NULL) {
		_log.Log(LOG_ERROR,"(Sonos) Play error getting AV_TRANSPORT for %s", upnpdevice->name.c_str());
		return false;
	}

	/* Send action */
	success = (bool)gupnp_service_proxy_send_action (av_transport, "Play", &error,
		"InstanceID", G_TYPE_UINT, 0,
		"Speed", G_TYPE_UINT, 1,
		NULL,
		NULL);
	if (!success) {
		if (error) {
			_log.Log(LOG_ERROR,"(Sonos) Play error %d %s", error->code, error->message);
			g_error_free (error);
		} else
			_log.Log(LOG_ERROR,"(Sonos) Play error no AV_TRANSPORT");
		return false;
	}
#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) Play <- success!");
#endif
	return true;
}

/*+------------------------------------------------------------------------+*/
/*| UPnP AV Action method to set URI to play on Media Renderer.            |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::SetURI(const std::string& uri ) {
	UPnPDevice *upnpdevice=this;
	GError *error;
	bool success=false;
	std::string metadata;

	/* Get AV_TRANSPORT service for device */
	GUPnPServiceProxy *av_transport;
	av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
		UPNP_SRV_AV_TRANSPORT));
	if (av_transport == NULL) {
		_log.Log(LOG_ERROR,"(Sonos) SetURI error getting AV_TRANSPORT for %s", upnpdevice->name.c_str());
		return false;
	}

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
	if (upnpdevice->type == UPNP_TYPE_SONOS) {
		temp << metadata1 << metadata2 << metadata3 << uri << metadata4;
	} else {
		//			temp << metadata1 << metadata3 << uri << metadata4;
		temp << uri;
	}
	metadata = temp.str();

	/* Send action */
	success = (bool)gupnp_service_proxy_send_action( av_transport, "SetAVTransportURI", &error,
		"InstanceID", G_TYPE_UINT, 0,
		"CurrentURI", G_TYPE_STRING, uri.c_str(),
		"CurrentURIMetaData", G_TYPE_STRING, metadata.c_str(), 
		NULL,
		NULL);
	if (!success) {
		if (error) {
			_log.Log(LOG_ERROR,"(Sonos) SetURI Error %d. Msg: %s", error->code, error->message);
			g_error_free (error);
		} else
			_log.Log(LOG_ERROR,"(Sonos) SetURI no AV_TRANSPORT");
		return false;
	}
#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) SetURI Ok playing %s", uri.c_str());
#endif
	return true;
}

/*+------------------------------------------------------------------------+*/
/*| UPnP AV Action method to Get Transport Info for device.                |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::GetTransportInfo(std::string& state) {
	UPnPDevice *upnpdevice = this;
	GError *error = NULL;
	bool success=false;
	char *CurrentTransportState;
	char *CurrentTransportStatus;
	char *CurrentSpeed;

	/* Get AV_TRANSPORT service for device */
	GUPnPServiceProxy *av_transport;
	av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
		UPNP_SRV_AV_TRANSPORT));
	if (av_transport == NULL) {
		_log.Log(LOG_ERROR,"(Sonos) GetTransportInfo error getting AV_TRANSPORT for %s", upnpdevice->name.c_str());
		return false;
	}

	/* Send action */
	success = (bool)gupnp_service_proxy_send_action (av_transport, "GetTransportInfo", &error,
		"InstanceID", G_TYPE_UINT, 0, 
		NULL,
		"CurrentTransportState", G_TYPE_STRING, &CurrentTransportState,
		"CurrentTransportStatus", G_TYPE_STRING, &CurrentTransportStatus,
		"CurrentSpeed", G_TYPE_STRING, &CurrentSpeed,
		NULL);
	if (!success) {
		if (error) {
			_log.Log(LOG_ERROR,"(Sonos) GetTransportInfo Error %d. Msg: %s", error->code, error->message);
			g_error_free (error);
		} else
			_log.Log(LOG_ERROR,"(Sonos) GetTransportInfo no AV_TRANSPORT");
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

/*+------------------------------------------------------------------------+*/
/*| UPnP AV Action method to Get Position Info for device.                 |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::GetPositionInfo(std::string& currenturi) {
	UPnPDevice *upnpdevice=this;
	GError *error = NULL;
	bool success=false;
	char *CurrentTrack;
	char *CurrentTrackDuration;
	char *CurrentTrackMetaData;
	char *CurrentTrackURI;
	int RelativeTimePosition, AbsoluteTimePosition, RelativeCounterPosition, AbsoluteCounterPosition;

	/* Get AV_TRANSPORT service for device */
	GUPnPServiceProxy *av_transport;
	av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
		UPNP_SRV_AV_TRANSPORT));
	if (av_transport == NULL) {
		_log.Log(LOG_ERROR,"(Sonos) GetPositionInfo error getting AV_TRANSPORT for %s", upnpdevice->name.c_str());
		return false;
	}

	/* Send action */
	success = (bool)gupnp_service_proxy_send_action (av_transport, "GetPositionInfo", &error,
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
		if (error) {
			_log.Log(LOG_ERROR,"(Sonos) GetPositionInfo Error %d. Msg: %s", error->code, error->message);
			g_error_free (error);
		} else
			_log.Log(LOG_ERROR,"(Sonos) GetPositionInfo no AV_TRANSPORT");
		return false;
	}

	if (strcmp(CurrentTrackMetaData, "NOT_IMPLEMENTED") == 0) {
		if (strncmp(CurrentTrackURI, "x-rincon:", 9) == 0) {
			//  this means that this zone is a slave to the master or group coordinator with the given id RINCON_xxxxxxxxx
			std::string currentrackuri(CurrentTrackURI);
			std::string zone_coordinator = currentrackuri.substr(9, std::string::npos);
			_log.Log(LOG_NORM,"(Sonos) %S attached to zone coordinator [%s]", upnpdevice->name.c_str(), zone_coordinator.c_str());
			upnpdevice->coordinator = zone_coordinator;
			upnpdevice->source = UPNP_SONOS_SOURCE_MASTER;
		}
	} else {
		upnpdevice->coordinator = "";

		/* Get source */
		/* On Sonos AbsoluteTimePosition is always - NOT_IMPLEMENTED */
		if (strncmp(CurrentTrackURI, "x-sonosapi-stream:", 18) == 0) {
			upnpdevice->source = UPNP_SONOS_SOURCE_LINEIN;
#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) Source LineIn URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
				CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		} else if (strncmp(CurrentTrackURI, "x-sonos-http:", 9) == 0) {
			upnpdevice->source = UPNP_SONOS_SOURCE_HTTP;
#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) Source File/Queue URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
				CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		} else if ((strncmp(CurrentTrackURI, "smb:", 9) == 0) || (strncmp(CurrentTrackURI, "x-file-cifs:", 12) == 0)) {
			upnpdevice->source = UPNP_SONOS_SOURCE_FILE;
#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) Source File/Queue URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
				CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		} else if (strncmp(CurrentTrackURI, "x-rincon-mp3radio:", 18) == 0) {
			upnpdevice->source = UPNP_SONOS_SOURCE_RADIO;
#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) Source Radio URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
				CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		} else {
			upnpdevice->source = UPNP_SONOS_SOURCE_NONE;
#ifdef _DEBUG
			_log.Log(LOG_NORM,"(Sonos) Source other URI[%s] Tr[%s] Dur[%s] RPos[%s] RC[%d] AC[%d]", 
				CurrentTrackURI, CurrentTrack, CurrentTrackDuration, RelativeTimePosition, RelativeCounterPosition, AbsoluteCounterPosition);
#endif
		}
	}

	std::string trackuri(CurrentTrackURI);
	currenturi = trackuri;

	return true;
}

#ifdef BELKIN
/*+------------------------------------------------------------------------+*/
/*| UPnP Action Belkin Wemo - Get Binary State.                            |*/
/*+------------------------------------------------------------------------+*/
int CSonosPlugin::GetBinaryState(DeviceData *upnpdevice) {
	GError *error = NULL;
	gboolean success;

#ifdef _DEBUG_2
	_log.Log(LOG_NORM,"(Belkin) GetBinaryState ->");
#endif

	/* Send action */
	int state = 0;
	success = gupnp_service_proxy_send_action (upnpdevice->service, "GetBinaryState", &error,
		NULL,										
		"BinaryState", G_TYPE_UINT, &state,
		NULL);
	if (!success) {
		_log.Log(LOG_ERROR,"(Belkin) GetBinaryState error %d %s", error, error->message);
		g_error_free (error);
		return -1;
	}
#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Belkin) GetBinaryState <- success!");
#endif
	return state;
}

/*+------------------------------------------------------------------------+*/
/*| UPnP Action Belkin Wemo - Set Binary State.                            |*/
/*+------------------------------------------------------------------------+*/
bool CSonosPlugin::SetBinaryState(DeviceData *upnpdevice, int state) {
	GError *error = NULL;
	gboolean success;

#ifdef _DEBUG_2
	_log.Log(LOG_NORM,"(Belkin) SetBinaryState ->");
#endif

	/* Send action */
	success = gupnp_service_proxy_send_action (upnpdevice->service, "SetBinaryState", &error,				
		"BinaryState", G_TYPE_UINT, state,
		NULL,
		NULL);
	if (!success) {
		_log.Log(LOG_ERROR,"(Belkin) SetBinaryState error %d %s", error, error->message);
		g_error_free (error);
		return false;
	}
#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Belkin) SetBinaryState <- success!");
#endif
	return true;
}
#endif


/*+------------------------------------------------------------------------+*/
/*| RENDERING CONTROL methods                                              |*/
/*+------------------------------------------------------------------------+*/

/*+------------------------------------------------------------------------+*/
/*| UPnP Action method to get volume from device.                          |*/
/*+------------------------------------------------------------------------+*/
int UPnPDevice::GetVolume() {
	UPnPDevice *upnpdevice = this;

	/* Get rendering control service for device */
	GUPnPServiceProxy *rendering_control;
	rendering_control = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
		UPNP_SRV_RENDERING_CONTROL));
	if (rendering_control == NULL) {
		_log.Log(LOG_ERROR,"(Sonos) GetVolume error getting rendering control for device %s", upnpdevice->name.c_str());
		return NO_VOLUME_CHANGE;
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
		return NO_VOLUME_CHANGE;
	}
#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) GetVolume Current volume=%d for %s", volume, upnpdevice->name.c_str());
#endif
	return(volume);
}

/*+------------------------------------------------------------------------+*/
/*| UPnP Action method to set volume on device.                            |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::SetVolume(int level) {
	UPnPDevice *upnpdevice = this;

	/* Get rendering control service for device */
	GUPnPServiceProxy *rendering_control;
	rendering_control = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
		UPNP_SRV_RENDERING_CONTROL));
	if (rendering_control == 0) {
		_log.Log(LOG_ERROR,"(Sonos) SetVolume error getting rendering control for device %s", 
			upnpdevice->name.c_str());
		return false;
	}

	/* SetVolume */
	GError *error = NULL;
	bool success;
	int volume=0;

	if (upnpdevice->type == UPNP_TYPE_SONOS) 
		volume = (int)((level*100) / 15);
	else
		volume = level;

	if (volume > 100)
		volume = 100;

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

/*+------------------------------------------------------------------------+*/
/*| UPnP Action method to browse Content Directory.                        |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::LoadQueue(std::string& sURL) {
	UPnPDevice *upnpdevice = this;

	GUPnPServiceProxy *content_directory;
	GUPnPDeviceInfo *info = GUPNP_DEVICE_INFO(upnpdevice->server);

	/* Get content directory service for device */
	GUPnPServiceInfo *proxy = gupnp_device_info_get_service(info, UPNP_SRV_CONTENT_DIRECTORY);
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
		"ObjectID", G_TYPE_STRING, "SQ:Domoticz",		// SQ:24
		"BrowseFlag", G_TYPE_STRING, "BrowseMetadata",
		"Filter", G_TYPE_STRING, NULL,
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
		_log.Log(LOG_ERROR,"(Sonos) CD Browse error %d %s", error, error->message);
		g_error_free (error);
		return false;
	}

	// Save what's in Result <res> </res> and pass this as the URI
	sURL = std::string(result);
#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) CD Browse %s NR[%d] TM[%d] UID[%d] \n------------------\nresult: %s\n-------------------", 
		upnpdevice->id.c_str(), NumberReturned, TotalMatches, UpdateID, result );
#endif

	// Delete queue SQ:24

	// See http://forum.micasaverde.com/index.php?topic=13842.0

	return(true);
}

/*+------------------------------------------------------------------------+*/
/*| UPnP Action method to save queue.                                      |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::SaveQueue() {
	UPnPDevice *upnpdevice = this;

	GError *error = NULL;
	bool success=false;

	/* Get AV_TRANSPORT service for device */
	GUPnPServiceProxy *av_transport;
	av_transport = GUPNP_SERVICE_PROXY (gupnp_device_info_get_service(GUPNP_DEVICE_INFO(upnpdevice->renderer), 
		UPNP_SRV_AV_TRANSPORT));
	if (av_transport == NULL) {
		_log.Log(LOG_ERROR,"(Sonos) SaveQueue error getting AV_TRANSPORT for %s", upnpdevice->name.c_str());
		return false;
	}

	/* Send action */
	success = (bool)gupnp_service_proxy_send_action (av_transport, "SaveQueue", &error,
		"InstanceID", G_TYPE_UINT, 0, 
		"Title", G_TYPE_STRING, SONOS_SAVED_QUEUE_NAME, 
		"ObjectID", G_TYPE_STRING, NULL,
		NULL,
		NULL);
	if (!success) {
		if (error) {
			_log.Log(LOG_ERROR,"(Sonos) SaveQueue error %d %s", error->code, error->message);
			g_error_free (error);
		} else
			_log.Log(LOG_ERROR,"(Sonos) SaveQueue error");
		return false;
	}
#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) SaveQueue for %s", upnpdevice->name.c_str());
#endif
	return true;
}

/*+------------------------------------------------------------------------+*/
/*| OTHER ACTIONS                                                          |*/
/*+------------------------------------------------------------------------+*/

/*+------------------------------------------------------------------------+*/
/*| getPlay1Temperature.                                                   |*/
/*| See http://www.hifi-forum.de/viewthread-100-623-4.html.                |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::GetPlay1Temperature(std::string& temperature)
{
	UPnPDevice *upnpdevice = this;
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

/*+------------------------------------------------------------------------+*/
/*| UPnP Action method to say text to speech using Google translate.       |*/
/*+------------------------------------------------------------------------+*/
bool UPnPDevice::Say(const std::string& sText, std::string& url, int type) {
	std::string sResult, sPlainText;
	bool bret;
	CURLEncode curl;
	int method = 0;

	// Method for publishing text transformed to speech
	if (sText.compare(0, 2, "0-") == 0) {
		// Text prepended with "0-" means direct Google Translate mp3 URL
		method = 0;
		sPlainText = sText.substr(2,std::string::npos);		

#ifdef _DEBUG
		_log.Log(LOG_NORM,"(Sonos) Say direct");
#endif
	} else if (sText.compare(0, 2, "1-") == 0) {
		// Text prepended with "1-" means Domoticz-served mp3 through URL
		method = 1;
		sPlainText = sText.substr(2,std::string::npos);		
	} else if (sText.compare(0, 2, "2-") == 0) {
		// Text prepended with "2-" means m3u file served by Domoticz, but directs to Google URL
		method = 2;
		sPlainText = sText.substr(2,std::string::npos);		
	} else {
		// No "n-" prefix means Domoticz-served URL
		method = 1;
		sPlainText = sText;
	}

	// Sample url: http://translate.google.com/translate_tts?ie=UTF-8&q=Welcome%20to%20the%20Domoticz%20home&tl=en
	std::stringstream ssURL("");
	//		ssURL << "http://translate.google.com/translate_tts?ie=UTF-8&q=" << curl.URLEncode(sPlainText) << "&tl=" << m_ttsLanguage;		
	ssURL << "http://translate.google.com/translate_tts?q=" << curl.URLEncode(sPlainText) << "&tl=" << m_ttsLanguage;		
	std::string sURLTTS(ssURL.str());

#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) Say TTS URL %s", sURLTTS.c_str());
#endif

	if (method == 0) {
		// --------------------------------------------------------------------
		// METHOD 0: Don't store anything... Just return Google URL
		// SONOS: ok from Sonos manager creating a radio with http:// - on logs has been transformed to x-rincon-mp3radio
		// XBMC: ERROR: Sink DIRECTSOUND:{5164DEFA-341A-4CF8-A514-59DA4A1E915B} returned invalid buffer size: 240

		//			ssURL.clear();
		//			ssURL.str("");
		//			ssURL << "stack://" << sURLTTS;
		url = sURLTTS;
		//		} else if (method == 2) {
		// --------------------------------------------------------------------
		// METHOD 2: Save the Google URL to a M3U file
		// XBMC: ERROR: Playlist Player: skipping unplayable item: 0, path [http://X.X.X.X:8888/media/tts_text.mp3.m3u]

		// std::ofstream myfile;
		// myfile.open(filepath.c_str());
		// myfile << sURLTTS;
		// myfile.close();
		// url = ssURL.str();
	} else {
		// --------------------------------------------------------------------
		// METHOD 1: Downloaded audio for text and serve it through the domoticz webserver
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
	}  

#ifdef _DEBUG
	_log.Log(LOG_NORM,"(Sonos) Say success (%s)!", url.c_str());
#endif
	return true;
}
#endif

/*+------------------------------------------------------------------------+*/
/*| List all devices stored in maps. Debug purposes.                       |*/
/*+------------------------------------------------------------------------+*/
bool CSonosPlugin::ListMaps(void)
{ 
	int i=0;

	// Fetch MediaDevices
	for(IteratorDevices itr = devicesmap.begin(); itr != devicesmap.end(); ++itr) {
		std::string key = (std::string)itr->first;
		UPnPDevice *upnpdevice = (UPnPDevice *)itr->second;
		_log.Log(LOG_NORM,"(Sonos) Device %i id=%s Name=%s", ++i, key.c_str(), upnpdevice->name.c_str());

		// List renderer services available
		GUPnPDeviceInfo* info  = GUPNP_DEVICE_INFO(upnpdevice->renderer);
		GList *services, *s;
		int j;
		
		services = gupnp_device_info_list_service_types (info);
		for (s = services, j = 0; s; s = s->next, j++) {
			char *type = (char *)s->data;	 	
			_log.Log(LOG_STATUS,"(Sonos) Renderer %i service [%d]: %s", i, j, type);

			g_free (type);
		}
		g_list_free (services);

		// List server services available
		info  = GUPNP_DEVICE_INFO(upnpdevice->server);
		services = gupnp_device_info_list_service_types (info);
		for (s = services, j = 0; s; s = s->next, j++) {
			char *type = (char *)s->data;	 	
			_log.Log(LOG_STATUS,"(Sonos) Server %i service [%d]: %s", i, j, type);

			g_free (type);
		}
		g_list_free (services);
	}

	// Fetch MediaDevice udn / id
	i=0;
	for(IteratorNames itmd = upnpnamesmap.begin(); itmd != upnpnamesmap.end(); ++itmd) {
		std::string key = (std::string)itmd->first;
		std::string sip = (std::string)itmd->second;
		_log.Log(LOG_NORM,"(Sonos) UPnPDevice %i udn=%s id=%s", ++i, key.c_str(), sip.c_str());
	}

	return true;
}

/*+------------------------------------------------------------------------+*/
/*| Erase all devices stored in maps and maps.                             |*/
/*+------------------------------------------------------------------------+*/
bool CSonosPlugin::EraseMaps(void)
{ 
	// Fetch Media Server / Renderer Devices
	for(IteratorDevices itr = devicesmap.begin(); itr != devicesmap.end(); ++itr) {
		delete itr->second;
		itr = devicesmap.erase(itr);
	}

	// Fetch Switches / Wemos
	for(IteratorWemos itw = wemosmap.begin(); itw != wemosmap.end(); ++itw) {
		delete itw->second;
		itw = wemosmap.erase(itw);
	}

	// Fetch Names
	for(IteratorNames itmd = upnpnamesmap.begin(); itmd != upnpnamesmap.end(); ++itmd) {
		itmd = upnpnamesmap.erase(itmd);
	}


	return true;
}

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
	cpmr = gupnp_control_point_new(context, UPNP_DEVICE_MEDIARENDERER);	 // "upnp:rootdevice");	//   // UPNP_DEVICES_ALL    
	cpms = gupnp_control_point_new(context, UPNP_DEVICE_MEDIASERVER);	 
	cpbw = gupnp_control_point_new(context, UPNP_DEVICE_BELKINWEMO);	 

	/* The device-proxy-available signal is emitted when target devices are found - connect to it */
	g_signal_connect (cpmr, "device-proxy-available", G_CALLBACK (callbackDeviceDiscovered), NULL);
	g_signal_connect (cpms, "device-proxy-available", G_CALLBACK (callbackDeviceDiscovered), NULL);
	g_signal_connect (cpbw, "device-proxy-available", G_CALLBACK (callbackDeviceDiscovered), NULL);

	/* The device-proxy-unavailable signal is emitted when target devices are removed - connect to it */
	g_signal_connect( cpmr, "device-proxy-unavailable", G_CALLBACK (callbackDeviceUnavailable), NULL);
	g_signal_connect( cpms, "device-proxy-unavailable", G_CALLBACK (callbackDeviceUnavailable), NULL);
	g_signal_connect( cpbw, "device-proxy-unavailable", G_CALLBACK (callbackDeviceUnavailable), NULL);

	/* Tell the Control Points to start searching */
	gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (cpmr), TRUE);
	gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (cpms), TRUE);
	gssdp_resource_browser_set_active (GSSDP_RESOURCE_BROWSER (cpbw), TRUE);

	/* Set a timeout of RUN_TIME seconds to do some things */
	g_timeout_add_seconds( RUN_TIME, callbackTimeout, NULL);

	/* Enter the main loop. This will start the search and result in callbacks to
	callbackDeviceDiscovered and callbackDeviceUnavailable. 
	This call will run forever waiting for new devices to appear*/
	main_loop = g_main_loop_new (NULL, FALSE);
	g_main_loop_run (main_loop);

	/* Clean up */
	_log.Log(LOG_NORM,"(Sonos) SonosInit clean devices");     
	EraseMaps();

	_log.Log(LOG_NORM,"(Sonos) SonosInit clean main loop");     
	g_main_loop_unref (main_loop);
	g_object_unref (cpmr);
	g_object_unref (cpms);
	g_object_unref (cpbw);
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

			<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>
			<u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
			<InstanceID>0</InstanceID>,<CurrentURI></CurrentURI>,<CurrentURIMetaData></CurrentURIMetaData></u:SetAVTransportURI>
			</s:Body></s:Envelope>0x0D0x0A0x0D0x0A	


			Group to zone
			First check <your sonos ip>:1400/status/topology the UUID of your main zone, e.g: RINCON_000E58226B1601400

Code:	
POST /MediaRenderer/AVTransport/Control HTTP/1.1
	Content-Length: 377
SOAPACTION: "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI"

			<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>
			<u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
			<InstanceID>0</InstanceID>,<CurrentURI>x-rincon:RINCON_000E58226B1601400</CurrentURI>,<CurrentURIMetaData></CurrentURIMetaData>
			</u:SetAVTransportURI>
			</s:Body>
			</s:Envelope>0x0D0x0A0x0D0x0A	

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

	HINT 5:
#endif

#ifdef _SNAPSHOT
/*+----------------------------------------------------------------------------+*/
/*| https://raw.githubusercontent.com/SoCo/SoCo/master/soco/snapshot.py        |*/
/*
Class to support snap-shotting the current Sonos State, and then
restoring it later

Note: This does not change anything to do with the configuration
such as which group the speaker is in, just settings that impact
what is playing, or how it is played.

List of sources that may be playing using root of media_uri:
'x-rincon-queue': playing from Queue
'x-sonosapi-stream': playing a stream (eg radio)
'x-file-cifs': playing file
'x-rincon': slave zone (only change volume etc. rest from coordinator) */
/*+----------------------------------------------------------------------------+*/

/*+----------------------------------------------------------------------------+*/
/*| CSonosShapshot class.                                                      |*/
/*+----------------------------------------------------------------------------+*/
CSonosShapshot::CSonosShapshot( DeviceData *upnpdevice, bool snapshot_queue=False) {
	/* Construct the Snapshot object

	:params device: Device to snapshot
	:params snapshot_queue: If the queue is to be snapshotted

	:return is_coordinator (Boolean)- tells users if to play alert
	playing an alert on a slave will un group it!

	Note: It is strongly advised that you do not snapshot the
	queue unless you really need to as it takes a very long
	time to restore large queues as it is done one track at
	a time
	*/

	// The device that will be snapshotted
	this.device = upnpdevice;

	// The values that will be stored
	// For all zones:
	this.media_uri= "";
	this.is_coordinator = False
		this.is_playing_queue = False

		this.volume= 0;
	this.mute= 0;
	this.bass= 0;
	this.treble= 0;
	this.loudness= 0;

	// For coordinator zone playing from Queue:
	this.play_mode= 0;
	this.cross_fade= 0;
	this.playlist_position= 0;
	this.track_position= 0;

	// For coordinator zone playing a Stream:
	this.media_metadata= 0;

	// For all coordinator zones
	this.transport_state= 0;

	this.queue= 0;

	// Only set the queue as a list if we are going to save it
	if (snapshot_queue) {
		// Create queue
		//		this.queue = []
	}
}

/*+----------------------------------------------------------------------------+*/
/*| CSonosShapshot class destructor.                                           |*/
/*+----------------------------------------------------------------------------+*/
CSonosShapshot::~CSonosShapshot(void)
{
}

/*+----------------------------------------------------------------------------+*/
/*| Snapshot - save state                                                      |*/
/*+----------------------------------------------------------------------------+*/
void CSonosShapshot::Snapshot(void) {
	/* 
	* Record and store the current state of a device
	*/

	// Get information about the currently playing media
	media_info = this.device.avTransport.GetMediaInfo([('InstanceID', 0)])
		this.media_uri = media_info['CurrentURI']

	// extract source from media uri
	if this.media_uri.split(':')[0] != 'x-rincon':
	this.is_coordinator = true;
	if this.media_uri.split(':')[0] == 'x-rincon-queue':
	this.is_playing_queue = true;

	// Save the volume, mute and other sound settings
	this.volume = this.device.volume
		this.mute = this.device.mute
		this.bass = this.device.bass
		this.treble = this.device.treble
		this.loudness = this.device.loudness

		// get details required for what's playing:
		if this.is_playing_queue:
	// playing from queue - save repeat, random, cross fade, track, etc.
	this.play_mode = this.device.play_mode
		this.cross_fade = this.device.cross_fade

		// Get information about the currently playing track
		track_info = this.device.get_current_track_info()
		if track_info is not None:
	position = track_info['playlist_position'];
	if (position != "") {
		// save as integer
		this.playlist_position = int(position)
			this.track_position = track_info['position']
	} else {
		// playing from a stream - save media metadata
		this.media_metadata = media_info['CurrentURIMetaData']
	}
	// Work out what the playing state is - if a coordinator
	if (this.is_coordinator)
		transport_info = this.device.get_current_transport_info();

	if (transport_info != 0)
		this.transport_state = transport_info['current_transport_state'];

	// Save of the current queue if we need to
	this._save_queue()

		// return if device is a coordinator (helps usage)
		return this.is_coordinator
}

/*+----------------------------------------------------------------------------+*/
/*| Restore - restore state                                                    |*/
/*+----------------------------------------------------------------------------+*/
void CSonosShapshot::Restore(int fade) {
	/* Restores the state of a device that was previously saved
	* For coordinator devices restore everything
	* For slave devices only restore volume etc. not transport info
	* (transport info comes from the slaves coordinator).
	*/

	// Start by ensuring that the speaker is paused as we don't want
	// things all rolling back when we are changing them, as this could
	// include things like audio
	if (this.is_coordinator)
		transport_info = this.device.get_current_transport_info();

	if (transport_info != 0)
		if transport_info['current_transport_state'] == 'PLAYING':
	this.device.pause()

		// Check if the queue should be restored
		this._restore_queue()

		// Reinstate what was playing
		if (this.is_playing_queue && this.playlist_position > 0) {

			// was playing from playlist
			if this.playlist_position is not None:

			// The position in the playlist returned by
			// get_current_track_info starts at 1, but when
			// playing from playlist, the index starts at 0
			// if position > 0:
			this.playlist_position -= 1
				this.device.play_from_queue(this.playlist_position, False)

				if this.track_position is not None:
			if this.track_position != "":
			this.device.seek(this.track_position);

			// reinstate track, position, play mode, cross fade
			// Need to make sure there is a proper track selected first
			this.device.play_mode = this.play_mode;
			this.device.cross_fade = this.cross_fade;
		} else {
			// was playing a stream (radio station, file, or nothing)
			// reinstate uri and meta data
			if this.media_uri != "":
			this.device.play_uri(this.media_uri, this.media_metadata, start=False);

			// For all devices:
			// Reinstate all the properties that are pretty easy to do
			this.device.mute = this.mute
				this.device.bass = this.bass
				this.device.treble = this.treble
				this.device.loudness = this.loudness

				// Reinstate volume
				// Can only change volume on device with fixed volume set to False
				// otherwise get uPnP error, so check first. Before issuing a network
				// command to check, fixed volume always has volume set to 100.
				// So only checked fixed volume if volume is 100.
				if (this.volume == 100)
					fixed_vol = this.device.renderingControl.GetOutputFixed([('InstanceID', 0)])['CurrentFixed'];
				else
					fixed_vol = False;

			// now set volume if not fixed
			if not fixed_vol:
			if fade:
			// if fade requested in restore
			// set volume to 0 then fade up to saved volume (non blocking)
			this.device.volume = 0
				this.device.renderingControl.RampToVolume([('InstanceID', 0), ('Channel', 'Master'),
				('RampType', 'SLEEP_TIMER_RAMP_TYPE'),
				('DesiredVolume', this.volume),
				('ResetVolumeAfter', False), ('ProgramURI', '')])
			else
			// set volume
			this.device.volume = this.volume

			// Now everything is set, see if we need to be playing, stopped
			// or paused ( only for coordinators)
			if this.is_coordinator:
			if this.transport_state == 'PLAYING':
			this.device.play()
				elif this.transport_state == 'STOPPED':
			this.device.stop()
		}

		/*+----------------------------------------------------------------------------+*/
		/*| SaveQueue                                                                  |*/
		/*+----------------------------------------------------------------------------+*/
		void CSonosShapshot::SaveQueue(void) {
			/* 
			* Saves the current state of the queue
			*/
			if this.queue is not None:

			// Maximum batch is 486, anything larger will still only
			// return 486
			batch_size = 400
				total = 0
				num_return = batch_size

				// Need to get all the tracks in batches, but Only get the next
				// batch if all the items requested were in the last batch
				while num_return == batch_size:
			queue_items = this.device.get_queue(total, batch_size)
				// Check how many entries were returned
				num_return = len(queue_items)
				// Make sure the queue is not empty
				if num_return > 0:
			this.queue.append(queue_items)
				// Update the total that have been processed
				total = total + num_return
		}

		void CSonosShapshot::RestoreQueue(void) {
			/* Restores the previous state of the queue

			Note: The restore currently adds the items back into the queue
			using the URI, for items the Sonos system already knows about
			this is OK, but for other items, they may be missing some of
			their metadata as it will not be automatically picked up
			*/
			if this.queue is not None:
			// Clear the queue so that it can be reset
			this.device.clear_queue()
				// Now loop around all the queue entries adding them
				for queue_group in this.queue:
			for queue_item in queue_group:
			this.device.add_uri_to_queue(queue_item.uri)
		}
#endif

