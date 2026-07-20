// This file is part of Notepad.
// See License.txt for details about distribution and modification.

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdarg>
#include "DarkMode.h"
#include "Helpers.h"
#include "Dialogs.h"

#pragma comment(lib, "shlwapi.lib")

#ifndef NP2_COUNTOF
#define NP2_COUNTOF(ar) (sizeof(ar) / sizeof((ar)[0]))
#endif

#pragma comment(lib, "dwmapi.lib")

void DarkMode_Log(LPCWSTR format, ...) noexcept;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE	20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR				35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR				36
#endif

typedef BOOL(WINAPI *FnAllowDarkModeForWindow)(HWND hWnd, BOOL allow);
typedef BOOL(WINAPI *FnSetPreferredAppMode)(int preferredAppMode);
typedef void(WINAPI *FnRefreshImmersiveColorPolicyState)();
typedef BOOL(WINAPI *FnFlushMenuThemes)();

constexpr int PreferredAppMode_Default = 0;
constexpr int PreferredAppMode_AllowDark = 1;
constexpr int PreferredAppMode_ForceDark = 2;

static FnAllowDarkModeForWindow pfnAllowDarkModeForWindow;
static FnSetPreferredAppMode pfnSetPreferredAppMode;
static FnRefreshImmersiveColorPolicyState pfnRefreshImmersiveColorPolicyState;
static FnFlushMenuThemes pfnFlushMenuThemes;

static DWORD g_windowsBuild;
static HBRUSH g_hbrDarkCtl;
static bool g_darkModeInitialized;
static bool g_darkModeApplying;

static void DarkMode_LoadUxTheme() noexcept {
	if (g_darkModeInitialized) {
		return;
	}
	g_darkModeInitialized = true;

	const HMODULE hUxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (hUxTheme == nullptr) {
		DarkMode_Log(L"uxtheme.dll loaded: no");
		return;
	}

	pfnAllowDarkModeForWindow = reinterpret_cast<FnAllowDarkModeForWindow>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
	pfnSetPreferredAppMode = reinterpret_cast<FnSetPreferredAppMode>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
	pfnRefreshImmersiveColorPolicyState = reinterpret_cast<FnRefreshImmersiveColorPolicyState>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(104)));
	pfnFlushMenuThemes = reinterpret_cast<FnFlushMenuThemes>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136)));
	DarkMode_Log(L"uxtheme.dll loaded: yes");
}

namespace {

// Undocumented UAH menubar structures (see adzm/win32-custom-menubar-aero-theme).
typedef union tagUAHMENUITEMMETRICS {
	struct {
		DWORD cx;
		DWORD cy;
	} rgsizeBar[2];
	struct {
		DWORD cx;
		DWORD cy;
	} rgsizePopup[4];
} UAHMENUITEMMETRICS;

typedef struct tagUAHMENUPOPUPMETRICS {
	DWORD rgcx[4];
	DWORD fUpdateMaxWidths : 2;
} UAHMENUPOPUPMETRICS;

typedef struct tagUAHMENU {
	HMENU hmenu;
	HDC hdc;
	DWORD dwFlags;
} UAHMENU;

typedef struct tagUAHMENUITEM {
	int iPosition;
	UAHMENUITEMMETRICS umim;
	UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

typedef struct tagUAHDRAWMENUITEM {
	DRAWITEMSTRUCT dis;
	UAHMENU um;
	UAHMENUITEM umi;
} UAHDRAWMENUITEM;

BOOL CALLBACK DarkMode_EnumChildProc(HWND hwnd, LPARAM lParam) {
	const bool dark = lParam != 0;
	DarkMode_AllowWindow(hwnd, dark);
	DarkMode_SetWindowThemes(hwnd, dark);
	EnumChildWindows(hwnd, DarkMode_EnumChildProc, lParam);
	return TRUE;
}

static int DarkMode_GetMenuSeparatorOverlap(HWND hwnd) noexcept {
	const UINT dpi = GetDpiForWindow(hwnd);
	// Cover the NC separator above client top; scale with DPI for HiDPI displays.
	const int scaled = MulDiv(3, dpi, USER_DEFAULT_SCREEN_DPI);
	return (scaled > 2) ? scaled : 2;
}

static void DarkMode_GetClientRectInWindow(HWND hwnd, RECT *prc) noexcept {
	GetClientRect(hwnd, prc);
	MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT *>(prc), 2);
	RECT rcWindow;
	GetWindowRect(hwnd, &rcWindow);
	OffsetRect(prc, -rcWindow.left, -rcWindow.top);
}

static void DarkMode_FillMenuBarBackground(HWND hwnd, HDC hdc) noexcept {
	MENUBARINFO mbi = { sizeof(MENUBARINFO) };
	if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) {
		return;
	}

