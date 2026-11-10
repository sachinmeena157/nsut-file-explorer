# NSUT File Explorer — Operating Systems Mini Project

A Windows-style **File Explorer** written in **C++17** with the native **Win32 API** and
`std::filesystem`. It lets you browse drives and folders, open files with their default
programs, create / rename / copy / cut / paste / delete items, search by name and view
file properties.

**Student:** Sachin Meena (2025UCS1662) — B.Tech CSE, NSUT Delhi  
**Subject:** Operating Systems (mini project)  
**Platform:** Windows 10 / 11 · **Compiler:** MinGW-w64 g++ (MSYS2 UCRT64) · **IDE:** VS Code

---

## 1. Installation (one time)

1. Install **MSYS2** from <https://www.msys2.org> (default folder `C:\msys64`).
2. Open **"MSYS2 UCRT64"** from the Start menu and run  
   `pacman -Syu` (reopen the window if it closes, run it again), then  
   `pacman -S --needed base-devel mingw-w64-ucrt-x86_64-toolchain` (press Enter, then `Y`).
3. Add `C:\msys64\ucrt64\bin` to your Windows **PATH**
   (Start → "Edit environment variables for your account" → Path → Edit → New).
4. Open a **new** PowerShell and check: `g++ --version` and `gdb --version`.
5. Install **VS Code** and its **C/C++** extension (Microsoft).

## 2. Build and run

Open the `FileExplorer` folder in VS Code (**File → Open Folder**), then either:

* press **Ctrl+Shift+B** (task *Build File Explorer*), or
* in the VS Code terminal run `.\build.bat` (build) / `.\build.bat run` (build and start).

The program is created as `build\FileExplorer.exe`. It is linked statically, so you can copy
this single `.exe` to any Windows PC (for example, for the viva demonstration).

To run the automatic tests of the non-GUI modules: `.\test.bat`.

## 3. Project structure

```
FileExplorer/
├── main.cpp                      program entry point (WinMain)
├── build.bat / test.bat          build and test scripts
├── include/                      header files
│   ├── Common.h                  FileEntry struct + helper functions
│   ├── FileSystemManager.h       reads directories, metadata, drives, permissions
│   ├── NavigationManager.h       back / forward stacks, up, path validation
│   ├── FileOperations.h          create, rename, delete, copy, cut, paste, properties
│   ├── SearchManager.h           recursive search by name (wildcards supported)
│   ├── SearchJob.h               data shared with the background search thread
│   ├── MainWindow.h              the GUI class
│   └── UiIds.h                   control / command / message identifiers
├── src/                          implementation files (.cpp)
│   ├── Common.cpp  FileSystemManager.cpp  NavigationManager.cpp
│   ├── FileOperations.cpp  SearchManager.cpp
│   └── MainWindow.cpp            window, controls, layout, list drawing
│       MainWindowActions.cpp     button / menu / keyboard actions, search thread
├── assets/                       app.ico, app.manifest, app.rc (icon + modern look)
├── tests/test_core.cpp           console tests (78 checks)
└── .vscode/                      tasks.json (build task), c_cpp_properties.json
```

## 4. Features

| Area | Feature |
|---|---|
| Interface | Sidebar (Quick access + This PC with all drives), file list with icons, address bar, search box, toolbar, status bar, right-click menus |
| Navigation | Back, Forward, Up, Refresh, typed paths, "This PC" drive view, history stacks |
| File management | Open (default app), new folder, new text file, rename (in place), delete (Recycle Bin or permanent, with confirmation), copy / cut / paste, properties |
| Search | Case-insensitive name search in a folder and all sub-folders, wildcards `*` `?`, runs in a separate thread |
| Sorting | Click Name / Date / Type / Size headers (folders always first, natural order `file2 < file10`) |
| Safety | Access-denied and invalid-path messages, hidden/system files hidden by default, invalid-name checks, cannot copy a folder into itself |

### Keyboard shortcuts

`Enter` open · `F2` rename · `Del` delete · `Shift+Del` delete permanently · `Ctrl+C / X / V` copy / cut / paste ·
`Ctrl+A` select all · `Ctrl+Shift+N` new folder · `F5` refresh · `Alt+←/→/↑` back / forward / up ·
`Backspace` up · `Ctrl+L` address bar · `Ctrl+F` search box · `Esc` leave the address / search box

## 5. Troubleshooting

| Problem | Solution |
|---|---|
| `g++` is not recognized | PATH step missing; close and reopen PowerShell / VS Code |
| `windres` is not recognized | You installed the wrong MSYS2 environment; install the **ucrt64 toolchain** (step 2) |
| Buttons show squares instead of arrows | Font *Segoe MDL2 Assets* is missing (Windows 10/11 include it) |
| Build error | Copy the **first** error message — it normally names the file and line |
| Copy/paste with other programs | The clipboard is internal to this program (see Limitations in the report) |

## 6. Known limitations

* Copy / paste is internal to the application (not shared with Windows Explorer).
* Large copy operations run in the main thread, so the window pauses until they finish.
* Folder properties stop counting after 50,000 items (the dialog then says "at least").
