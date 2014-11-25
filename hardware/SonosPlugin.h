#pragma once
//#include "../main/RFXtrx.h" 
#include "DomoticzHardware.h"

#if defined __linux__
#include <glib-2.0/glib.h>
#include <libgupnp/gupnp-control-point.h>
#include <libgupnp-av/gupnp-av.h>
#include <libsoup/soup.h>

typedef struct __DeviceSessionData {
	unsigned long		ip;
	int					type;
	unsigned char		level;
	int					prev_state;
	std::string			id;
	std::string			udn;
	std::string			name;
	std::string			prev_uri;	
	std::string			coordinator;
//	char				*id;
//	char				*udn;
//	char				*name;
//	char				*prev_uri;	
//	char				*coordinator;
	GUPnPDeviceProxy	*renderer;
} DeviceSessionData;
#endif

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
	
	// Hardware-->Domoticz
	void UpdateValueEasy(int qType, const std::string& devId, const std::string& devName, const std::string& devValue, int level );
	
	// Sonos UPnP AV specific public methods/actions
	bool SonosActionPause(GUPnPServiceProxy *av_transport);
	bool SonosActionNext(GUPnPServiceProxy *av_transport);
	bool SonosActionPrevious(GUPnPServiceProxy *av_transport);
	bool SonosActionPlay(GUPnPServiceProxy *av_transport);
	bool SonosActionSetURI(GUPnPServiceProxy *av_transport, const std::string& uri, int type);
	bool SonosActionSetNextURI(GUPnPServiceProxy *av_transport, const std::string& uri, int type);
	bool SonosActionGetPositionInfo(GUPnPServiceProxy *av_transport, std::string& currenturi);
	bool SonosActionGetTransportInfo(GUPnPServiceProxy *av_transport, std::string& state);

	// Sonos UPnP Rendering Control specific public methods/actions
	bool SonosActionSetVolume(const std::string& devID, int volume);
	int  SonosActionGetVolume(const std::string& devID);

	// Sonos other UPnP methods
	bool SonosGetRendererAVTransport(const std::string& deviceID, DeviceSessionData **upnprenderer, GUPnPServiceProxy **av_transport);
	bool SonosGetDeviceData(DeviceSessionData *upnprenderer, std::string& brand, std::string& model, std::string& name );

	// Sonos non-UPnP
	bool SonosActionSay(const std::string& tts, std::string& url, int type);
	bool SonosActionGetPlay1Temperature(const std::string& devID, std::string& temperature);

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

	// Sonos specific private methods
#ifdef WIN32
// Sorry No WIN32 support yet
#elif defined __linux__
    void SonosInit(void);
#endif
};