	RECT rcWindow;
	GetWindowRect(hwnd, &rcWindow);
	RECT rc = mbi.rcBar;
	OffsetRect(&rc, -rcWindow.left, -rcWindow.top);

	RECT rcClient;
	DarkMode_GetClientRectInWindow(hwnd, &rcClient);
	const int overlap = DarkMode_GetMenuSeparatorOverlap(hwnd);
	if (rc.bottom < rcClient.top + overlap) {
		rc.bottom = rcClient.top + overlap;
	}

	FillRect(hdc, &rc, DarkMode_GetCtlColorBrush(true));
}

enum : UINT_PTR {
	DarkMode_SubclassReBar = 1,
	DarkMode_SubclassToolbar,
	DarkMode_SubclassStatus,
};

static bool DarkMode_IsBarDark(HWND hwnd) noexcept {
	return GetPropW(hwnd, L"NP2DarkShell") != nullptr;
}

static void DarkMode_EnsureCommCtrlVersion(HWND hwnd) noexcept {
	if (hwnd != nullptr) {
		SendMessage(hwnd, CCM_SETVERSION, COMCTL32_VERSION, 0);
	}
}

static void DarkMode_PaintStatusBar(HWND hwnd, HDC hdc) noexcept {
	RECT rcClient;
	GetClientRect(hwnd, &rcClient);
	FillRect(hdc, &rcClient, DarkMode_GetCtlColorBrush(true));

	HFONT hFont = reinterpret_cast<HFONT>(SendMessage(hwnd, WM_GETFONT, 0, 0));
	if (hFont == nullptr) {
		hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
	}
	HGDIOBJ hOldFont = SelectObject(hdc, hFont);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, NP2_DARK_SHELL_TEXT);

	if (SendMessage(hwnd, SB_ISSIMPLE, 0, 0)) {
		WCHAR text[512];
		SendMessage(hwnd, SB_GETTEXT, 255, reinterpret_cast<LPARAM>(text));
		RECT rc = rcClient;
		InflateRect(&rc, -4, 0);
		DrawTextW(hdc, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
	} else {
		const int partCount = static_cast<int>(SendMessage(hwnd, SB_GETPARTS, 0, 0));
		for (int i = 0; i < partCount; ++i) {
			RECT rc;
			if (!SendMessage(hwnd, SB_GETRECT, i, reinterpret_cast<LPARAM>(&rc))) {
				continue;
			}
			WCHAR text[512] = L"";
			SendMessage(hwnd, SB_GETTEXT, i, reinterpret_cast<LPARAM>(text));
			RECT rcPart = rc;
			InflateRect(&rcPart, -4, 0);
			DrawTextW(hdc, text, -1, &rcPart, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
		}
	}

	SelectObject(hdc, hOldFont);
}

static LRESULT CALLBACK DarkMode_BarSubclassProc(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) noexcept {
	if (!DarkMode_IsBarDark(hwnd)) {
		return DefSubclassProc(hwnd, umsg, wParam, lParam);
	}

	switch (umsg) {
	case WM_ERASEBKGND:
		if (uIdSubclass != DarkMode_SubclassStatus) {
			RECT rc;
			GetClientRect(hwnd, &rc);
			FillRect(reinterpret_cast<HDC>(wParam), &rc, DarkMode_GetCtlColorBrush(true));
			return TRUE;
		}
		break;

	case WM_NCPAINT:
		if (uIdSubclass == DarkMode_SubclassReBar) {
			const LRESULT lr = DefSubclassProc(hwnd, umsg, wParam, lParam);
			HDC hdc = GetWindowDC(hwnd);
			if (hdc != nullptr) {
				RECT rc;
				GetWindowRect(hwnd, &rc);
				const int overlap = DarkMode_GetMenuSeparatorOverlap(GetAncestor(hwnd, GA_PARENT));
				const RECT rcTop = { rc.left, rc.top, rc.right, rc.top + overlap };
				FillRect(hdc, &rcTop, DarkMode_GetCtlColorBrush(true));
				ReleaseDC(hwnd, hdc);
			}
			return lr;
		}
		break;

	case WM_PAINT:
		if (uIdSubclass == DarkMode_SubclassStatus) {
			PAINTSTRUCT ps;
			const HDC hdc = BeginPaint(hwnd, &ps);
			DarkMode_PaintStatusBar(hwnd, hdc);
			EndPaint(hwnd, &ps);
			return 0;
		}
		break;

	case WM_NCDESTROY:
		RemoveWindowSubclass(hwnd, DarkMode_BarSubclassProc, uIdSubclass);
		break;
	}

	return DefSubclassProc(hwnd, umsg, wParam, lParam);
}

static void DarkMode_SubclassBar(HWND hwnd, UINT_PTR id) noexcept {
	if (hwnd != nullptr) {
		SetWindowSubclass(hwnd, DarkMode_BarSubclassProc, id, 0);
	}
}

} // namespace

