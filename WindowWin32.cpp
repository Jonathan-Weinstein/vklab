#include "Window.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

LRESULT CALLBACK main_wndproc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Window *const window = (Window *)GetWindowLongPtr(hWnd, GWLP_USERDATA); // The "A" or "W" here needs to match?
    if (!window) {
        /*  At least these messages can be sent right after CreateWindow, before DispatchMessage is called.
                WM_GETMINMAXINFO (36)
                Got WM_NCCREATE (129), "NC"=NonClient
                WM_NCCALCSIZE (131)
                WM_CREATE (1)
        */
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    void *const userPtr = window->user_ptr;

    switch (message)
    {
        case WM_CLOSE:
        case WM_DESTROY:
        {
            window->bShouldClose = true;
            PostQuitMessage(0);
        } return 0;
        case WM_SIZE:
        {
            //printf("WM_SIZE %u %u\n", LOWORD(lParam), HIWORD(lParam));
            if (!window->user_cb.onResizeClient) break;

            window->user_cb.onResizeClient(userPtr, LOWORD(lParam), HIWORD(lParam), wParam == SIZE_MINIMIZED);
        } return 0;
        case WM_PAINT:
        {
            if (!window->user_cb.onPaint) break;

            /*
             * CS_VREDRAW causes a paint to be sent when the window is resized vertically. When the displayed picture depends on the aspect ratio
             * this is usually desired. Similar for CS_HREDRAW, but horizontal.
             */
            PAINTSTRUCT p;
            (void)BeginPaint(hWnd, &p);
            window->user_cb.onPaint(userPtr, p.rcPaint.left, p.rcPaint.top, p.rcPaint.right, p.rcPaint.bottom);//x,y,xend,yend : exclusive
            EndPaint(hWnd, &p);
        } return 0;
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            if (!window->user_cb.onVirtualKey) break;
            /*  Bit 30 is 1 if the key was down before
                Bit 31 is 1 if the key is UP now.
                The top 2 bits have this mapping:
                    00 -> press
                    01 -> repeat
                    10 : invalid
                    11 -> release
            */
            window->user_cb.onVirtualKey(userPtr, uint32_t(wParam), virtual_key_action(((uint32_t(lParam) >> 30) + 1) & 3));
        } return 0;
        case WM_CHAR:
        case WM_SYSCHAR:
        {
            if (!window->user_cb.onChar) break;

            // http://www.ngedit.com/a_wm_keydown_wm_char.html
            window->user_cb.onChar(userPtr, (uint32_t)wParam);
        } return 0;
    // -- No explicit default. --
    }//end switch

    //printf("unhandled message=%u, 0x%x\n", message, message);
    return DefWindowProcA(hWnd, message, wParam, lParam);
}

static int Error(HWND wnd, const char *message)
{
    MessageBoxA(wnd, message, "Win32 window creation error:", MB_ICONERROR);
    return 1;
}

int WindowWin32_Create(Window *window, int width, int height, const char *title, uint32_t classStyle)
{
    *window = {};

    //window->windowPlacement = { sizeof(nw->windowPlacement) };

    HINSTANCE const hInstance = GetModuleHandle(nullptr);

    const char *windowClassName;
    {
        WNDCLASSEXA wcex = { sizeof(wcex) };
        wcex.style = classStyle;
        wcex.lpfnWndProc = main_wndproc;//see set wndporc notes below
        wcex.hInstance = hInstance;
        wcex.lpszClassName = "A";//something short, only needs to be process specific?
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);//need this so dont get stuck with <-> looking cursor after moving to edge of screen

        windowClassName = (const char *)MAKEINTATOM(RegisterClassExA(&wcex)); // MAKEINTATOM c-casts to "LPTSTR" (TCHAR *)... but want char * for CreateWindowExA...
    }

    if (!windowClassName)
        return Error(nullptr, "Error: RegisterClassExA");

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;

    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    {
        // Adjust window's size for non-client area elements
        {
            RECT rect = { 0, 0, width, height };
            AdjustWindowRect(&rect, style, false);
            width = rect.right - rect.left;
            height = rect.bottom - rect.top;
        }

        #if 0 // Center the window's position:
        {
            RECT primaryDisplaySize;
            SystemParametersInfo(SPI_GETWORKAREA, 0, &primaryDisplaySize, 0); // system taskbar and application desktop toolbars not included
            x = (primaryDisplaySize.right - width) / 2;
            y = (primaryDisplaySize.bottom - height) / 2;
        }
        #endif
    }

    HWND const wnd = CreateWindowExA(
        0,                    //DWORD     dwExStyle,
        windowClassName,    //LPCSTR    lpClassName,
        title,                //LPCSTR    lpWindowName,
        style,                //DWORD     dwStyle,
        x, y, width, height,//int x, y, nWidth, nHeight;
        nullptr,            //HWND      hWndParent,
        nullptr,            //HMENU     hMenu,
        hInstance,            //HINSTANCE hInstance,
        nullptr);            //LPVOID    lpParam

    if (wnd == nullptr) {
        return Error(nullptr, "CreateWindowExA)");
    }

    static_assert(sizeof(LONG_PTR) == sizeof(void *), "");
    /* Dunno why these are macros with the A/W thing. Make sure matches in main_wndproc. */
    SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR)window);
    if ((void *)GetWindowLongPtr(wnd, GWLP_USERDATA) != window) {
        WindowWin32_Destroy(*window);
        return Error(wnd, "{Get, Set}WindowLongPtr");
    }

    window->nativeHandle = wnd;
    return 0;
}

void WindowWin32_Destroy(Window& win)
{
    HWND hw = (HWND) win.nativeHandle;
    if (hw) {
        DestroyWindow(hw);
        win.nativeHandle = nullptr;
    }
}

void Window_Show(Window& win)
{
    ShowWindow((HWND) win.nativeHandle, SW_NORMAL);
}

void Window_SetTitle(Window& win, const char *s)
{
    SetWindowTextA((HWND) win.nativeHandle, s);
}

bool Window_DispatchMessagesNonblocking()
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            if (void *window = (void *)GetWindowLongPtr(msg.hwnd, GWLP_USERDATA)) {
                static_cast<Window *>(window)->bShouldClose = true;
            }
            return true;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return false;
}

bool Window_WaitAtLeastOneMessage()
{
    MSG msg;

    if (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
        return Window_DispatchMessagesNonblocking();
    }
    else {
        if (void *window = (void *)GetWindowLongPtr(msg.hwnd, GWLP_USERDATA)) {
            static_cast<Window *>(window)->bShouldClose = true;
        }
        return true;
    }
}

void Window_PostQuitMessage()
{
    PostQuitMessage(0);
}
