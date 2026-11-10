// ============================================================================
//  main.cpp  -  Program entry point
//  NSUT File Explorer - Operating Systems Mini Project
//  A Windows-style file explorer written in C++17 with the Win32 API.
// ============================================================================
#include "Common.h"
#include "MainWindow.h"

#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Do not show the Windows "There is no disk in the drive" pop-ups when we
    // look at empty CD/DVD or card-reader drives.
    SetErrorMode(SEM_FAILCRITICALERRORS);

    MainWindow window;
    if (!window.create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"The main window could not be created.", L"File Explorer",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    return window.run();
}