WCHAR g_logPath[MAX_PATH];
bool g_logEnabled = true;

static void DarkMode_LogV(LPCWSTR format, va_list args) noexcept {
	if (!g_logEnabled || g_logPath[0] == L'\0') {
		return;
	}

	WCHAR line[1024];
	SYSTEMTIME st;
	GetLocalTime(&st);
	const int prefix = swprintf(line, NP2_COUNTOF(line),
		L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	if (prefix <= 0) {
		return;
	}
	const int body = vswprintf(line + prefix, NP2_COUNTOF(line) - prefix, format, args);
	if (body <= 0) {
		return;
	}
	const int len = prefix + body;
	if (len + 2 < static_cast<int>(NP2_COUNTOF(line))) {
		line[len] = L'\n';
		line[len + 1] = L'\0';
	}

	HANDLE hFile = CreateFileW(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		return;
	}
	DWORD written = 0;
	WriteFile(hFile, line, static_cast<DWORD>(len + 1) * sizeof(WCHAR), &written, nullptr);
	CloseHandle(hFile);
}

void DarkMode_SetLogAppName(LPCWSTR appName) noexcept {
	UNREFERENCED_PARAMETER(appName);
	WCHAR exePath[MAX_PATH];
	if (GetModuleFileNameW(nullptr, exePath, NP2_COUNTOF(exePath)) == 0) {
		g_logPath[0] = L'\0';
		return;
	}
	PathRemoveFileSpecW(exePath);
	PathCombineW(g_logPath, exePath, L"NP2DarkTheme.log");

	WCHAR env[8];
	g_logEnabled = (GetEnvironmentVariableW(L"NP2_DARK_LOG", env, NP2_COUNTOF(env)) == 0)
		|| (env[0] != L'0');
}

void DarkMode_Log(LPCWSTR format, ...) noexcept {
	va_list args;
	va_start(args, format);
	DarkMode_LogV(format, args);
	va_end(args);
}

void DarkMode_WriteStartupLog(LPCWSTR appName, int styleTheme) noexcept {
	DarkMode_Log(L"========== %s startup ==========", appName);
	DarkMode_Log(L"Windows build: %lu (min required: %u)", DarkMode_GetWindowsBuild(), NP2_MIN_WINDOWS_BUILD);
	DarkMode_Log(L"OS supported: %s", DarkMode_VerifyMinimumWindowsVersion() ? L"yes" : L"no");
	DarkMode_Log(L"High contrast: %s", DarkMode_IsHighContrastActive() ? L"on" : L"off");
	DarkMode_Log(L"IsAppThemed: %s", IsAppThemed() ? L"yes" : L"no");
	DarkMode_Log(L"StyleTheme ini value: %d (0=Default, 1=Dark)", styleTheme);
	DarkMode_Log(L"uxtheme AllowDarkModeForWindow(133): %s", pfnAllowDarkModeForWindow ? L"yes" : L"no");
	DarkMode_Log(L"uxtheme SetPreferredAppMode(135): %s", pfnSetPreferredAppMode ? L"yes" : L"no");
	DarkMode_Log(L"uxtheme RefreshImmersiveColorPolicyState(104): %s", pfnRefreshImmersiveColorPolicyState ? L"yes" : L"no");
	DarkMode_Log(L"uxtheme FlushMenuThemes(136): %s", pfnFlushMenuThemes ? L"yes" : L"no");
	DarkMode_Log(L"Log file: %s", g_logPath);
	DarkMode_Log(L"Disable logging: set NP2_DARK_LOG=0");
}

void DarkMode_LogApply(HWND hwnd, bool darkRequested, LPCWSTR context) noexcept {
	const bool applyDark = DarkMode_ShouldApply(darkRequested);
	DarkMode_Log(L"Apply [%s] hwnd=%p darkRequested=%d shouldApply=%d highContrast=%d",
		context, hwnd, darkRequested ? 1 : 0, applyDark ? 1 : 0, DarkMode_IsHighContrastActive() ? 1 : 0);
}

bool DarkMode_IsApplying() noexcept {
	return g_darkModeApplying;
}

void DarkMode_Init() noexcept {
	DarkMode_LoadUxTheme();
	g_windowsBuild = DarkMode_GetWindowsBuild();
	if (g_hbrDarkCtl == nullptr) {
		g_hbrDarkCtl = CreateSolidBrush(NP2_DARK_SHELL_BACKGROUND);
	}
}

bool DarkMode_VerifyMinimumWindowsVersion() noexcept {
	const DWORD build = DarkMode_GetWindowsBuild();
	return build >= NP2_MIN_WINDOWS_BUILD;
}

DWORD DarkMode_GetWindowsBuild() noexcept {
	if (g_windowsBuild != 0) {
		return g_windowsBuild;
	}

	using FnRtlGetVersion = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
	const HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
	if (hNtdll == nullptr) {
		return 0;
	}

	const auto pfnRtlGetVersion = reinterpret_cast<FnRtlGetVersion>(GetProcAddress(hNtdll, "RtlGetVersion"));
	if (pfnRtlGetVersion == nullptr) {
		return 0;
	}

	RTL_OSVERSIONINFOW versionInfo = {};
	versionInfo.dwOSVersionInfoSize = sizeof(versionInfo);
	if (pfnRtlGetVersion(&versionInfo) != 0) {
		return 0;
	}

	g_windowsBuild = versionInfo.dwBuildNumber;
	return g_windowsBuild;
}

bool DarkMode_IsHighContrastActive() noexcept {
	HIGHCONTRASTW hc = { sizeof(HIGHCONTRASTW) };
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) {
		return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
	}
	return false;
}

