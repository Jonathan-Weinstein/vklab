# Vulkan Lab  

For some reason I feel like learning a bit of the Vulkan API.  

The Visual Studio files are from VS 2017

mingw release build:  
```
g++ -std=c++11 -Wall -Wextra -Wno-missing-field-initializers -DWIN32_LEAN_AND_MEAN -DVK_USE_PLATFORM_WIN32_KHR -DVK_NO_PROTOTYPES -I %VULKAN_SDK%\Include -O1 -DNDEBUG *.cpp -o vklab_O1.exe
```
for debug omit `-O1 -DNDEBUG`, add `-g -D_DEBUG`

Should prob use glfw at some point so I can test on linux, see if that fence vs waitIdle thing occurs there.
But not needing another lib on win32 is nice.

[hooray triangles](hello.jpg)
