#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE
#define WM_MYMESSAGE (WM_USER + 1)
#define ID_TRAY_APP_ICON    1001
#define ID_TRAY_EXIT        1002
#define ID_TIMER            1003
#define ID_TRAY_INFO        1004

#include <windows.h>
#include <shellapi.h>
#include <windows.h>  // Windows API functions and types
#include <iostream>   // For console input/output
#include <cmath>      // For abs() function
#include <cstdlib>    // For exit() function

NOTIFYICONDATA nid = {};
HMENU hPopMenu;

// Global constants for shake detection and cursor size adjustment
const int SHAKE_THRESHOLD = 100;        // Minimum movement to consider as part of a shake
const int SHAKE_COUNT_THRESHOLD = 5;    // Number of movements needed to trigger a shake
const int width = 256;                  // Desired cursor width
const int height = 256;                 // Desired cursor height

// Function to get the current cursor position
POINT GetCursorPos() {
    POINT pt;
    ::GetCursorPos(&pt);                // Windows API call to get cursor position
    return pt;
}


POINT lastPos = GetCursorPos();
POINT currentPos = GetCursorPos();
int shakeCount = 0;
bool movingRight = false;
bool lastMovingRight = false;


// Function to load a cursor from resources and scale it to a new size
HCURSOR LoadAndScaleCursor(int cursorId, int width, int height) {
    HCURSOR hCursor = LoadCursor(NULL, MAKEINTRESOURCE(cursorId));
    if (hCursor == NULL) {
        std::cerr << "Failed to load cursor. Error code: " << GetLastError() << std::endl;
        return NULL;
    }

    ICONINFO iconInfo;
    if (!GetIconInfo(hCursor, &iconInfo)) {
        std::cerr << "Failed to get icon info. Error code: " << GetLastError() << std::endl;
        return NULL;
    }

    BITMAP bmp;
    if (!GetObject(iconInfo.hbmMask, sizeof(BITMAP), &bmp)) {
        std::cerr << "Failed to get bitmap info. Error code: " << GetLastError() << std::endl;
        DeleteObject(iconInfo.hbmMask);
        DeleteObject(iconInfo.hbmColor);
        return NULL;
    }

    HDC hdc = GetDC(NULL);
    HBITMAP hbmMaskScaled = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP hbmColorScaled = CreateCompatibleBitmap(hdc, width, height);

    HDC hdcSrc = CreateCompatibleDC(hdc);
    HDC hdcDst = CreateCompatibleDC(hdc);

    HBITMAP hbmOldSrc = (HBITMAP)SelectObject(hdcSrc, iconInfo.hbmMask);
    HBITMAP hbmOldDst = (HBITMAP)SelectObject(hdcDst, hbmMaskScaled);
    StretchBlt(hdcDst, 0, 0, width, height, hdcSrc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);

    SelectObject(hdcSrc, iconInfo.hbmColor);
    SelectObject(hdcDst, hbmColorScaled);
    StretchBlt(hdcDst, 0, 0, width, height, hdcSrc, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);

    SelectObject(hdcSrc, hbmOldSrc);
    SelectObject(hdcDst, hbmOldDst);
    DeleteDC(hdcSrc);
    DeleteDC(hdcDst);
    ReleaseDC(NULL, hdc);

    DeleteObject(iconInfo.hbmMask);
    DeleteObject(iconInfo.hbmColor);

    ICONINFO iconInfoScaled = {
        iconInfo.fIcon,
        iconInfo.xHotspot * width / bmp.bmWidth,
        iconInfo.yHotspot * height / bmp.bmHeight,
        hbmMaskScaled,
        hbmColorScaled
    };

    HCURSOR hCursorScaled = CreateIconIndirect(&iconInfoScaled);

    DeleteObject(hbmMaskScaled);
    DeleteObject(hbmColorScaled);

    return hCursorScaled;
}


void RunEvery50ms() {
    // This function will be called every 50 milliseconds
    
    // Get current cursor position
    currentPos = GetCursorPos();
    
    // Calculate movement since last check
    int deltaX = abs(currentPos.x - lastPos.x);
    int deltaY = abs(currentPos.y - lastPos.y);

    movingRight = currentPos.x > lastPos.x;

    // Check if movement exceeds shake threshold
    if (deltaX + deltaY > SHAKE_THRESHOLD) {
        shakeCount++;

        if (shakeCount >= SHAKE_COUNT_THRESHOLD) {
            if (movingRight != lastMovingRight) {
                // Enlarge  Arrow cursor
                HCURSOR hCursorArrow = LoadAndScaleCursor(OCR_NORMAL, width, height);
                if (hCursorArrow == NULL) {
                    std::cerr << "Failed to create scaled cursor." << std::endl;
                    exit(1);
                }
                SetSystemCursor(hCursorArrow, OCR_NORMAL);

                // Enlarge beam cursor
                HCURSOR hCursorBeam = LoadAndScaleCursor(OCR_IBEAM, width, height);
                if (hCursorBeam == NULL) {
                    std::cerr << "Failed to create scaled cursor." << std::endl;
                    exit(1);
                }
                SetSystemCursor(hCursorBeam, OCR_IBEAM);

                // Sleap and reset
                Sleep(1000);  // Wait for 1 second to prevent multiple rapid changes
                SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);  // Restore original cursor
                
                shakeCount = 0;  // Reset shake counter
            }
        }
    } else {
        shakeCount = 0;  // Reset shake counter if movement is below threshold (the shake has stopped or never started)
    }

    lastPos = currentPos;  // Update last known position for next iteration
    lastMovingRight = movingRight;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = hWnd;
        nid.uID = ID_TRAY_APP_ICON;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_MYMESSAGE;
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        lstrcpy(nid.szTip, TEXT("ShakeToEnlarge"));
        Shell_NotifyIcon(NIM_ADD, &nid);

        hPopMenu = CreatePopupMenu();
        InsertMenu(hPopMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_INFO, TEXT("Info"));
        InsertMenu(hPopMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, TEXT("Exit"));

        // Set up the timer
        SetTimer(hWnd, ID_TIMER, 50, NULL);
        break;

    case WM_TIMER:
        if (wParam == ID_TIMER)
        {
            RunEvery50ms();
        }
        break;

    case WM_MYMESSAGE:
        switch (lParam)
        {
        case WM_RBUTTONDOWN:
        case WM_CONTEXTMENU:
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hPopMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
            PostMessage(hWnd, WM_NULL, 0, 0);
            break;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_INFO:
            MessageBox(NULL, TEXT("ShakeToEnlarge\n\nShake your mouse side to side to enlarge the cursor."), TEXT("Info"), MB_OK);
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, ID_TIMER);
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.lpszClassName = TEXT("ShakeToEnlarge");
    RegisterClassEx(&wcex);

    HWND hWnd = CreateWindow(TEXT("ShakeToEnlarge"), TEXT("ShakeToEnlarge"), 
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 
                             NULL, NULL, hInstance, NULL);

    if (!hWnd)
        return FALSE;

    ShowWindow(hWnd, SW_HIDE);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}