bool DarkMode_ShouldApply(bool darkRequested) noexcept {
	return darkRequested && !DarkMode_IsHighContrastActive();
}

void DarkMode_SetPreferredAppMode(bool dark) noexcept {
	DarkMode_LoadUxTheme();
	if (pfnSetPreferredAppMode != nullptr) {
		pfnSetPreferredAppMode(dark ? PreferredAppMode_AllowDark : PreferredAppMode_Default);
	}
	if (pfnRefreshImmersiveColorPolicyState != nullptr) {
		pfnRefreshImmersiveColorPolicyState();
	}
	if (pfnFlushMenuThemes != nullptr) {
		pfnFlushMenuThemes();
	}
}

void DarkMode_SetTitleBarDark(HWND hwnd, bool dark) noexcept {
	if (hwnd == nullptr) {
		return;
	}

	BOOL value = dark ? TRUE : FALSE;
	const HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
	DarkMode_Log(L"DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE) hwnd=%p dark=%d hr=0x%08lX", hwnd, dark ? 1 : 0, hr);

	if (dark && g_windowsBuild >= NP2_WIN11_BUILD) {
		const COLORREF caption = NP2_DARK_SHELL_BACKGROUND;
		const COLORREF text = NP2_DARK_SHELL_TEXT;
		DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
		DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
	} else if (!dark && g_windowsBuild >= NP2_WIN11_BUILD) {
		constexpr COLORREF colorDefault = 0xFFFFFFFF;
		DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &colorDefault, sizeof(colorDefault));
		DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &colorDefault, sizeof(colorDefault));
	}
}

void DarkMode_AllowWindow(HWND hwnd, bool dark) noexcept {
	DarkMode_LoadUxTheme();
	if (hwnd != nullptr && pfnAllowDarkModeForWindow != nullptr) {
		pfnAllowDarkModeForWindow(hwnd, dark ? TRUE : FALSE);
	}
}

