Watch the video: https://vimeo.com/1209453038?share=copy&fl=sv&fe=ci

Desk Clock

It’s strange, before, I never really considered that building something like this was an option, if you know what I mean. Like just buying parts and fitting them together and it all working is really magical.

It may sound quite annoying, but I am surprisingly glad of how often I failed (mind you, not at the time). I redesigned the outer body of the clock 3 times (plus printing it three times, don’t even want to know how much filament I used up), moved up from an Arduino Uno to an ESP32 as the Uno just didn’t have enough RAM, and redesigned how the buttons would work 4 times. At the time, this was very disheartening, but when I now look at my first draft and imagine this would be the final result, I’m so glad it isn’t.

A retro-inspired desk clock.
Features

E-paper time display, current time
Date display, date and month shown below the time
4-day weather forecast, fetched from Open-Meteo API. Shows day, max temperature, and condition
Ambient LED strip, dark purple WS2812B LED strip for mood lighting
Light toggle button, turns the LED strip on or off
Alarm system, set alarm hour via a potentiometer dial, small indicator LED shows when alarm is active
Snooze/dismiss button, dismisses the alarm when ringing, or toggles AM/PM when setting the alarm
WiFi time sync, time is fetched from an NTP server automatically


Hardware

    - ESP32 dev board
    - RGB addressable light strip
    - 3mm LED light in orange
    - 3.7-inch e-ink display
    - two buttons
    - one Potentiometer
    - One passive buzzer
    - a lot of 3d printing filament (PLA and PETG transparent)
    - usb cable



Project Notes

Weather data is provided by Open-Meteo.
Time is synced from pool.ntp.org.
