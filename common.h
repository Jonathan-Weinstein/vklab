#pragma once

#include <stdint.h>
#include <stddef.h>


#if defined _DEBUG || defined DEBUG
    #define is_debug true
    #include <assert.h>
    #define ASSERT assert
#else
    #define is_debug false
    #define ASSERT(e) ((void)0)
#endif


typedef unsigned uint;
typedef unsigned char ubyte;

template<class T, uint N> constexpr uint lengthof(T(& )[N]) { return   N; }
template<class T, uint N> constexpr T*      endof(T(&a)[N]) { return a+N; }

template<class T> T Max(T a, T b) { return b < a ? a : b; }

template<class T> T Abs(T v) { return v < T(0) ? -v : v; }