void DarkMode_SetWindowThemes(HWND hwnd, bool dark) noexcept {
	if (hwnd == nullptr) {
		return;
	}

	WCHAR className[64];
	if (GetClassNameW(hwnd, className, NP2_COUNTOF(className)) == 0) {
		return;
	}

	LPCWSTR theme = dark ? L"DarkMode_Explorer" : L"Explorer";
	if (lstrcmpW(className, WC_COMBOBOXW) == 0) {
		theme = dark ? L"DarkMode_CFD" : L"";
	}

	SetWindowTheme(hwnd, theme, nullptr);
}

void DarkMode_RefreshFrame(HWND hwnd) noexcept {
	if (hwnd != nullptr) {
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}
}

void DarkMode_ApplyToWindow(HWND hwnd, bool dark) noexcept {
	if (hwnd == nullptr || g_darkModeApplying) {
		return;
	}

	g_darkModeApplying = true;
	const bool applyDark = DarkMode_ShouldApply(dark);
	DarkMode_LogApply(hwnd, dark, L"ApplyToWindow");
	DarkMode_SetPreferredAppMode(applyDark);
	DarkMode_SetTitleBarDark(hwnd, applyDark);
	DarkMode_AllowWindow(hwnd, applyDark);
	DarkMode_SetWindowThemes(hwnd, applyDark);
	EnumChildWindows(hwnd, DarkMode_EnumChildProc, applyDark ? 1 : 0);
	DarkMode_RefreshFrame(hwnd);
	InvalidateRect(hwnd, nullptr, TRUE);
	g_darkModeApplying = false;
}

COLORREF DarkMode_GetShellBackgroundColor(bool dark) noexcept {
	return dark ? NP2_DARK_SHELL_BACKGROUND : GetSysColor(COLOR_3DFACE);
}

COLORREF DarkMode_GetShellTextColor(bool dark) noexcept {
	return dark ? NP2_DARK_SHELL_TEXT : GetSysColor(COLOR_WINDOWTEXT);
}

HBRUSH DarkMode_GetCtlColorBrush(bool dark) noexcept {
	if (dark) {
		if (g_hbrDarkCtl == nullptr) {
			g_hbrDarkCtl = CreateSolidBrush(NP2_DARK_SHELL_BACKGROUND);
		}
		return g_hbrDarkCtl;
	}
	return static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
}

void DarkMode_HandleCtlColor(HDC hdc, bool dark) noexcept {
	if (!dark) {
		return;
	}
	SetTextColor(hdc, NP2_DARK_SHELL_TEXT);
	SetBkColor(hdc, NP2_DARK_SHELL_BACKGROUND);
	SetBkMode(hdc, OPAQUE);
}

bool DarkMode_IsWindowDark(HWND hwnd) noexcept {
	return GetPropW(hwnd, L"NP2DarkShell") != nullptr;
}

void DarkMode_SetWindowDark(HWND hwnd, bool dark) noexcept {
	if (dark) {
		SetPropW(hwnd, L"NP2DarkShell", reinterpret_cast<HANDLE>(1));
	} else {
		RemovePropW(hwnd, L"NP2DarkShell");
	}
}

