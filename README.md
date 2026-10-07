# Guilty Gear Vastedge XT Muxer
This tool reads the LVDS video and audio signals from the Vastedge arcade machine, and uses FFMPEG's C libraries to save them to MKV files for archiving.

## What is Guilty Gear Vastedge?
Overview from the Guilty Gear Wiki team's [Gofundme page](https://www.gofundme.com/f/acquiring-archiving-the-guilty-gear-vastedge-xt-pachislot)
> Guilty Gear Vastedge XT is a Japan-exclusive pachislot machine and mobile app from 2013. It was the first Guilty Gear game produced after Arc System Works narrowly managed to reacquire the rights to Guilty Gear after a devastating merger between their previous publisher, Sammy, and Sega, making Vastedge historically important to the series and to Arc System Works. Among other firsts, Vastedge was Naoki Hashimoto's debut as a vocalist for several heavy metal tracks in the game, which he still continues to provide with Guilty Gear Strive's soundtrack, 12 years later. Vastedge tells a story relevant to the "canon" of the series and is frequently mentioned or summarized in games that have released since. 
> Unfortunately, neither the pachislot machine or the mobile app were ever made available outside of Japan, and the mobile app reached its end of service not too long after its release, being removed from the iPhone App Store and Android Play Store permanently. The pachislot machines themselves have also become rare, expensive, and difficult to find in working condition. 

[Guilty Gear Wiki page for Vastedge XT](https://guiltygear.wiki.gg/wiki/Guilty_Gear_Vastedge_XT)

## Vastedge Hardware Information
- LVDS video data is transmitted to the onboard screen by a THine THC63LVDM83D transmitter.
- Efforts to capture video data with available LVDS-to-HDMI converters and capture cards have failed.

## Project Goals
To facilitate the Guilty Gear Wiki team's goal of providing subtitled video files of Vastedge's story, I aim to do the following:
- Set up a hardware system for capturing raw LVDS output and reading it on PC via [sigrok](https://sigrok.org/wiki/Main_Page) and the [libsigrok](https://sigrok.org/wiki/Libsigrok) C library.
- Pipe LVDS signals to a byte buffer of video frame pixels, and write the frames to an MKV file as they complete.
- Sync video output with audio output.
- Provide a simple interface for separating the continuous output stream into individual files in real time.

## Building and running
This project is currently built through Visual Studio for Windows

### Dependencies
**FFMPEG C libraries**
- Download [here](https://ffmpeg.org/download.html) including the shared version, and unzip
- Set up the directory structure that the `.vcxproj` expects:
	1. Create a `bin` directory in this project, and copy all `.dll` files into it
	2. Create an `include` directory in this project, and copy all of the subfolders that contain `.h` files, such as `libavcodec`, `libavdevice`, etc.
	3. Createa `lib` directory in this project, and copy all `.lib` files into it