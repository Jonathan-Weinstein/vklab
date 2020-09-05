#pragma once

#include <stdint.h>

enum class virtual_key_action : uint8_t { release, press, repeat };

typedef unsigned char ubyte;
typedef int32_t ibool32;

struct Window;

struct WindowEventCallbacks {
    void (* onResizeClient) (void *userPtr, int clientWidth, int clientHeight, bool isIconic);
    void (* onVirtualKey)   (void *userPtr, uint32_t vkey, virtual_key_action action);
    void (* onPaint)        (void *userPtr, int x, int y, int xEnd, int yEnd);
    void (* onChar)         (void *userPtr, uint32_t ch); // Will also get this after a WM_KEY*, may not want to process both.
};

struct Window {

    /* I Guess just fill these in AFTER window_create is called... is memset to zero in window_create... */
    void *user_ptr;
    WindowEventCallbacks user_cb;


    bool bShouldClose;

    void *nativeHandle; // HWND
};

// these are CS_* WNDCLASS::style args, NOT WS_* "dwFlags" args passed to CreateWindowExA
enum {
    WindowClassFlag_VerticalRedraw=1, //CS_VREDRAW
    WindowClassFlag_HorizontalRedraw=2, //CS_HREDRAW
};

struct WindowD3D11 : Window { };

int WindowWin32_Create(Window* window, int width, int height, const char *title, uint32_t classStyle);
void WindowWin32_Destroy(Window& w);

void Window_Show(Window&);
void Window_SetTitle(Window&, const char *);

bool Window_DispatchMessagesNonblocking(); // Returns true if application should close, does not block if there is no message.
bool Window_WaitAtLeastOneMessage();
void Window_PostQuitMessage();

enum {
    WINDOW_VKEY_ESCAPE = 0x1B,

    WINDOW_VKEY_LEFT=0x25, WINDOW_VKEY_UP=0x26, WINDOW_VKEY_RIGHT=0x27, WINDOW_VKEY_DOWN=0x28

    //regular keyboard keys are UPPER_CASE ascii, e.g: 'Q'
};

inline bool Window_ShouldClose(const Window& window) { return window.bShouldClose; }
inline void Window_SetShouldClose(Window& window) { window.bShouldClose = true; }
