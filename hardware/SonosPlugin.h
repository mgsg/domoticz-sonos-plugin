#pragma once
//#include "../main/RFXtrx.h" 
#include "DomoticzHardware.h"

#if defined __linux__
#include <glib-2.0/glib.h>
#include <libgupnp/gupnp-control-point.h>
#include <libgupnp-av/gupnp-av.h>
#include <libsoup/soup.h>

typedef struct __DeviceSessionData {
	char				*id;
	char				*udn;
	unsigned long		ip;
	int					type;
	GUPnPDeviceProxy	*renderer;
	unsigned char		level;
	int					prev_state;
	char				*prev_uri;	
	std::string			coordinator;
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
	bool SonosActionPause(const std::string& devID);
	bool SonosActionPlay(const std::string& devID);
	bool SonosActionPlayURI(const std::string& devID, const std::string& uri);
	bool SonosActionGetPositionInfo(const std::string& deviceID);
	bool SonosGetRendererAVTransport(const std::string& deviceID, DeviceSessionData **upnprenderer, GUPnPServiceProxy **av_transport);

	// Sonos UPnP Rendering Control specific public methods/actions
	bool SonosActionSetVolume(const std::string& devID, int volume);
	int  SonosActionGetVolume(const std::string& devID);

	// Sonos other UPnP methods
	bool SonosGetDeviceData(DeviceSessionData *upnprenderer, std::string& brand, std::string& model, std::string& name );

	// Sonos non-UPnP
	bool SonosActionSay(const std::string& tts, std::string& url);
	bool SonosSaveState(const std::string& devID);
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

