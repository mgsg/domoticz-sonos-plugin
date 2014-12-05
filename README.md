R E A D M E
===========

This is a hardware plugin for the excellent Domoticz home automation system. Please visit Domoticz website http://www.domoticz.com for more info.

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
    - "Say"/TTS (text to speech) command implemented for Sonos speakers to use for home automation messages ("Temperature in XXX over YYY�C").

Prerequisites:
    - Linux based Domoticz installation with compile support
    - Some UPnP Media Renderer. Sonos is my primary target. Other hardware could work (test at your own risk).
    - I would strongly suggest to use monit to restart Domoticz if testing new UPnP equipment.

Known bugs/limitations:

    - For some Sonos streams, a warning can be seen in the log: I assume it's a libsoup problem (domoticz and sonos continue $
    - Volume level ocassionally doesn't refresh in the Web UI
    - Glib instead of BOOST libraries used (much has been already migrated)
    - Size of the executable is too big!  
    - Polling inteval must be lower than 20secs; if not: you get Error:
    Sonos hardware (11) thread seems to have ended unexpectedly
    - Play:1 temp sensor monitor disabled by default -limited interest for HA
    - Avoid playing if URI=No stream!!!

Runtime instructions:

    - Activate the Sonos hardware in the Setup->Hardware section
    - After some minutes, Domoticz will identify 4 devices for each Sonos speaker in the Setup->Devices section
    - Add one device for Play/Pause/Volume (dimmer type), and three devices for TTS, Preset 1 (P1) and Preset 2 (P2) - (Push On button type)
    - Create three new String-type user variables (maybe in some future version this ones will have to be created automatically):
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
