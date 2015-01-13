R E A D M E
===========

This is a hardware plugin for the excellent Domoticz home automation system. Please visit Domoticz website http://www.domoticz.com for more info.

Some use cases
    - switch lights/amplifier on/off when music starts to play (and not the other way round!)
    - complex wakeup/alarm scenarios (first soft music, later news, later switch music to the kitchen...)
    - voice announcements (power consumption above threshold, front bell ringing, bathroom lights on for more than xxx...)

Changes in v0.53
    - Based on Domoticz v2180
    - Not using additional boost libraries anymore: Now using std::map instead of boost::unordered_map. 
    - Bug fix: Volume showing 100% when playing. 
    - Bug fix: "Say" command in rest API and in JavaScript code now in sync.

    Known bugs/limitations:
    - Deleting Sonos hardware gives segmentation fault. (Code changed but still happening sometimes).
    - Error in log: "Error: Error opening url:" - Maybe related to url base64 encoding.
    - UPnP tab still in "alpha" state:
      - UPnP devices change order on every status change (last changed device shown first)
      - Volume slider behaves differently in Firefox/Chrome/Safari. Sometimes hangs on the mouse.
      - Play/stop button sometimes unresponsive
    - Volume level ocassionally doesn't refresh in the Web UI
    - Occasional error: Sonos hardware (11) thread seems to have ended unexpectedly
    
Changes in v0.52
    - Code refactored from it's C origin to a more OO paradigm. 
    - Delete devices from last version! Now only one device for each MediaRenderer device (speaker, TV, Kodi...)
    - Upgraded Json API with specific commands for UPnP devices
    - New UI tab for UPnP devices. 
    - Remerged with Domoticz v2180

Changes in v0.51
    - Debug logs still activated
    - Bug in TTS possibly corrected: If user variable starts with 0-, then, direct Google access url is used for text-to-speech mp3. If not, Domoticz WebServer.
    - Initial experimental specific tab view for UPnP devices, still with no functionality other than Play/Pause: http://yourip:8080/#/UPnP

Changes in v0.50
    - Logs, logs, logs
    - If TTS user variable starts with 0-, then, direct Google access url is used for text-to-speech mp3. If not, Domoticz WebServer.
    - Initial experimental support for Belkin WeMo switch

Changes in v0.49
    - Remerged with Domoticz v2145
    - Save queue before TTS announcement. Still not able to restore previous state.
    - Compiled with GPIO, OpenZwave and GUPnP (now size is much bigger...)
    	
Changes in v0.48
    - First steps to save Sonos state before TTS:
    	- Support MediaServer in addition to MediaRenderer UPnP interfaces - SaveQueue method - still wip 

Changes in v0.47:
    - Moved from Glib HashTable to Boost unordered_map
    - XBMC is not able to play a file from Domoticz WebServer. It is working with other radio servers.

Changes in v0.46:
    - Deleted line in CMakeLists.txt for a now-deleted HardwarePluginSample.cpp
    - Some changes to try to support XBMC-Kodi (returning (null) metadata even if the function succeeds!). Play/Pause working now.
    - Better zone support. Zone coordinator pause causes slaves to pause. Pausing a slave has no effect. UI still doesn't refresh Ok in this case.
    - TTS say device now enabled for all devices. Only Sonos speakers tested. 

Changes in v0.43:

    - Many code stability and readability improvements
    - Use user variables to store 2 presets (URIs) to play from (preset1,...)
    - "Say"/TTS (text to speech) command implemented for Sonos speakers to use for home automation messages ("Temperature in XXX over YYYºC").

Prerequisites:
    - Linux based Domoticz installation with compile support
    - Some UPnP Media Renderer. Sonos is my primary target. Other hardware could work (test at your own risk).
    - I would strongly suggest to use monit to restart Domoticz if testing new UPnP equipment.

Runtime instructions:

    - Activate the Sonos hardware in the Setup->Hardware section
    - After some minutes, Domoticz will identify 1 device for each Sonos speaker in the Setup->Devices section
    - Add device for Play/Pause/Volume with dimmer type
    - Create three new String-type user variables:
    sonos-tts f.i.: Welcome to the Domoticz home
    sonos-preset-1 f.i.: x-rincon-mp3radio://radioclasica.rtve.stream.flumotion.com/rtve/radioclasica.mp3.m3u (check for your favorite stations looking for x-rincon-mp3radio on the web)
    sonos-preset-2

Notes:

    Using this plugin with XBMC (and other UPnP devices)
    Unfortunately, after some trial and error cycles/hours trying to make the TTS functionality work under xbmc,
    that functionality is still now working. XBMC/Kodi does not support all UPnP commands/the same ones as Sonos.
    To test basic Play/Pause functionality, start XBMC/Kodi, go to the Music menu option, and play a mp3 file.
    This plugin should be able to play and pause this stream. Volume control doesn't work (and will not, XBMC doesn't
    support UPnP control points). The presets sometimes work, it depends on the web server, the file extensions,...

