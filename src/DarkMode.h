// This file is part of Notepad.
// See License.txt for details about distribution and modification.
#pragma once

#include <windows.h>

// Windows 10 21H2
#ifndef NP2_MIN_WINDOWS_BUILD
#define NP2_MIN_WINDOWS_BUILD	19044
#endif

#ifndef NP2_WIN11_BUILD
#define NP2_WIN11_BUILD			22000
#endif

// Shell colors aligned with Notepad DarkTheme.ini
#define NP2_DARK_SHELL_BACKGROUND	RGB(0x2D, 0x2D, 0x30)
#define NP2_DARK_SHELL_TEXT			RGB(0xD4, 0xD4, 0xD4)
#define NP2_DARK_SHELL_HIGHLIGHT		RGB(0x3E, 0x3E, 0x42)
#define NP2_DARK_SHELL_DISABLED_TEXT	RGB(0x80, 0x80, 0x80)

void DarkMode_Init() noexcept;
bool DarkMode_VerifyMinimumWindowsVersion() noexcept;
DWORD DarkMode_GetWindowsBuild() noexcept;
bool DarkMode_IsHighContrastActive() noexcept;
bool DarkMode_ShouldApply(bool darkRequested) noexcept;
void DarkMode_SetPreferredAppMode(bool dark) noexcept;
void DarkMode_SetTitleBarDark(HWND hwnd, bool dark) noexcept;
void DarkMode_AllowWindow(HWND hwnd, bool dark) noexcept;
void DarkMode_SetWindowThemes(HWND hwnd, bool dark) noexcept;
void DarkMode_RefreshFrame(HWND hwnd) noexcept;
void DarkMode_ApplyToWindow(HWND hwnd, bool dark) noexcept;
void DarkMode_ApplyToCommCtrlBars(HWND hwndReBar, HWND hwndToolbar, HWND hwndStatus, bool dark) noexcept;
COLORREF DarkMode_GetShellBackgroundColor(bool dark) noexcept;
COLORREF DarkMode_GetShellTextColor(bool dark) noexcept;
HBRUSH DarkMode_GetCtlColorBrush(bool dark) noexcept;
void DarkMode_HandleCtlColor(HDC hdc, bool dark) noexcept;
bool DarkMode_IsWindowDark(HWND hwnd) noexcept;
void DarkMode_SetWindowDark(HWND hwnd, bool dark) noexcept;
void DarkMode_PaintClientTopSeparator(HWND hwnd) noexcept;

// UAH menu bar dark drawing (WM_UAHDRAWMENU / WM_UAHDRAWMENUITEM)
#ifndef WM_UAHDRAWMENU
#define WM_UAHDRAWMENU		0x0091
#endif
#ifndef WM_UAHDRAWMENUITEM
#define WM_UAHDRAWMENUITEM	0x0092
#endif

LRESULT DarkMode_HandleMenuDraw(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam, bool dark) noexcept;

void DarkMode_SetLogAppName(LPCWSTR appName) noexcept;
void DarkMode_WriteStartupLog(LPCWSTR appName, int styleTheme) noexcept;
void DarkMode_Log(LPCWSTR format, ...) noexcept;
void DarkMode_LogApply(HWND hwnd, bool darkRequested, LPCWSTR context) noexcept;
bool DarkMode_IsApplying() noexcept;

// Upstream dialog-hook API (dialog positioning / basic control init)
enum DialogRefData {
	DialogRefData_CenterParent,
	DialogRefData_MessageBox,
	DialogRefData_RightBottom,
	DialogRefData_DefaultPosition,
	DialogRefData_CustomizeToolbar,
	DialogRefData_MaxValue,
};

void DialogHook_Start(DWORD_PTR dwRefData) noexcept;
void DialogHook_Stop() noexcept;
void DarkMode_Cleanup() noexcept;
void DarkMode_InitDialog(HWND hwnd, DWORD_PTR dwRefData = DialogRefData_CenterParent) noexcept;
void DarkMode_InitTreeView(HWND hwndTV) noexcept;
void DarkMode_InitFileListView(HWND hwndLV, DWORD exStyle = 0) noexcept;

