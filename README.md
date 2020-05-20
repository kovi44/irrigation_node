![Sonoff 4ch Pro r2 Irrigation / Sprinkler node](/images/poster_irrigation_node.png)

Alternative Irrigation / Sprikler firmware for Sonoff 4ch Pro with **easy configuration using webUI, OTA updates, automation using timers and entirely local control over HTTP, or 433Mhz remote control**.
_Written in Arduino IDE._

If you like **Wifi Irrigation Node**, give it a star!

See [RELEASENOTES.md](RELEASENOTES.md) for release information.

## Description
The goal of this project was re-use of existing Sonoff 4ch Pro r2 hardware for garden irrigation system by alternative firmware. Such irrigation system fully benefits from hardware design - ability to turn ON/OFF zones manually by buttons, use RF433 remotes to start defined Irrigation Zones, handle irrigation scheduling using intuitive WebUI or connect to HomeAutomation System using HTTP API. 


## Disclaimer

:warning: **DANGER OF ELECTROCUTION** :warning:

If your device connects to mains electricity (AC power) there is danger of electrocution if not installed properly. If you don't know how to install it, please call an electrician (***Beware:*** certain countries prohibit installation without a licensed electrician present). Remember: _**SAFETY FIRST**_. It is not worth the risk to yourself, your family and your home if you don't know exactly what you are doing. Never tinker or try to flash a device using the serial programming interface while it is connected to MAINS ELECTRICITY (AC power).

We don't take any responsibility nor liability for using this software nor for the installation.

## Quick Install
esptool.py --port [serial_interface like COM3 or /dev/cu.SerialInterface] write_flash -fs 1MB -fm dout 0x0 irrigation_sonoff_4ch_pro_r2.bin

## Author
lukas(at)k0val.sk (C)2020