void DarkMode_ApplyToCommCtrlBars(HWND hwndReBar, HWND hwndToolbar, HWND hwndStatus, bool dark) noexcept {
	const bool applyDark = DarkMode_ShouldApply(dark);
	const COLORREF bk = DarkMode_GetShellBackgroundColor(applyDark);

	auto applyBar = [&](HWND hwnd, UINT_PTR subclassId) noexcept {
		if (hwnd == nullptr) {
			return;
		}
		DarkMode_AllowWindow(hwnd, applyDark);
		DarkMode_SetWindowThemes(hwnd, applyDark);
		DarkMode_SetWindowDark(hwnd, applyDark);
		DarkMode_EnsureCommCtrlVersion(hwnd);
		DarkMode_SubclassBar(hwnd, subclassId);
	};

	applyBar(hwndReBar, DarkMode_SubclassReBar);
	applyBar(hwndToolbar, DarkMode_SubclassToolbar);
	applyBar(hwndStatus, DarkMode_SubclassStatus);

	if (hwndReBar != nullptr) {
		DWORD style = GetWindowLong(hwndReBar, GWL_STYLE);
		constexpr DWORD rebarBorderStyles = WS_BORDER | RBS_BANDBORDERS;
		if (applyDark) {
			SetWindowLong(hwndReBar, GWL_STYLE, style & ~rebarBorderStyles);
		} else {
			SetWindowLong(hwndReBar, GWL_STYLE, style | rebarBorderStyles);
		}
		SendMessage(hwndReBar, RB_SETBKCOLOR, 0, bk);
		if (applyDark) {
			COLORSCHEME cs = { sizeof(COLORSCHEME) };
			cs.clrBtnShadow = bk;
			cs.clrBtnHighlight = bk;
			SendMessage(hwndReBar, CCM_SETCOLORSCHEME, sizeof(COLORSCHEME), reinterpret_cast<LPARAM>(&cs));
		} else {
			SendMessage(hwndReBar, CCM_SETCOLORSCHEME, 0, 0);
		}
		REBARBANDINFO rbBand = { sizeof(REBARBANDINFO) };
		rbBand.fMask = RBBIM_STYLE | RBBIM_COLORS;
		if (SendMessage(hwndReBar, RB_GETBANDINFO, 0, reinterpret_cast<LPARAM>(&rbBand))) {
			if (applyDark) {
				rbBand.fStyle &= ~RBBS_CHILDEDGE;
				rbBand.clrFore = NP2_DARK_SHELL_TEXT;
				rbBand.clrBack = bk;
			} else if (IsAppThemed()) {
				rbBand.fStyle |= RBBS_CHILDEDGE;
			}
			SendMessage(hwndReBar, RB_SETBANDINFO, 0, reinterpret_cast<LPARAM>(&rbBand));
		}
		SetWindowPos(hwndReBar, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
		InvalidateRect(hwndReBar, nullptr, TRUE);
	}
	if (hwndToolbar != nullptr) {
		if (applyDark) {
			COLORSCHEME cs = { sizeof(COLORSCHEME) };
			cs.clrBtnShadow = bk;
			cs.clrBtnHighlight = bk;
			SendMessage(hwndToolbar, CCM_SETCOLORSCHEME, sizeof(COLORSCHEME), reinterpret_cast<LPARAM>(&cs));
		} else {
			SendMessage(hwndToolbar, CCM_SETCOLORSCHEME, 0, 0);
		}
		InvalidateRect(hwndToolbar, nullptr, TRUE);
	}
	if (hwndStatus != nullptr) {
		SendMessage(hwndStatus, SB_SETBKCOLOR, 0, bk);
		InvalidateRect(hwndStatus, nullptr, TRUE);
	}
}

LRESULT DarkMode_HandleMenuDraw(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam, bool dark) noexcept {
	if (!DarkMode_ShouldApply(dark)) {
		return DefWindowProcW(hwnd, umsg, wParam, lParam);
	}

	if (umsg == WM_UAHDRAWMENU) {
		const UAHMENU * const menu = reinterpret_cast<const UAHMENU *>(lParam);
		if (menu != nullptr && menu->hdc != nullptr) {
			DarkMode_FillMenuBarBackground(hwnd, menu->hdc);
			DarkMode_PaintClientTopSeparator(hwnd);
			return 0;
		}
	}

	if (umsg == WM_UAHDRAWMENUITEM) {
		const UAHDRAWMENUITEM * const pUDMI = reinterpret_cast<const UAHDRAWMENUITEM *>(lParam);
		if (pUDMI != nullptr && pUDMI->um.hmenu != nullptr) {
			const UINT state = pUDMI->dis.itemState;
			HBRUSH hbr = DarkMode_GetCtlColorBrush(true);
			if ((state & ODS_SELECTED) != 0 || (state & ODS_HOTLIGHT) != 0) {
				hbr = CreateSolidBrush(NP2_DARK_SHELL_HIGHLIGHT);
			}
			FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, hbr);
			if (hbr != DarkMode_GetCtlColorBrush(true)) {
				DeleteObject(hbr);
			}

			MENUITEMINFOW mii = { sizeof(MENUITEMINFOW) };
			mii.fMask = MIIM_STRING;
			WCHAR text[256];
			mii.dwTypeData = text;
			mii.cch = NP2_COUNTOF(text);
			if (GetMenuItemInfoW(pUDMI->um.hmenu, static_cast<UINT>(pUDMI->umi.iPosition), TRUE, &mii)) {
				const bool disabled = (state & (ODS_DISABLED | ODS_GRAYED)) != 0;
				SetTextColor(pUDMI->um.hdc, disabled ? NP2_DARK_SHELL_DISABLED_TEXT : NP2_DARK_SHELL_TEXT);
				SetBkMode(pUDMI->um.hdc, TRANSPARENT);
				UINT dtFlags = DT_SINGLELINE | DT_VCENTER | DT_CENTER;
				if ((state & ODS_NOACCEL) != 0) {
					dtFlags |= DT_HIDEPREFIX;
				}
				DrawTextW(pUDMI->um.hdc, text, -1, const_cast<LPRECT>(&pUDMI->dis.rcItem), dtFlags);
			}
			return 0;
		}
	}

	return DefWindowProcW(hwnd, umsg, wParam, lParam);
}

