Changes in v0.43:

    - Many code stability and readability improvements
    - Use user variables to store 2 presets (URIs) to play from (preset1,...)
    - "Say"/TTS (text to speech) command implemented for Sonos speakers to use for home automation messages ("Temperature in XXX over YYYºC").


RUNTIME INSTRUCTIONS:

    - Activate the Sonos hardware in the Setup->Hardware section
    - After some minutes, Domoticz will identify 4 devices for each Sonos speaker in the Setup->Devices section
    - Add one device for Play/Pause/Volume (dimmer type), and three devices for TTS, Preset 1 (P1) and Preset 2 (P2) - (Push On button type)
    - Create three new String-type user variables (maybe in some future version this ones will have to be created automatically):
    sonos-tts f.i.: Welcome to the Domoticz home
    sonos-preset-1 f.i.: x-rincon-mp3radio://radioclasica.rtve.stream.flumotion.com/rtve/radioclasica.mp3.m3u (check for your favorite stations looking for x-rincon-mp3radio on the web)
    sonos-preset-2


NOTES:

    Using this plugin with XBMC (and other UPnP devices)
    Unfortunately, after some trial and error cycles/hours trying to make the TTS functionality work under xbmc, I had to give up: while XBMC supports some UPnP commands, like play and pause, it does not directly support playing a http stream, and that's what TTS support uses: a mp3 file from Google translate is downloaded and saved to a domoticz web server accessible location (domoticz/www/media/), and then Sonos use that http url to play it.

    XBMC only supports playing streams with this trick: http://kodi.wiki/view/Internet_video_and_audio_streams

    There could be eventually a solution: share the sonos location as a XBMC accessible network share with a specific name, create the xbmc .STRM file and instruct XBMC to use it to play (I assume you can do that; didn't try).

    If somebody want's to do it, please go ahead (and ask if you want); I did try to comment my code. [Sorry, I'm not going to work on that. ]


Known bugs/limitations:

    - For some Sonos streams, a warning can be seen in the log: I assume it's a libsoup problem (domoticz and sonos continue working Ok): libsoup-CRITICAL **: soup_uri_new_with_base:
    - Volume level ocassionally doesn't refresh
    - TTS say device only enabled for Sonos speakers
    - Glib instead of BOOST libraries used (much has been already migrated)
    - Polling inteval must be lower than 20secs; if not: you get Error:
    Sonos hardware (11) thread seems to have ended unexpectedly
    - Play:1 temp sensor monitor disabled by default -limited interest for HA