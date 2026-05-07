# godot-openh264
Godot OpenH264 Addon via GDExtension

## About OpenH264

OpenH264 is an open-source H.264 codec library developed and published
by Cisco Systems, Inc.

To comply with Cisco's binary license, the following are required
for all users of this addon:
```
1. Provide a UI that allows end users to enable and disable the use
   of OpenH264.
2. Display the following text in that UI:
   "OpenH264 Video Codec provided by Cisco Systems, Inc."
3. Include or make available Cisco's OpenH264 Binary License to end users.
```
For details, please refer to Cisco's binary license:
http://www.openh264.org/BINARY_LICENSE.txt

## How to use

The OpenH264 binaries will be downloaded automatically, but OpenH264 is disabled by default.  
Please enable the loader from the script.  
```
OpenH264Loader.enabled = true
```

## Build
```
git clone --recursive https://github.com/buresu/godot-openh264.git
cd godot-openh264
mkdir build && cd build

[Windows]
cmake -G "Visual Studio 18 2026" ..
cmake --build . --config [Debug|Release] --target install

[Mac, Linux]
cmake -DCMAKE_BUILD_TYPE=[Debug|Release] ..
cmake --build . --target install
```

## License
MIT License