void DarkMode_PaintClientTopSeparator(HWND hwnd) noexcept {
	if (!DarkMode_ShouldApply(true)) {
		return;
	}

	RECT rcClient;
	DarkMode_GetClientRectInWindow(hwnd, &rcClient);
	const int overlap = DarkMode_GetMenuSeparatorOverlap(hwnd);

	RECT rcLine = rcClient;
	rcLine.bottom = rcLine.top;
	rcLine.top -= overlap;

	HDC hdc = GetWindowDC(hwnd);
	if (hdc == nullptr) {
		return;
	}
	FillRect(hdc, &rcLine, DarkMode_GetCtlColorBrush(true));
	ReleaseDC(hwnd, hdc);
}


namespace { // DialogHook

struct DialogHook {
	HHOOK hook;
	DWORD_PTR dwRefData;

	void Start() noexcept {
		hook = SetWindowsHookEx(WH_CALLWNDPROCRET, DialogHook::HookProc, nullptr, GetCurrentThreadId());
	}
	void Stop() noexcept {
		HHOOK current = hook;
		if (current) {
			hook = nullptr;
			UnhookWindowsHookEx(current);
		}
	}
	static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) noexcept;
};

DialogHook dialogHook {nullptr, DialogRefData_CenterParent};
LRESULT CALLBACK DialogHook::HookProc(int nCode, WPARAM wParam, LPARAM lParam) noexcept {
	if (nCode == HC_ACTION) {
		const auto *cwpret = AsPointer<const CWPRETSTRUCT *>(lParam);
		if (cwpret->message == WM_INITDIALOG) {
			const auto current = dialogHook;
			memset(&dialogHook, 0, sizeof(dialogHook));
			UnhookWindowsHookEx(current.hook);
			if (current.dwRefData > DialogRefData_MaxValue) {
				SetWindowSubclass(cwpret->hwnd, FileDialog::SubProc, 0, current.dwRefData);
			} else {
				DarkMode_InitDialog(cwpret->hwnd, current.dwRefData);
			}
		}
	}
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}

void DialogHook_Start(DWORD_PTR dwRefData) noexcept {
	dialogHook.dwRefData = dwRefData;
	dialogHook.Start();
}

void DialogHook_Stop() noexcept {
	dialogHook.Stop();
}

void DarkMode_Cleanup() noexcept {
}

void DarkMode_InitDialog(HWND hwnd, DWORD_PTR dwRefData) noexcept {
	if (dwRefData >= DialogRefData_DefaultPosition) {
		return;
	}
	if (dwRefData != DialogRefData_RightBottom) {
		CenterDlgInParent(hwnd);
		if (dwRefData == DialogRefData_MessageBox) {
			SnapToDefaultButton(hwnd);
		}
	} else {
		SetToRightBottom(hwnd);
	}
}

void DarkMode_InitTreeView(HWND hwndTV) noexcept {
	InitWindowCommon(hwndTV);
	TreeView_SetExtendedStyle(hwndTV, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
	SetWindowTheme(hwndTV, L"Explorer", nullptr);
}

void DarkMode_InitFileListView(HWND hwndLV, DWORD exStyle) noexcept {
	InitWindowCommon(hwndLV);
	exStyle |= LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP;
	ListView_SetExtendedListViewStyle(hwndLV, exStyle);
	if (exStyle & LVS_EX_FULLROWSELECT) {
		SetWindowTheme(hwndLV, L"Explorer", nullptr);
	}

	LVCOLUMN lvc{};
	lvc.mask = LVCF_FMT | LVCF_TEXT;
	lvc.fmt = LVCFMT_LEFT;
	ListView_InsertColumn(hwndLV, 0, &lvc);
}

