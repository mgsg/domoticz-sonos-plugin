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
#pragma once
//#include "../main/RFXtrx.h" 
#include "DomoticzHardware.h"

#if defined __linux__
#include <glib-2.0/glib.h>
#include <libgupnp/gupnp-control-point.h>
#include <libgupnp-av/gupnp-av.h>
#include <libsoup/soup.h>
#endif

class UPnPDevice {
public:
	unsigned long		ip;					// Long conversion of IP address
	unsigned char		type;				// Type of device - Sonos, UPnP,...
	unsigned char		volume;				// The speaker's volume
	unsigned char		prev_state;			// Previous state
	unsigned char		source;				// Source of media being played
	bool				restore_state;		// State restore pending from previous interruption

	bool				mute;				// The speaker's mute status
	unsigned char		bass;				// The speaker's bass EQ
	unsigned char		treble;				// The speaker's treble EQ
	unsigned char		loudness;			// The status of the speaker's loudness compensation
	unsigned char		cross_fade;			// The status of the speaker's crossfade
	unsigned char		status_light;		// The state of the Sonos status light
	unsigned char		play_mode;			// The queue's repeat/shuffle settings
	unsigned char		queue_size;			// Get size of queue

	std::string			id;					// String representation of long conversion of IP address
	std::string			udn;				// uid -- The speaker's unique identifier
	std::string			name;				// player_name  -- The speaker's name

	std::string			prev_uri;			// Previously played uri - to be moved to snapshot class???
	std::string			coordinator;		// Zone Coordinator uri/udn

	std::string			title;				// Currently played title
	std::string			artist;				// Currently played artist
	std::string			album;				// Currently played album
	std::string			album_art;			// Currently played album_art
	std::string			device_icon;		// Device icon

	// Interface defined public constructor and methods
	UPnPDevice(void);
	~UPnPDevice(void);

	bool GetDeviceData(std::string& brand, std::string& model, std::string& name );

	// AV specific public methods/actions
	bool Action(std::string action);
	bool Play();
	bool SetURI(const std::string& uri );
	bool GetPositionInfo(std::string& currenturi);
	bool GetTransportInfo(std::string& state);
	bool LeaveGroup();

	// Rendering Control specific public methods/actions
	bool SetVolume(int volume);
	int  GetVolume();

	// Sonos non-UPnP
	bool GetPlay1Temperature(std::string& temperature);
	bool Say(const std::string& tts, std::string& url, int type);

	// Sonos UPnP Content Directory
	bool LoadQueue(std::string& sURL);

#if defined __linux__
	GUPnPDeviceProxy	*renderer;
	GUPnPDeviceProxy	*server;
#endif

private:
	// Renderer methods
	bool SaveQueue();
};

typedef struct __DeviceData {
	unsigned long		ip;
	int					type;
	unsigned char		level;
	std::string			id;
	std::string			udn;
	std::string			name;
#if defined __linux__
	GUPnPDeviceProxy	*device;
	GUPnPServiceProxy   *service;
#endif
} DeviceData;

#if defined WIN32 
// Sorry No WIN32 support yet
#endif

class CSonosPlugin : public CDomoticzHardwareBase
{
public:
	// Interface defined public constructor and methods
	CSonosPlugin(const int);
	~CSonosPlugin(void);

	// Domoticz-->Hardware
	void WriteToHardware(const char *pdata, const unsigned char length);
	
	// Update Renderer Value in Domoticz
	void UpdateRendererValue( int qType, UPnPDevice& upnpdevice, const std::string& devValue, int volume );
	void UpdateSwitchValue( int qType, DeviceData *upnpdevice, const std::string& devValue, int level);

	// Sonos UPnP Belkin WeMo actions @@@
#ifdef BELKIN
	int GetBinaryState(DeviceData *upnpdevice);
	bool SetBinaryState(DeviceData *upnpdevice, int state);
#endif

private:
	bool								m_bEnabled;
	int									hwId;
	double								m_lastquerytime;
	volatile bool						m_stoprequested;
	boost::shared_ptr<boost::thread>	m_thread;

	// Interface defined methods
	void Init();
	bool StartHardware();
	bool StopHardware();
	void Do_Work();	
    bool ListMaps(void);
    bool EraseMaps(void);
    void SonosInit(void);

	// Sonos specific private methods
#if defined WIN32
	// Sorry No WIN32 support yet

#elif defined __linux__
	// GUPnP internal state
	GUPnPContext						*context;
	GUPnPControlPoint					*cpmr;
	GUPnPControlPoint					*cpms;
	GUPnPControlPoint					*cpbw;
#endif
};

#ifdef _SNAPSHOT
class CSonosSnapshot : public Object
{
public:
	// Interface defined public constructor and methods
	CSonosSnapshot(const int);
	~CSonosSnapshot(void);

    void Init( DeviceData *upnpdevice, bool snapshot_queue=False);
	void Snapshot(void);
	void Restore(int fade);
	void SaveQueue(void);
	void RestoreQueue(void);

private:
	// The device that will be snapshotted
	DeviceData				*upnpdevice;

	// The values that will be stored
	// For all zones:
	std::string				media_uri;		
	bool					is_coordinator;
	bool					is_playing_queue;	
	unsigned char			volume;
	unsigned char			mute;	
	unsigned char			bass;   
	unsigned char			treble; 
	unsigned char			loudness;

	// For coordinator zone playing from Queue:
	unsigned char			play_mode; 
	unsigned char			cross_fade;
	unsigned char			playlist_position;
	unsigned char			track_position;  

	// For coordinator zone playing a Stream:
	std::string				media_metadata; 

	// For all coordinator zones
	std::string				transport_state;
//	queue;   // None

	// Only set the queue as a list if we are going to save it
//	queue = []
};
#endif