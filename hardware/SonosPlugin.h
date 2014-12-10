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

typedef struct __RendererDeviceData {
	unsigned long		ip;
	unsigned char		type;
	unsigned char		volume;
	unsigned char		prev_state;
	unsigned char		protocol;
	bool				restore_state;
	std::string			id;
	std::string			udn;
	std::string			name;
	std::string			prev_uri;	
	std::string			coordinator;

#if defined __linux__
	GUPnPDeviceProxy	*renderer;
	GUPnPServiceProxy   *av_transport;
	GUPnPServiceProxy   *rendering_control;
#endif
} RendererDeviceData;

typedef struct __ServerDeviceData {
	unsigned long		ip;
	int					type;
	unsigned char		volume;
	std::string			id;
	std::string			udn;
	std::string			name;
#if defined __linux__
	GUPnPDeviceProxy	*server;
	GUPnPServiceProxy   *av_transport;
#endif
} ServerDeviceData;

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
	void UpdateRendererValue( int qType, RendererDeviceData *upnpdevice, const std::string& devValue, int volume );
	void UpdateSwitchValue( int qType, DeviceData *upnpdevice, const std::string& devValue, int level);

	// Sonos UPnP AV specific public methods/actions
	bool SonosActionPause(RendererDeviceData *upnpdevice);
	bool SonosActionNext(RendererDeviceData *upnpdevice);
	bool SonosActionPrevious(RendererDeviceData *upnpdevice);
	bool SonosActionPlay(RendererDeviceData *upnpdevice);
	bool SonosActionSetURI(RendererDeviceData *upnpdevice, const std::string& uri );
	bool SonosActionGetPositionInfo(RendererDeviceData *upnpdevice, std::string& currenturi);
	bool SonosActionGetTransportInfo(RendererDeviceData *upnpdevice, std::string& state);
	bool SonosActionLeaveGroup(RendererDeviceData *upnpdevice);

	// Sonos UPnP Rendering Control specific public methods/actions
	bool SonosActionSetVolume(RendererDeviceData *upnpdevice, int volume);
	int  SonosActionGetVolume(RendererDeviceData *upnpdevice);

	// Sonos UPnP Belkin WeMo actions @@@
#ifdef BELKIN
	int SonosActionGetBinaryState(DeviceData *upnpdevice);
	bool SonosActionSetBinaryState(DeviceData *upnpdevice, int state);
#endif

	// Sonos UPnP Content Directory
	bool SonosActionSaveQueue(RendererDeviceData *upnpdevice);
	bool SonosActionLoadQueue(ServerDeviceData *upnpdevice, std::string& sURL);

	// Sonos other UPnP methods
	bool SonosGetRenderer(const std::string& deviceID, RendererDeviceData **upnpdevice);
	bool SonosGetDeviceData(RendererDeviceData *upnpdevice, std::string& brand, std::string& model, std::string& name );

	// Sonos non-UPnP
	bool SonosActionSay(const std::string& tts, std::string& url, int type);
	bool SonosActionGetPlay1Temperature(RendererDeviceData *upnpdevice, std::string& temperature);

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
    void SonosInit(void);

	// Sonos specific private methods
#if defined WIN32
	// Sorry No WIN32 support yet

#elif defined __linux__
	// GUPnP internal state
	GUPnPContext *context;
	GUPnPControlPoint *cpmr, *cpms, *cpbw;
#endif
};

