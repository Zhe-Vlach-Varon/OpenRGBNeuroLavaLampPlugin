# Neuro-sama Lava Lamp Plugin 

> [!WARNING]
> **This plugin only supports taking control of RGB devices from standard OpenRGB or the OpenRGB Effects Plugin.**
> 
> If you have other plugins attempting to set colors simultaneously, your LEDs will rapidly flicker between colors!
> 
> **Why does this happen?** OpenRGB's plugin architecture assumes that a plugin **always** wants persistent control over its targeted LEDs. It is not designed to dynamically share or toggle control with other plugins.
## What is this?

This is a plugin for [OpenRGB](https://openrgb.org/) that synchronizes OpenRGB with [Neuro-sama](https://www.twitch.tv/vedal987)'s lava lamp

## Downloads

* [Windows 32](https://github.com/bw8686/OpenRGBNeuroLavaLampPlugin/releases/latest/download/OpenRGBNeuroLavaLampPlugin_Windows_32.dll)
* [Windows 64](https://github.com/bw8686/OpenRGBNeuroLavaLampPlugin/releases/latest/download/OpenRGBNeuroLavaLampPlugin_Windows_64.dll)
* [Linux 64](https://github.com/bw8686/OpenRGBNeuroLavaLampPlugin/releases/latest/download/libOpenRGBNeuroLavaLampPlugin_Linux_amd64.so)
* [MacOS ARM64](https://github.com/bw8686/OpenRGBNeuroLavaLampPlugin/releases/latest/download/libOpenRGBNeuroLavaLampPlugin_MacOS_ARM64.dylib)

## How do I install it?

* Download and extract the correct files depending on your system
* Launch OpenRGB
* From the Settings -> Plugins menu, click the "Install plugin" button