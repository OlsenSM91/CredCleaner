#include <windows.h>
#include <shlobj.h>
#include <tchar.h>
#include <stdio.h>
#include "resource.h"  // Include the resource header

// Function declarations
void ListUsers(HWND hwnd);
void ClearCreds(LPCTSTR username);
void DeleteFilesInDirectory(LPCTSTR directory);
void LogDeletion(LPCTSTR filePath);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
BOOL IsRunAsAdmin();
void RelaunchAsAdmin();

// Global variables
HINSTANCE hInst;
HWND hUserList;
HWND hClearButton;
HWND hLabel;
TCHAR selectedUser[256];
FILE *logFile;

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow) {
    if (!IsRunAsAdmin()) {
        RelaunchAsAdmin();
        return 0; // Exit the current instance as the new elevated instance will continue
    }

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = _T("SimpleCredClearApp");
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MYICON));  // Load the icon

    if (!RegisterClass(&wc)) {
        MessageBox(NULL, _T("RegisterClass failed!"), _T("Error"), MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        _T("CredCleaner x Steven Olsen"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 300,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        MessageBox(NULL, _T("CreateWindowEx failed!"), _T("Error"), MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

BOOL IsRunAsAdmin() {
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;
    // Allocate and initialize a SID of the administrators group.
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(
            &NtAuthority, 
            2, 
            SECURITY_BUILTIN_DOMAIN_RID, 
            DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, 
            &pAdministratorsGroup)) {
        // Determine whether the SID of administrators group is enabled in the primary access token of the process.
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin;
}

void RelaunchAsAdmin() {
    TCHAR szPath[MAX_PATH];
    if (GetModuleFileName(NULL, szPath, ARRAYSIZE(szPath))) {
        // Launch itself as administrator.
        SHELLEXECUTEINFO sei = { sizeof(sei) };
        sei.lpVerb = _T("runas");
        sei.lpFile = szPath;
        sei.hwnd = NULL;
        sei.nShow = SW_NORMAL;
        if (!ShellExecuteEx(&sei)) {
            MessageBox(NULL, _T("The program requires administrator privileges to run."), _T("Error"), MB_OK | MB_ICONERROR);
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            hLabel = CreateWindow(
                _T("STATIC"), _T("Select User"),
                WS_VISIBLE | WS_CHILD,
                10, 10, 300, 20,
                hwnd, NULL, hInst, NULL
            );

            ListUsers(hwnd);

            hClearButton = CreateWindow(
                _T("BUTTON"), _T("Clear Creds"),
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                10, 230, 100, 30,
                hwnd, (HMENU)1, hInst, NULL
            );

            // Open log file
            logFile = _tfopen(_T("deletion_log.txt"), _T("a"));
            if (logFile == NULL) {
                MessageBox(hwnd, _T("Unable to open log file."), _T("Error"), MB_OK | MB_ICONERROR);
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == 1) {
                int index = SendMessage(hUserList, LB_GETCURSEL, 0, 0);
                if (index != LB_ERR) {
                    SendMessage(hUserList, LB_GETTEXT, index, (LPARAM)selectedUser);
                    ClearCreds(selectedUser);
                    MessageBox(hwnd, _T("Credentials cleared!"), _T("Success"), MB_OK);
                }
            }
            break;
        case WM_DESTROY:
            // Close log file
            if (logFile != NULL) {
                fclose(logFile);
            }
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void ListUsers(HWND hwnd) {
    hUserList = CreateWindowEx(
        0, _T("LISTBOX"), NULL,
        WS_CHILD | WS_VISIBLE | LBS_STANDARD,
        10, 40, 300, 180,
        hwnd, NULL, hInst, NULL
    );

    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    TCHAR path[MAX_PATH];

    _sntprintf(path, MAX_PATH, _T("%s\\Users\\*"), _tgetenv(_T("SystemDrive")));

    hFind = FindFirstFile(path, &findFileData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (_tcscmp(findFileData.cFileName, _T(".")) != 0 &&
                    _tcscmp(findFileData.cFileName, _T("..")) != 0 &&
                    _tcslen(findFileData.cFileName) < sizeof(selectedUser) / sizeof(TCHAR)) {
                    SendMessage(hUserList, LB_ADDSTRING, 0, (LPARAM)findFileData.cFileName);
                }
            }
        } while (FindNextFile(hFind, &findFileData) != 0);
        FindClose(hFind);
    }
}

void ClearCreds(LPCTSTR username) {
    if (_tcschr(username, _T('\\')) || _tcschr(username, _T('/')) || _tcschr(username, _T(':'))) {
        MessageBox(NULL, _T("Invalid username"), _T("Error"), MB_OK | MB_ICONERROR);
        return;
    }

    TCHAR path[MAX_PATH];
    LPCTSTR directories[] = {
        _T("AppData\\Local\\Microsoft\\Credentials"),
        _T("AppData\\Local\\Microsoft\\Vault"),
        _T("AppData\\Roaming\\Microsoft\\Credentials"),
        _T("AppData\\Roaming\\Microsoft\\Vault")
    };

    for (int i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        _sntprintf(path, MAX_PATH, _T("%s\\Users\\%s\\%s"), _tgetenv(_T("SystemDrive")), username, directories[i]);
        DeleteFilesInDirectory(path);
    }
}

void DeleteFilesInDirectory(LPCTSTR directory) {
    WIN32_FIND_DATA findFileData;
    HANDLE hFind;
    TCHAR searchPath[MAX_PATH];
    TCHAR filePath[MAX_PATH];

    _sntprintf(searchPath, MAX_PATH, _T("%s\\*"), directory);

    hFind = FindFirstFile(searchPath, &findFileData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                _sntprintf(filePath, MAX_PATH, _T("%s\\%s"), directory, findFileData.cFileName);
                if (DeleteFile(filePath)) {
                    LogDeletion(filePath);
                } else {
                    LogDeletion(_T("Failed to delete file"));
                }
            } else if (_tcscmp(findFileData.cFileName, _T(".")) != 0 && _tcscmp(findFileData.cFileName, _T("..")) != 0) {
                _sntprintf(filePath, MAX_PATH, _T("%s\\%s"), directory, findFileData.cFileName);
                DeleteFilesInDirectory(filePath);  // Recursively delete files in subdirectories
                RemoveDirectory(filePath); // Remove the subdirectory
            }
        } while (FindNextFile(hFind, &findFileData) != 0);
        FindClose(hFind);
    }
}

void LogDeletion(LPCTSTR filePath) {
    if (logFile != NULL) {
        _ftprintf(logFile, _T("Deleted: %s\n"), filePath);
        fflush(logFile);
    }
}
