#pragma once

// Moved these to global compile flags:
//#define VK_NO_PROTOTYPES
//#define VK_USE_PLATFORM_WIN32_KHR

/*

WIN32_LEAN_AND_MEAN
VC_EXTRALEAN
_CRT_SECURE_NO_WARNINGS
VK_USE_PLATFORM_WIN32_KHR
VK_NO_PROTOTYPES

*/

/*
    Ctrl + H
    Find what:
[\u0020]+\r?\n
    Replace with:
\n
*/

#include "volk/volk.h"
