// This file is part of Notepad.
// Preview Mode: WebView2 host with Markdown/HTML/XML/CSV preview and Mermaid diagrams.

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <ole2.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>

#include "config.h"
#include "SciCall.h"
#include "Helpers.h"
#ifndef kMaxMultiByteCount
#define kMaxMultiByteCount	3
#endif
#include "EditLexer.h"
#include "Styles.h"
#include "DarkMode.h"
#include "Notepad4.h"
#include "PreviewMode.h"
#include "md4c/md4c.h"

#if defined(_MSC_VER)
#include <wrl.h>
#include <wrl/event.h>
#include <WebView2.h>
#define NP2_USE_WEBVIEW2 1
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
#endif

extern "C" int __cdecl md_html(const MD_CHAR *input, MD_SIZE input_size,
	void (*process_output)(const MD_CHAR *, MD_SIZE, void *),
	void *userdata, unsigned parser_flags, unsigned renderer_flags);

extern HWND hwndEdit;

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")
#if defined(NP2_USE_WEBVIEW2)
#pragma comment(lib, "version.lib")
#endif

#ifndef NP2_COUNTOF
#define NP2_COUNTOF(ar) (sizeof(ar) / sizeof((ar)[0]))
#endif

#define PREVIEW_LAYOUT_RETRY_MS		16
#define PREVIEW_DEBOUNCE_MS			420
#define PREVIEW_DEBOUNCE_FIRST_MS	80
#define PREVIEW_DEBOUNCE_MERMAID_MS	560
#define PREVIEW_MAX_TEXT_BYTES		(2 * 1024 * 1024)
#define PREVIEW_INIT_TIMEOUT_MS		15000
#define PREVIEW_SPLITTER_CY			5
#define PREVIEW_MIN_PANE_CY			40
#define PREVIEW_DEFAULT_PERCENT		50
#define PREVIEW_ZOOM_MIN			50
#define PREVIEW_ZOOM_MAX			250
#define PREVIEW_ZOOM_STEP			10
#define PREVIEW_ZOOM_DEFAULT		100

#define ID_PREVIEW_CTX_COPY			0xFB30
#define ID_PREVIEW_CTX_SELECTALL	0xFB31
#define ID_PREVIEW_CTX_OPEN_LINK	0xFB32
#define ID_PREVIEW_CTX_COPY_LINK	0xFB33

constexpr WCHAR PREVIEW_SPLITTER_CLASS[] = L"NP2PreviewSplitter";
constexpr WCHAR PREVIEW_CONTAINER_CLASS[] = L"NP2PreviewContainer";
constexpr WCHAR PREVIEW_VIRTUAL_HOST[] = L"np2.preview";

namespace {

HWND g_hwndMain;
HWND g_hwndContainer;
HWND g_hwndSplitter;

bool g_active;
bool g_autoEnable;
bool g_dirty;
unsigned g_dirtySerial;
bool g_updatePosted;
bool g_inUpdate;

int g_previewPercent = PREVIEW_DEFAULT_PERCENT;
int g_savedPreviewPercent = PREVIEW_DEFAULT_PERCENT;
bool g_previewMaximized;
bool g_splitterDragging;
int g_splitterDragStartY;
int g_splitterDragStartPercent;

int g_lastLayoutX;
int g_lastLayoutY;
int g_lastLayoutCx;
int g_lastLayoutCy;

int g_previewZoomPercent = PREVIEW_ZOOM_DEFAULT;

bool g_splitterClassRegistered;
bool g_containerClassRegistered;
HBRUSH g_previewContainerBrush;

WCHAR g_previewLogPath[MAX_PATH];
bool g_previewLogEnabled = true;

WCHAR g_previewAssetsDir[MAX_PATH];
WCHAR g_webviewUserDataDir[MAX_PATH];
bool g_lastHadMermaid;

#if defined(NP2_USE_WEBVIEW2)
ComPtr<ICoreWebView2Environment> g_env;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
bool g_webviewCreating;
bool g_webviewReady;
bool g_webviewFailed;
bool g_virtualHostMapped;
EventRegistrationToken g_tokenNavigationStarting {};
EventRegistrationToken g_tokenNewWindowRequested {};
EventRegistrationToken g_tokenContextMenuRequested {};
EventRegistrationToken g_tokenAcceleratorKeyPressed {};
EventRegistrationToken g_tokenNavigationCompleted {};
std::wstring g_pendingHtml;
std::wstring g_pendingBody;
std::wstring g_contextLinkUrl;
bool g_contextHasSelection;
bool g_shellReady;
bool g_shellDark;
bool g_webviewBgEnvSet;
#endif

void PreviewMode_LogV(LPCWSTR format, va_list args) noexcept {
	if (!g_previewLogEnabled || g_previewLogPath[0] == L'\0') {
		return;
	}
	WCHAR line[1024];
	SYSTEMTIME st;
	GetLocalTime(&st);
	const int prefix = swprintf(line, NP2_COUNTOF(line),
		L"[%04u-%02u-%02u %02u:%02u:%02u.%03u tid=%lu] ",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		GetCurrentThreadId());
	if (prefix <= 0) {
		return;
	}
	const int body = vswprintf(line + prefix, NP2_COUNTOF(line) - prefix, format, args);
	if (body <= 0) {
		return;
	}
	const int len = prefix + body;
	if (len + 2 >= static_cast<int>(NP2_COUNTOF(line))) {
		return;
	}
	line[len] = L'\n';
	line[len + 1] = L'\0';

	HANDLE hFile = CreateFileW(g_previewLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		return;
	}
	char utf8[2048];
	const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line, len + 1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
	DWORD written = 0;
	if (utf8Len > 0) {
		WriteFile(hFile, utf8, static_cast<DWORD>(utf8Len), &written, nullptr);
	}
	FlushFileBuffers(hFile);
	CloseHandle(hFile);
}

void PreviewMode_Log(LPCWSTR format, ...) noexcept {
	va_list args;
	va_start(args, format);
	PreviewMode_LogV(format, args);
	va_end(args);
}

void PreviewMode_InitLog() noexcept {
	WCHAR exePath[MAX_PATH];
	if (GetModuleFileNameW(nullptr, exePath, NP2_COUNTOF(exePath)) == 0) {
		g_previewLogPath[0] = L'\0';
		return;
	}
	PathRemoveFileSpecW(exePath);
	PathCombineW(g_previewLogPath, exePath, L"NotepadPreview.log");
	PathCombineW(g_previewAssetsDir, exePath, L"preview");
	PathCombineW(g_webviewUserDataDir, exePath, L"WebView2Data");

	WCHAR env[8];
	g_previewLogEnabled = (GetEnvironmentVariableW(L"NP2_PREVIEW_LOG", env, NP2_COUNTOF(env)) == 0)
		|| (env[0] != L'0');

	DeleteFileW(g_previewLogPath);
	PreviewMode_Log(L"========== PreviewMode log started ==========");
	PreviewMode_Log(L"Log file: %s", g_previewLogPath);
	PreviewMode_Log(L"Assets: %s", g_previewAssetsDir);
}

void ShowContainer(bool show) noexcept {
	if (g_hwndContainer) {
		ShowWindow(g_hwndContainer, show ? SW_SHOW : SW_HIDE);
	}
}

void ShowSplitter(bool show) noexcept {
	if (g_hwndSplitter) {
		ShowWindow(g_hwndSplitter, show ? SW_SHOW : SW_HIDE);
	}
}

void RequestRelayout() noexcept {
	if (g_hwndMain) {
		PostMessage(g_hwndMain, APPM_PREVIEW_RELAYOUT, 0, 0);
	}
}

void SchedulePreviewUpdate() noexcept {
	if (!g_active || g_hwndMain == nullptr) {
		PreviewMode_Log(L"SchedulePreviewUpdate skipped active=%d hwnd=%p", g_active, g_hwndMain);
		return;
	}
	UINT delay = PREVIEW_DEBOUNCE_MS;
#if defined(NP2_USE_WEBVIEW2)
	if (!g_shellReady) {
		delay = PREVIEW_DEBOUNCE_FIRST_MS;
	} else if (g_lastHadMermaid) {
		delay = PREVIEW_DEBOUNCE_MERMAID_MS;
	}
#endif
	SetTimer(g_hwndMain, ID_PREVIEW_TIMER, delay, nullptr);
}

bool IsPreviewDarkTheme() noexcept {
	return np2StyleTheme == StyleTheme_Dark;
}

COLORREF GetPreviewBackgroundColor() noexcept {
	return IsPreviewDarkTheme()
		? RGB(0x0D, 0x11, 0x17)
		: RGB(0xFF, 0xFF, 0xFF);
}

#if defined(NP2_USE_WEBVIEW2)
COREWEBVIEW2_COLOR MakePreviewWebViewColor() noexcept {
	COREWEBVIEW2_COLOR color {};
	color.A = 255;
	if (IsPreviewDarkTheme()) {
		color.R = 0x0D;
		color.G = 0x11;
		color.B = 0x17;
	} else {
		color.R = 0xFF;
		color.G = 0xFF;
		color.B = 0xFF;
	}
	return color;
}

void SetPreviewWebViewEnvBackground() noexcept {
	if (g_webviewBgEnvSet) {
		return;
	}
	const LPCWSTR value = IsPreviewDarkTheme() ? L"FF0D1117" : L"FFFFFFFF";
	SetEnvironmentVariableW(L"WEBVIEW2_DEFAULT_BACKGROUND_COLOR", value);
	g_webviewBgEnvSet = true;
	PreviewMode_Log(L"WEBVIEW2_DEFAULT_BACKGROUND_COLOR=%s", value);
}
#endif

void UpdatePreviewContainerBackground() noexcept {
	if (g_previewContainerBrush != nullptr) {
		DeleteObject(g_previewContainerBrush);
		g_previewContainerBrush = nullptr;
	}
	const COLORREF color = GetPreviewBackgroundColor();
	g_previewContainerBrush = CreateSolidBrush(color);
	if (g_hwndContainer) {
		InvalidateRect(g_hwndContainer, nullptr, TRUE);
	}
}

bool IsPreviewFocus() noexcept {
	if (g_hwndContainer == nullptr) {
		return false;
	}
	const HWND focus = GetFocus();
	if (focus == nullptr) {
		return false;
	}
	return focus == g_hwndContainer || IsChild(g_hwndContainer, focus);
}

void OpenUrl(LPCWSTR url) noexcept {
	if (url == nullptr || url[0] == L'\0') {
		return;
	}
	ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

bool IsHttpOrHttpsUrl(LPCWSTR url) noexcept {
	if (url == nullptr || url[0] == L'\0') {
		return false;
	}
	return (_wcsnicmp(url, L"http://", 7) == 0) || (_wcsnicmp(url, L"https://", 8) == 0);
}

bool IsAllowedPreviewNavigation(LPCWSTR url) noexcept {
	if (url == nullptr || url[0] == L'\0') {
		return true;
	}
	if (url[0] == L'#') {
		return true;
	}
	if (_wcsicmp(url, L"about:blank") == 0) {
		return true;
	}
	if (wcsncmp(url, L"about:blank", 11) == 0 && url[11] == L'#') {
		return true;
	}
	if (_wcsnicmp(url, L"data:", 5) == 0) {
		return true;
	}
	// Virtual host for Mermaid / local preview assets.
	if (_wcsnicmp(url, L"https://np2.preview/", 20) == 0
		|| _wcsnicmp(url, L"http://np2.preview/", 19) == 0) {
		return true;
	}
	return false;
}

void CopyTextToClipboard(HWND hwnd, LPCWSTR text) noexcept {
	if (text == nullptr) {
		return;
	}
	const size_t len = wcslen(text);
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(WCHAR));
	if (hMem == nullptr) {
		return;
	}
	LPWSTR dest = static_cast<LPWSTR>(GlobalLock(hMem));
	if (dest == nullptr) {
		GlobalFree(hMem);
		return;
	}
	memcpy(dest, text, (len + 1) * sizeof(WCHAR));
	GlobalUnlock(hMem);
	if (!OpenClipboard(hwnd)) {
		GlobalFree(hMem);
		return;
	}
	EmptyClipboard();
	SetClipboardData(CF_UNICODETEXT, hMem);
	CloseClipboard();
}

void AppendHtmlEscapedUtf8(const char *text, size_t len, LPWSTR &dst, size_t &remaining) noexcept {
	size_t i = 0;
	while (i < len && remaining > 8) {
		const unsigned char ch = static_cast<unsigned char>(text[i]);
		LPCWSTR repl = nullptr;
		switch (ch) {
		case '&': repl = L"&amp;"; break;
		case '<': repl = L"&lt;"; break;
		case '>': repl = L"&gt;"; break;
		case '"': repl = L"&quot;"; break;
		default: break;
		}
		if (repl != nullptr) {
			while (*repl && remaining > 1) {
				*dst++ = *repl++;
				--remaining;
			}
			++i;
			continue;
		}
		if (ch < 0x80) {
			*dst++ = static_cast<WCHAR>(ch);
			--remaining;
			++i;
			continue;
		}
		int seq = 1;
		if ((ch & 0xE0) == 0xC0) {
			seq = 2;
		} else if ((ch & 0xF0) == 0xE0) {
			seq = 3;
		} else if ((ch & 0xF8) == 0xF0) {
			seq = 4;
		}
		if (i + static_cast<size_t>(seq) > len) {
			break;
		}
		WCHAR wbuf[2] {};
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, text + i, seq, wbuf, 2);
		if (wlen <= 0) {
			++i;
			continue;
		}
		for (int k = 0; k < wlen && remaining > 1; ++k) {
			*dst++ = wbuf[k];
			--remaining;
		}
		i += static_cast<size_t>(seq);
	}
}

void AppendPreviewShell(LPWSTR &dst, size_t &remaining, LPCWSTR body, bool dark, bool withMermaid) noexcept {
	(void)dark;
	(void)withMermaid;
	while (*body && remaining > 1) {
		*dst++ = *body++;
		--remaining;
	}
}

void ClosePreviewShell(LPWSTR &dst, size_t &remaining, bool dark, bool withMermaid) noexcept {
	(void)dark;
	(void)withMermaid;
	*dst = L'\0';
	(void)remaining;
}

void BuildPreviewShellDocument(LPWSTR html, size_t htmlCch, bool dark) noexcept {
	static const WCHAR shellLight[] =
		L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><base target=\"_self\"><style>"
		L"html,body{min-height:100%;background:#fff;}"
		L"body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Helvetica,Arial,sans-serif;font-size:16px;"
		L"line-height:1.6;word-wrap:break-word;margin:16px 24px;color:#24292f;background:#fff;}"
		L"h1,h2,h3,h4,h5,h6{margin-top:24px;margin-bottom:16px;font-weight:600;line-height:1.25;}"
		L"h1{font-size:2em;border-bottom:1px solid #d0d7de;padding-bottom:.3em;}"
		L"h2{font-size:1.5em;border-bottom:1px solid #d0d7de;padding-bottom:.3em;}"
		L"h3{font-size:1.25em;}h4{font-size:1em;}h5{font-size:.875em;}h6{font-size:.85em;color:#57606a;}"
		L"p,ul,ol,dl,table,pre,blockquote{margin-top:0;margin-bottom:16px;}"
		L"ul,ol{padding-left:2em;}li+li{margin-top:.25em;}"
		L"li.task-list-item{list-style-type:none;}li.task-list-item input{margin:0 .2em .25em -1.6em;vertical-align:middle;}"
		L"code,tt{font-family:Consolas,Courier New,monospace;font-size:85%;background:#f6f8fa;padding:.2em .4em;border-radius:6px;}"
		L"pre{font-family:Consolas,Courier New,monospace;font-size:85%;background:#f6f8fa;padding:16px;overflow:auto;border-radius:6px;line-height:1.45;}"
		L"pre code{background:transparent;padding:0;font-size:100%;}"
		L"blockquote{border-left:4px solid #d0d7de;margin:0;padding:0 1em;color:#57606a;}"
		L"table{border-collapse:collapse;width:100%;margin:16px 0;display:block;overflow:auto;}"
		L"th,td{border:1px solid #d0d7de;padding:6px 13px;}th{font-weight:600;background:#f6f8fa;}"
		L"tr:nth-child(2n){background:#f6f8fa;}hr{border:0;border-top:1px solid #d0d7de;height:0;margin:24px 0;}"
		L"a{color:#0969da;text-decoration:none;}a:hover{text-decoration:underline;}"
		L"img{max-width:100%;box-sizing:content-box;}del{opacity:.8;}"
		L".mermaid{background:transparent;text-align:center;}"
		L"#np2-content{min-height:1px;}"
		L"html,body,*{-ms-user-select:text;user-select:text;}"
		L"::selection{background:#B3D4FC;}"
		L"</style></head><body><div id=\"np2-content\"></div>"
		L"<script src=\"https://np2.preview/mermaid.min.js\"></script>"
		L"<script>(function(){"
		L"var dark=0;"
		L"function bootMermaid(){try{if(window.mermaid){mermaid.initialize({startOnLoad:false,securityLevel:'loose',theme:dark?'dark':'default'});}}catch(e){}}"
		L"function convertMermaid(root){root.querySelectorAll('pre code.language-mermaid').forEach(function(code){"
		L"var pre=code.parentElement;if(!pre)return;var div=document.createElement('div');div.className='mermaid';"
		L"div.textContent=code.textContent;pre.replaceWith(div);});}"
		L"async function np2Apply(html){var root=document.getElementById('np2-content');if(!root)return;"
		L"var y=window.scrollY||document.documentElement.scrollTop||0;"
		L"root.innerHTML=html||'';convertMermaid(root);"
		L"try{bootMermaid();if(window.mermaid){await mermaid.run({querySelector:'#np2-content .mermaid'});}}catch(e){}"
		L"window.scrollTo(0,y);}"
		L"bootMermaid();"
		L"if(window.chrome&&window.chrome.webview){window.chrome.webview.addEventListener('message',function(e){"
		L"var data=e.data;np2Apply(typeof data==='string'?data:(data&&data.html)||'');});}"
		L"})();</script></body></html>";
	static const WCHAR shellDark[] =
		L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><base target=\"_self\"><style>"
		L"html,body{min-height:100%;background:#0d1117;}"
		L"body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Helvetica,Arial,sans-serif;font-size:16px;"
		L"line-height:1.6;word-wrap:break-word;margin:16px 24px;color:#c9d1d9;background:#0d1117;}"
		L"h1,h2,h3,h4,h5,h6{margin-top:24px;margin-bottom:16px;font-weight:600;line-height:1.25;color:#e6edf3;}"
		L"h1{font-size:2em;border-bottom:1px solid #30363d;padding-bottom:.3em;}"
		L"h2{font-size:1.5em;border-bottom:1px solid #30363d;padding-bottom:.3em;}"
		L"h3{font-size:1.25em;}h4{font-size:1em;}h5{font-size:.875em;}h6{font-size:.85em;color:#8b949e;}"
		L"p,ul,ol,dl,table,pre,blockquote{margin-top:0;margin-bottom:16px;}"
		L"ul,ol{padding-left:2em;}li+li{margin-top:.25em;}"
		L"li.task-list-item{list-style-type:none;}li.task-list-item input{margin:0 .2em .25em -1.6em;vertical-align:middle;}"
		L"code,tt{font-family:Consolas,Courier New,monospace;font-size:85%;background:#161b22;padding:.2em .4em;border-radius:6px;}"
		L"pre{font-family:Consolas,Courier New,monospace;font-size:85%;background:#161b22;padding:16px;overflow:auto;border-radius:6px;line-height:1.45;}"
		L"pre code{background:transparent;padding:0;font-size:100%;}"
		L"blockquote{border-left:4px solid #3b434b;margin:0;padding:0 1em;color:#8b949e;}"
		L"table{border-collapse:collapse;width:100%;margin:16px 0;display:block;overflow:auto;}"
		L"th,td{border:1px solid #30363d;padding:6px 13px;}th{font-weight:600;background:#161b22;}"
		L"tr:nth-child(2n){background:#161b22;}hr{border:0;border-top:1px solid #30363d;height:0;margin:24px 0;}"
		L"a{color:#58a6ff;text-decoration:none;}a:hover{text-decoration:underline;}"
		L"img{max-width:100%;box-sizing:content-box;}del{opacity:.8;}"
		L".mermaid{background:transparent;text-align:center;}"
		L"#np2-content{min-height:1px;}"
		L"html,body,*{-ms-user-select:text;user-select:text;}"
		L"::selection{background:#264F78;}"
		L"</style></head><body><div id=\"np2-content\"></div>"
		L"<script src=\"https://np2.preview/mermaid.min.js\"></script>"
		L"<script>(function(){"
		L"var dark=1;"
		L"function bootMermaid(){try{if(window.mermaid){mermaid.initialize({startOnLoad:false,securityLevel:'loose',theme:dark?'dark':'default'});}}catch(e){}}"
		L"function convertMermaid(root){root.querySelectorAll('pre code.language-mermaid').forEach(function(code){"
		L"var pre=code.parentElement;if(!pre)return;var div=document.createElement('div');div.className='mermaid';"
		L"div.textContent=code.textContent;pre.replaceWith(div);});}"
		L"async function np2Apply(html){var root=document.getElementById('np2-content');if(!root)return;"
		L"var y=window.scrollY||document.documentElement.scrollTop||0;"
		L"root.innerHTML=html||'';convertMermaid(root);"
		L"try{bootMermaid();if(window.mermaid){await mermaid.run({querySelector:'#np2-content .mermaid'});}}catch(e){}"
		L"window.scrollTo(0,y);}"
		L"bootMermaid();"
		L"if(window.chrome&&window.chrome.webview){window.chrome.webview.addEventListener('message',function(e){"
		L"var data=e.data;np2Apply(typeof data==='string'?data:(data&&data.html)||'');});}"
		L"})();</script></body></html>";
	LPCWSTR src = dark ? shellDark : shellLight;
	wcsncpy(html, src, htmlCch - 1);
	html[htmlCch - 1] = L'\0';
}

struct MdHtmlBuffer {
	char *data = nullptr;
	size_t len = 0;
	size_t cap = 0;
	bool oom = false;
};

bool MdHtmlBufferGrow(MdHtmlBuffer *buf, size_t add) noexcept {
	const size_t need = buf->len + add + 1;
	if (need <= buf->cap) {
		return true;
	}
	size_t newCap = buf->cap ? buf->cap : 4096;
	while (newCap < need) {
		newCap *= 2;
	}
	char *next = static_cast<char *>(NP2HeapAlloc(newCap));
	if (next == nullptr) {
		buf->oom = true;
		return false;
	}
	if (buf->data != nullptr) {
		memcpy(next, buf->data, buf->len);
		NP2HeapFree(buf->data);
	}
	buf->data = next;
	buf->cap = newCap;
	return true;
}

void MdHtmlAppendOutput(const MD_CHAR *text, MD_SIZE size, void *userdata) noexcept {
	auto *buf = static_cast<MdHtmlBuffer *>(userdata);
	if (buf->oom || size == 0) {
		return;
	}
	if (!MdHtmlBufferGrow(buf, size)) {
		return;
	}
	memcpy(buf->data + buf->len, text, size);
	buf->len += size;
	buf->data[buf->len] = '\0';
}

void AppendWideFromUtf8(LPWSTR &dst, size_t &remaining, const char *utf8, size_t utf8Len) noexcept {
	if (utf8Len == 0 || remaining <= 1 || utf8 == nullptr) {
		return;
	}
	while (utf8Len > 0 && remaining > 1) {
		const int chunk = static_cast<int>(min<size_t>(utf8Len, 32768));
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, chunk, dst, static_cast<int>(remaining - 1));
		if (wlen <= 0) {
			break;
		}
		dst += wlen;
		remaining -= static_cast<size_t>(wlen);
		utf8 += chunk;
		utf8Len -= static_cast<size_t>(chunk);
	}
}

bool Utf8ContainsMermaidFence(const char *text, size_t len) noexcept {
	if (text == nullptr || len < 10) {
		return false;
	}
	const char *end = text + len;
	for (const char *p = text; p + 10 <= end; ++p) {
		if (p[0] == '`' && p[1] == '`' && p[2] == '`') {
			const char *lang = p + 3;
			while (lang < end && (*lang == ' ' || *lang == '\t')) {
				++lang;
			}
			if (lang + 7 <= end && _strnicmp(lang, "mermaid", 7) == 0) {
				const char next = (lang + 7 < end) ? lang[7] : '\0';
				if (next == '\0' || next == '\r' || next == '\n' || next == ' ' || next == '\t') {
					return true;
				}
			}
		}
	}
	return false;
}

void MarkdownToHtml(const char *text, size_t len, LPWSTR html, size_t htmlCch) noexcept {
	LPWSTR dst = html;
	size_t remaining = htmlCch;
	html[0] = L'\0';

	MdHtmlBuffer body;
	body.cap = max<size_t>(len * 3, 8192);
	body.data = static_cast<char *>(NP2HeapAlloc(body.cap));
	if (body.data != nullptr) {
		body.data[0] = '\0';
		const unsigned parserFlags = MD_DIALECT_GITHUB;
		const int rc = md_html(text, static_cast<MD_SIZE>(len), MdHtmlAppendOutput, &body, parserFlags, 0);
		if (rc == 0 && body.len > 0 && !body.oom) {
			AppendWideFromUtf8(dst, remaining, body.data, body.len);
		} else {
			PreviewMode_Log(L"MarkdownToHtml md_html failed rc=%d len=%zu oom=%d", rc, body.len, body.oom);
			AppendHtmlEscapedUtf8(text, len, dst, remaining);
		}
		NP2HeapFree(body.data);
	} else {
		AppendHtmlEscapedUtf8(text, len, dst, remaining);
	}
	*dst = L'\0';
	const bool withMermaid = Utf8ContainsMermaidFence(text, len);
	g_lastHadMermaid = withMermaid;
	PreviewMode_Log(L"MarkdownToHtml body done mermaid=%d", withMermaid ? 1 : 0);
}

void CsvToHtml(const char *text, size_t len, LPWSTR html, size_t htmlCch) noexcept {
	LPWSTR dst = html;
	size_t remaining = htmlCch;
	g_lastHadMermaid = false;
	const WCHAR openTable[] = L"<table>";
	for (size_t i = 0; openTable[i] && remaining > 1; ++i) {
		*dst++ = openTable[i];
		--remaining;
	}

	const char *lineStart = text;
	const char *end = text + len;
	bool firstRow = true;
	while (lineStart < end && remaining > 16) {
		const char *lineEnd = lineStart;
		while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r') {
			++lineEnd;
		}
		if (lineEnd > lineStart) {
			LPCWSTR rowOpen = firstRow ? L"<tr><th>" : L"<tr><td>";
			LPCWSTR cellSep = firstRow ? L"</th><th>" : L"</td><td>";
			LPCWSTR rowClose = firstRow ? L"</th></tr>" : L"</td></tr>";
			firstRow = false;

			const char *cellStart = lineStart;
			for (const char *p = lineStart; p <= lineEnd; ++p) {
				if (p == lineEnd || *p == ',') {
					for (LPCWSTR s = rowOpen; *s && remaining > 1; ++s) {
						*dst++ = *s;
						--remaining;
					}
					rowOpen = L"";
					AppendHtmlEscapedUtf8(cellStart, static_cast<size_t>(p - cellStart), dst, remaining);
					cellStart = p + 1;
					if (p < lineEnd) {
						for (LPCWSTR s = cellSep; *s && remaining > 1; ++s) {
							*dst++ = *s;
							--remaining;
						}
					}
				}
			}
			for (LPCWSTR s = rowClose; *s && remaining > 1; ++s) {
				*dst++ = *s;
				--remaining;
			}
		}
		lineStart = lineEnd;
		if (lineStart < end && *lineStart == '\r') {
			++lineStart;
		}
		if (lineStart < end && *lineStart == '\n') {
			++lineStart;
		}
	}
	const WCHAR closeTable[] = L"</table>";
	for (size_t i = 0; closeTable[i] && remaining > 1; ++i) {
		*dst++ = closeTable[i];
		--remaining;
	}
	*dst = L'\0';
}

void WrapDocumentHtml(const char *text, size_t len, LPWSTR html, size_t htmlCch) noexcept {
	g_lastHadMermaid = false;
	if (len >= 5 && (_strnicmp(text, "<html", 5) == 0 || _strnicmp(text, "<!DOC", 5) == 0)) {
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(len), html, static_cast<int>(htmlCch - 1));
		if (wlen > 0) {
			html[wlen] = L'\0';
		} else {
			html[0] = L'\0';
		}
		return;
	}
	LPWSTR dst = html;
	size_t remaining = htmlCch;
	AppendHtmlEscapedUtf8(text, len, dst, remaining);
	*dst = L'\0';
}

void GetDocumentUtf8(char *text, size_t textCapacity) noexcept {
	if (textCapacity == 0) {
		return;
	}
	const Sci_Position length = SciCall_GetLength();
	if (length <= 0) {
		text[0] = '\0';
		return;
	}
	const Sci_Position toGet = min<Sci_Position>(length, static_cast<Sci_Position>(textCapacity - 1));
	const Sci_Position maxBytes = min<Sci_Position>(toGet, PREVIEW_MAX_TEXT_BYTES);
	SciCall_GetText(maxBytes + 1, text);
	text[maxBytes] = '\0';

	const UINT cp = SciCall_GetCodePage();
	if (cp == SC_CP_UTF8) {
		return;
	}

	WCHAR *wide = static_cast<WCHAR *>(NP2HeapAlloc((maxBytes + 2) * sizeof(WCHAR)));
	const int wideLen = MultiByteToWideChar(cp, 0, text, static_cast<int>(maxBytes), wide, static_cast<int>(maxBytes + 1));
	if (wideLen > 0) {
		const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wide, wideLen, text, static_cast<int>(textCapacity - 1), nullptr, nullptr);
		if (utf8Len > 0) {
			text[utf8Len] = '\0';
		}
	}
	NP2HeapFree(wide);
}

#if defined(NP2_USE_WEBVIEW2)

void ApplyPreviewZoom() noexcept {
	if (g_controller == nullptr) {
		return;
	}
	const double factor = static_cast<double>(g_previewZoomPercent) / 100.0;
	g_controller->put_ZoomFactor(factor);
}

void ApplyPreviewWebViewBackground() noexcept {
	if (g_controller == nullptr) {
		return;
	}
	ComPtr<ICoreWebView2Controller2> controller2;
	if (FAILED(g_controller.As(&controller2)) || !controller2) {
		PreviewMode_Log(L"ApplyPreviewWebViewBackground: ICoreWebView2Controller2 unavailable");
		return;
	}
	const COREWEBVIEW2_COLOR color = MakePreviewWebViewColor();
	const HRESULT hr = controller2->put_DefaultBackgroundColor(color);
	PreviewMode_Log(L"put_DefaultBackgroundColor hr=0x%08lX dark=%d", hr, IsPreviewDarkTheme() ? 1 : 0);
}

void SetPreviewWebViewVisible(bool visible) noexcept {
	if (g_controller == nullptr) {
		return;
	}
	g_controller->put_IsVisible(visible ? TRUE : FALSE);
}

void ResizeWebViewToContainer() noexcept {
	if (g_controller == nullptr || g_hwndContainer == nullptr) {
		return;
	}
	ApplyPreviewWebViewBackground();
	RECT rc {};
	GetClientRect(g_hwndContainer, &rc);
	if (rc.right <= rc.left || rc.bottom <= rc.top) {
		PreviewMode_Log(L"ResizeWebViewToContainer skipped empty bounds %dx%d",
			rc.right - rc.left, rc.bottom - rc.top);
		return;
	}
	g_controller->put_Bounds(rc);
}

void ShowWebViewUnavailableHtml(LPCWSTR message) noexcept {
	const bool dark = IsPreviewDarkTheme();
	WCHAR html[1024];
	if (dark) {
		swprintf(html, NP2_COUNTOF(html),
			L"<!DOCTYPE html><html><body style=\"font-family:Segoe UI;margin:24px;color:#c9d1d9;background:#0d1117\">"
			L"<h2>Preview unavailable</h2><p>%s</p>"
			L"<p>Install the Evergreen <b>WebView2 Runtime</b> and restart Notepad.</p>"
			L"</body></html>",
			message ? message : L"WebView2 failed to initialize.");
	} else {
		swprintf(html, NP2_COUNTOF(html),
			L"<!DOCTYPE html><html><body style=\"font-family:Segoe UI;margin:24px;color:#24292f;background:#fff\">"
			L"<h2>Preview unavailable</h2><p>%s</p>"
			L"<p>Install the Evergreen <b>WebView2 Runtime</b> and restart Notepad.</p>"
			L"</body></html>",
			message ? message : L"WebView2 failed to initialize.");
	}
	g_pendingHtml = html;
}

HRESULT HandleNavigationStarting(ICoreWebView2 * /*sender*/, ICoreWebView2NavigationStartingEventArgs *args) {
	LPWSTR uri = nullptr;
	if (FAILED(args->get_Uri(&uri)) || uri == nullptr) {
		return S_OK;
	}
	if (!IsAllowedPreviewNavigation(uri)) {
		args->put_Cancel(TRUE);
		if (IsHttpOrHttpsUrl(uri)) {
			OpenUrl(uri);
		}
		PreviewMode_Log(L"NavigationStarting cancel uri=%s", uri);
	}
	CoTaskMemFree(uri);
	return S_OK;
}

HRESULT HandleNewWindowRequested(ICoreWebView2 * /*sender*/, ICoreWebView2NewWindowRequestedEventArgs *args) {
	args->put_Handled(TRUE);
	LPWSTR uri = nullptr;
	if (SUCCEEDED(args->get_Uri(&uri)) && uri != nullptr) {
		if (IsHttpOrHttpsUrl(uri)) {
			OpenUrl(uri);
		}
		CoTaskMemFree(uri);
	}
	return S_OK;
}

void ExecPreviewCopy() noexcept {
	if (g_webview == nullptr) {
		return;
	}
	g_webview->ExecuteScript(L"document.execCommand('copy')", nullptr);
}

void ExecPreviewSelectAll() noexcept {
	if (g_webview == nullptr) {
		return;
	}
	g_webview->ExecuteScript(L"document.execCommand('selectAll')", nullptr);
}

bool ShowPreviewContextMenu(POINT screenPt) noexcept {
	HMENU hMenu = CreatePopupMenu();
	if (hMenu == nullptr) {
		return false;
	}
	if (g_contextHasSelection) {
		AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_COPY, L"&Copy");
	}
	AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_SELECTALL, L"Select &All");
	if (!g_contextLinkUrl.empty()) {
		AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_OPEN_LINK, L"&Open Link");
		AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_COPY_LINK, L"Copy &Link");
	}
	const UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, g_hwndMain, nullptr);
	DestroyMenu(hMenu);
	switch (cmd) {
	case ID_PREVIEW_CTX_COPY:
		ExecPreviewCopy();
		break;
	case ID_PREVIEW_CTX_SELECTALL:
		ExecPreviewSelectAll();
		break;
	case ID_PREVIEW_CTX_OPEN_LINK:
		if (!g_contextLinkUrl.empty()) {
			OpenUrl(g_contextLinkUrl.c_str());
		}
		break;
	case ID_PREVIEW_CTX_COPY_LINK:
		if (!g_contextLinkUrl.empty()) {
			CopyTextToClipboard(g_hwndMain, g_contextLinkUrl.c_str());
		}
		break;
	default:
		break;
	}
	return true;
}

HRESULT HandleContextMenuRequested(ICoreWebView2 * /*sender*/, ICoreWebView2ContextMenuRequestedEventArgs *args) {
	args->put_Handled(TRUE);
	g_contextLinkUrl.clear();
	g_contextHasSelection = false;

	ComPtr<ICoreWebView2ContextMenuTarget> target;
	if (SUCCEEDED(args->get_ContextMenuTarget(&target)) && target) {
		BOOL hasSelection = FALSE;
		target->get_HasSelection(&hasSelection);
		g_contextHasSelection = hasSelection != FALSE;
		BOOL hasLink = FALSE;
		target->get_HasLinkUri(&hasLink);
		if (hasLink) {
			LPWSTR link = nullptr;
			if (SUCCEEDED(target->get_LinkUri(&link)) && link != nullptr) {
				g_contextLinkUrl = link;
				CoTaskMemFree(link);
			}
		}
	}

	POINT pt {};
	args->get_Location(&pt);
	if (g_hwndContainer) {
		ClientToScreen(g_hwndContainer, &pt);
	}
	ShowPreviewContextMenu(pt);
	return S_OK;
}

HRESULT HandleAcceleratorKeyPressed(ICoreWebView2Controller * /*sender*/, ICoreWebView2AcceleratorKeyPressedEventArgs *args) {
	COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
	args->get_KeyEventKind(&kind);
	if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
		return S_OK;
	}
	UINT key = 0;
	args->get_VirtualKey(&key);
	INT lParam = 0;
	args->get_KeyEventLParam(&lParam);
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	if (!ctrl) {
		return S_OK;
	}
	if (key == 'C' || key == VK_INSERT) {
		args->put_Handled(TRUE);
		ExecPreviewCopy();
	} else if (key == 'A') {
		args->put_Handled(TRUE);
		ExecPreviewSelectAll();
	}
	(void)lParam;
	return S_OK;
}

void PostBodyHtml(LPCWSTR bodyHtml) noexcept {
	if (g_webview == nullptr || bodyHtml == nullptr) {
		return;
	}
	const HRESULT hr = g_webview->PostWebMessageAsString(bodyHtml);
	PreviewMode_Log(L"PostWebMessageAsString hr=0x%08lX len=%zu", hr, wcslen(bodyHtml));
}

void FlushPendingBody() noexcept {
	if (!g_pendingBody.empty()) {
		PostBodyHtml(g_pendingBody.c_str());
		g_pendingBody.clear();
	}
}

HRESULT HandleNavigationCompleted(ICoreWebView2 * /*sender*/, ICoreWebView2NavigationCompletedEventArgs *args) {
	BOOL success = FALSE;
	if (args) {
		args->get_IsSuccess(&success);
	}
	g_shellReady = success != FALSE;
	PreviewMode_Log(L"NavigationCompleted success=%d", g_shellReady ? 1 : 0);
	if (g_shellReady) {
		FlushPendingBody();
		ResizeWebViewToContainer();
		SetPreviewWebViewVisible(true);
	}
	return S_OK;
}

void NavigatePendingHtml() noexcept {
	if (g_webview == nullptr || g_pendingHtml.empty()) {
		return;
	}
	g_shellReady = false;
	SetPreviewWebViewVisible(FALSE);
	if (g_hwndContainer) {
		InvalidateRect(g_hwndContainer, nullptr, TRUE);
	}
	const HRESULT hr = g_webview->NavigateToString(g_pendingHtml.c_str());
	PreviewMode_Log(L"NavigateToString shell hr=0x%08lX len=%zu", hr, g_pendingHtml.size());
	g_pendingHtml.clear();
	ApplyPreviewZoom();
}

void AttachWebViewHandlers() noexcept {
	if (g_webview == nullptr || g_controller == nullptr) {
		return;
	}

	g_webview->add_NavigationStarting(
		Callback<ICoreWebView2NavigationStartingEventHandler>(
			[](ICoreWebView2 *sender, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
				return HandleNavigationStarting(sender, args);
			}).Get(),
		&g_tokenNavigationStarting);

	g_webview->add_NavigationCompleted(
		Callback<ICoreWebView2NavigationCompletedEventHandler>(
			[](ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
				return HandleNavigationCompleted(sender, args);
			}).Get(),
		&g_tokenNavigationCompleted);

	g_webview->add_NewWindowRequested(
		Callback<ICoreWebView2NewWindowRequestedEventHandler>(
			[](ICoreWebView2 *sender, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT {
				return HandleNewWindowRequested(sender, args);
			}).Get(),
		&g_tokenNewWindowRequested);

	ComPtr<ICoreWebView2_11> webview11;
	if (SUCCEEDED(g_webview.As(&webview11)) && webview11) {
		webview11->add_ContextMenuRequested(
			Callback<ICoreWebView2ContextMenuRequestedEventHandler>(
				[](ICoreWebView2 *sender, ICoreWebView2ContextMenuRequestedEventArgs *args) -> HRESULT {
					return HandleContextMenuRequested(sender, args);
				}).Get(),
			&g_tokenContextMenuRequested);
	}

	g_controller->add_AcceleratorKeyPressed(
		Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
			[](ICoreWebView2Controller *sender, ICoreWebView2AcceleratorKeyPressedEventArgs *args) -> HRESULT {
				return HandleAcceleratorKeyPressed(sender, args);
			}).Get(),
		&g_tokenAcceleratorKeyPressed);

	ComPtr<ICoreWebView2Settings> settings;
	if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
		settings->put_IsScriptEnabled(TRUE);
		settings->put_AreDefaultScriptDialogsEnabled(TRUE);
		settings->put_IsWebMessageEnabled(TRUE);
		settings->put_AreDefaultContextMenusEnabled(FALSE);
		settings->put_AreDevToolsEnabled(FALSE);
		settings->put_IsStatusBarEnabled(FALSE);
		settings->put_IsZoomControlEnabled(FALSE);
	}

	if (!g_virtualHostMapped && g_previewAssetsDir[0] != L'\0' && PathFileExistsW(g_previewAssetsDir)) {
		ComPtr<ICoreWebView2_3> webview3;
		if (SUCCEEDED(g_webview.As(&webview3)) && webview3) {
			const HRESULT mapHr = webview3->SetVirtualHostNameToFolderMapping(
				PREVIEW_VIRTUAL_HOST, g_previewAssetsDir, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
			g_virtualHostMapped = SUCCEEDED(mapHr);
			PreviewMode_Log(L"SetVirtualHostNameToFolderMapping hr=0x%08lX dir=%s", mapHr, g_previewAssetsDir);
		}
	}

	ApplyPreviewWebViewBackground();
	SetPreviewWebViewVisible(FALSE);
	ResizeWebViewToContainer();
	ApplyPreviewZoom();
}

void OnControllerCreated(HRESULT result, ICoreWebView2Controller *controller) {
	g_webviewCreating = false;
	if (FAILED(result) || controller == nullptr) {
		g_webviewFailed = true;
		PreviewMode_Log(L"CreateCoreWebView2Controller failed hr=0x%08lX", result);
		ShowWebViewUnavailableHtml(L"CreateCoreWebView2Controller failed.");
		return;
	}
	g_controller = controller;
	ApplyPreviewWebViewBackground();
	HRESULT hr = g_controller->get_CoreWebView2(&g_webview);
	if (FAILED(hr) || g_webview == nullptr) {
		g_webviewFailed = true;
		PreviewMode_Log(L"get_CoreWebView2 failed hr=0x%08lX", hr);
		ShowWebViewUnavailableHtml(L"get_CoreWebView2 failed.");
		return;
	}
	g_webviewReady = true;
	AttachWebViewHandlers();
	NavigatePendingHtml();
	PreviewMode_Log(L"WebView2 controller ready");
}

void OnEnvironmentCreated(HRESULT result, ICoreWebView2Environment *env) {
	if (FAILED(result) || env == nullptr) {
		g_webviewCreating = false;
		g_webviewFailed = true;
		PreviewMode_Log(L"CreateCoreWebView2Environment failed hr=0x%08lX", result);
		ShowWebViewUnavailableHtml(L"WebView2 Runtime is missing or failed to start.");
		return;
	}
	g_env = env;
	if (g_hwndContainer == nullptr) {
		g_webviewCreating = false;
		g_webviewFailed = true;
		return;
	}
	const HRESULT hr = g_env->CreateCoreWebView2Controller(
		g_hwndContainer,
		Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
			[](HRESULT result, ICoreWebView2Controller *controller) -> HRESULT {
				OnControllerCreated(result, controller);
				return S_OK;
			}).Get());
	if (FAILED(hr)) {
		g_webviewCreating = false;
		g_webviewFailed = true;
		PreviewMode_Log(L"CreateCoreWebView2Controller call failed hr=0x%08lX", hr);
		ShowWebViewUnavailableHtml(L"CreateCoreWebView2Controller call failed.");
	}
}

void PumpMessagesForWebViewInit(DWORD timeoutMs) noexcept {
	const DWORD start = GetTickCount();
	while (!g_webviewReady && !g_webviewFailed && (GetTickCount() - start) < timeoutMs) {
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				PostQuitMessage(static_cast<int>(msg.wParam));
				return;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (!g_webviewReady && !g_webviewFailed) {
			MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
		}
	}
}

void DestroyBrowser() noexcept {
	PreviewMode_Log(L"DestroyBrowser begin");
	g_pendingHtml.clear();
	g_pendingBody.clear();
	g_webviewReady = false;
	g_webviewCreating = false;
	g_virtualHostMapped = false;
	g_shellReady = false;
	if (g_webview) {
		if (g_tokenNavigationStarting.value) {
			g_webview->remove_NavigationStarting(g_tokenNavigationStarting);
			g_tokenNavigationStarting = {};
		}
		if (g_tokenNavigationCompleted.value) {
			g_webview->remove_NavigationCompleted(g_tokenNavigationCompleted);
			g_tokenNavigationCompleted = {};
		}
		if (g_tokenNewWindowRequested.value) {
			g_webview->remove_NewWindowRequested(g_tokenNewWindowRequested);
			g_tokenNewWindowRequested = {};
		}
		if (g_tokenContextMenuRequested.value) {
			ComPtr<ICoreWebView2_11> webview11;
			if (SUCCEEDED(g_webview.As(&webview11)) && webview11) {
				webview11->remove_ContextMenuRequested(g_tokenContextMenuRequested);
			}
			g_tokenContextMenuRequested = {};
		}
	}
	if (g_controller) {
		if (g_tokenAcceleratorKeyPressed.value) {
			g_controller->remove_AcceleratorKeyPressed(g_tokenAcceleratorKeyPressed);
			g_tokenAcceleratorKeyPressed = {};
		}
		g_controller->Close();
	}
	g_webview.Reset();
	g_controller.Reset();
	g_env.Reset();
	PreviewMode_Log(L"DestroyBrowser end");
}

bool EnsureBrowser() noexcept {
	if (g_webviewReady && g_webview != nullptr) {
		return true;
	}
	if (g_webviewFailed) {
		return false;
	}
	if (g_hwndContainer == nullptr) {
		return false;
	}
	RECT rc;
	GetClientRect(g_hwndContainer, &rc);
	if (rc.right <= 0 || rc.bottom <= 0) {
		PreviewMode_Log(L"EnsureBrowser: container size zero");
		return false;
	}

	if (!g_webviewCreating) {
		g_webviewCreating = true;
		CreateDirectoryW(g_webviewUserDataDir, nullptr);
		SetPreviewWebViewEnvBackground();
		PreviewMode_Log(L"EnsureBrowser: CreateCoreWebView2Environment userData=%s", g_webviewUserDataDir);
		const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
			nullptr, g_webviewUserDataDir, nullptr,
			Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
				[](HRESULT result, ICoreWebView2Environment *env) -> HRESULT {
					OnEnvironmentCreated(result, env);
					return S_OK;
				}).Get());
		if (FAILED(hr)) {
			g_webviewCreating = false;
			g_webviewFailed = true;
			PreviewMode_Log(L"CreateCoreWebView2EnvironmentWithOptions hr=0x%08lX", hr);
			ShowWebViewUnavailableHtml(L"CreateCoreWebView2EnvironmentWithOptions failed.");
			return false;
		}
	}

	PumpMessagesForWebViewInit(PREVIEW_INIT_TIMEOUT_MS);
	return g_webviewReady && g_webview != nullptr;
}

void LoadHtmlIntoBrowser(LPCWSTR bodyHtml) noexcept {
	if (bodyHtml == nullptr) {
		return;
	}
	const bool dark = IsPreviewDarkTheme();
	const bool isFullDocument = (_wcsnicmp(bodyHtml, L"<html", 5) == 0 || _wcsnicmp(bodyHtml, L"<!DOC", 5) == 0);

	if (!EnsureBrowser()) {
		PreviewMode_Log(L"LoadHtmlIntoBrowser: EnsureBrowser failed");
		return;
	}

	// Rare path: raw HTML/XML documents that already include a full page.
	if (isFullDocument) {
		g_shellReady = false;
		g_pendingBody.clear();
		g_pendingHtml = bodyHtml;
		NavigatePendingHtml();
		return;
	}

	if (!g_shellReady || g_shellDark != dark) {
		g_shellDark = dark;
		g_pendingBody = bodyHtml;
		WCHAR shell[16384];
		BuildPreviewShellDocument(shell, NP2_COUNTOF(shell), dark);
		g_pendingHtml = shell;
		NavigatePendingHtml();
		return;
	}

	PostBodyHtml(bodyHtml);
}

#else // !NP2_USE_WEBVIEW2

void ApplyPreviewZoom() noexcept {}
void DestroyBrowser() noexcept {}
bool EnsureBrowser() noexcept { return false; }
void LoadHtmlIntoBrowser(LPCWSTR) noexcept {}
void ResizeWebViewToContainer() noexcept {}

#endif

void UpdatePreviewContent() noexcept {
	PreviewMode_Log(L"UpdatePreviewContent enter active=%d inUpdate=%d dirty=%d lex=%d",
		g_active, g_inUpdate, g_dirty, pLexCurrent ? pLexCurrent->rid : -1);
	if (!g_active || pLexCurrent == nullptr || g_inUpdate) {
		return;
	}
	g_inUpdate = true;
	const unsigned serialAtStart = g_dirtySerial;
	const bool keepPreviewFocus = IsPreviewFocus();

	const Sci_Position length = min<Sci_Position>(SciCall_GetLength(), PREVIEW_MAX_TEXT_BYTES);
	PreviewMode_Log(L"UpdatePreviewContent: doc bytes=%lld", static_cast<long long>(length));
	const size_t textCapacity = static_cast<size_t>(max<Sci_Position>(length, 1)) + 1;
	char *text = static_cast<char *>(NP2HeapAlloc(textCapacity));
	GetDocumentUtf8(text, textCapacity);
	const size_t textLen = strlen(text);

	const size_t htmlCch = textCapacity * 8 + 8192;
	LPWSTR html = static_cast<LPWSTR>(NP2HeapAlloc(htmlCch * sizeof(WCHAR)));

	switch (pLexCurrent->rid) {
	case NP2LEX_MARKDOWN:
		MarkdownToHtml(text, textLen, html, htmlCch);
		break;
	case NP2LEX_CSV:
		CsvToHtml(text, textLen, html, htmlCch);
		break;
	case NP2LEX_HTML:
	case NP2LEX_XML:
	default:
		WrapDocumentHtml(text, textLen, html, htmlCch);
		break;
	}

	LoadHtmlIntoBrowser(html);

	NP2HeapFree(html);
	NP2HeapFree(text);

	if (g_dirtySerial == serialAtStart) {
		g_dirty = false;
	}
	g_inUpdate = false;

	if (keepPreviewFocus && g_hwndContainer) {
		SetFocus(g_hwndContainer);
#if defined(NP2_USE_WEBVIEW2)
		if (g_controller) {
			g_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
		}
#endif
	}
	PreviewMode_Log(L"UpdatePreviewContent leave dirty=%d", g_dirty);
}

void ComputePaneHeights(int cy, int &editCy, int &previewCy) noexcept {
	const int avail = max(0, cy - PREVIEW_SPLITTER_CY);
	if (g_previewMaximized) {
		previewCy = avail;
		editCy = 0;
		return;
	}
	previewCy = avail * g_previewPercent / 100;
	previewCy = clamp(previewCy, PREVIEW_MIN_PANE_CY, max(PREVIEW_MIN_PANE_CY, avail - PREVIEW_MIN_PANE_CY));
	editCy = avail - previewCy;
}

bool HandlePreviewMouseWheel(WPARAM wParam, LPARAM lParam) noexcept {
	if (!g_active || !(GetKeyState(VK_CONTROL) & 0x8000)) {
		return false;
	}
	POINT pt { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
	ScreenToClient(g_hwndMain, &pt);
	if (g_lastLayoutCx <= 0 || g_lastLayoutCy <= 0) {
		return false;
	}
	int editCy = 0;
	int previewCy = 0;
	ComputePaneHeights(g_lastLayoutCy, editCy, previewCy);
	const int previewTop = g_lastLayoutY + editCy + PREVIEW_SPLITTER_CY;
	const RECT rcPreview { g_lastLayoutX, previewTop, g_lastLayoutX + g_lastLayoutCx, previewTop + previewCy };
	if (!PtInRect(&rcPreview, pt)) {
		return false;
	}
	const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
	if (delta == 0) {
		return false;
	}
	const int step = (delta > 0) ? PREVIEW_ZOOM_STEP : -PREVIEW_ZOOM_STEP;
	g_previewZoomPercent = clamp(g_previewZoomPercent + step, PREVIEW_ZOOM_MIN, PREVIEW_ZOOM_MAX);
	ApplyPreviewZoom();
	return true;
}

void ApplyPaneLayout(int x, int y, int cx, int cy, int *pEditHeight) noexcept {
	if (cx <= 0 || cy <= 0) {
		PreviewMode_Log(L"ApplyPaneLayout ignored invalid size %dx%d", cx, cy);
		if (pEditHeight) {
			*pEditHeight = max(0, cy);
		}
		return;
	}

	g_lastLayoutX = x;
	g_lastLayoutY = y;
	g_lastLayoutCx = cx;
	g_lastLayoutCy = cy;

	int editCy = 0;
	int previewCy = 0;
	ComputePaneHeights(cy, editCy, previewCy);

	if (pEditHeight) {
		*pEditHeight = editCy;
	}

	const int splitterY = y + editCy;
	const int previewY = splitterY + PREVIEW_SPLITTER_CY;

	if (g_hwndSplitter != nullptr) {
		SetWindowPos(g_hwndSplitter, HWND_TOP, x, splitterY, cx, PREVIEW_SPLITTER_CY, SWP_NOACTIVATE);
		ShowSplitter(true);
	}
	SetWindowPos(g_hwndContainer, HWND_TOP, x, previewY, cx, previewCy, SWP_NOACTIVATE);
	if (g_hwndContainer) {
		InvalidateRect(g_hwndContainer, nullptr, TRUE);
	}
	ResizeWebViewToContainer();
	ShowContainer(true);
}

void RelayoutClientNow() noexcept {
	if (!g_active || g_lastLayoutCx <= 0 || g_lastLayoutCy <= 0) {
		return;
	}
	int editCy = 0;
	ApplyPaneLayout(g_lastLayoutX, g_lastLayoutY, g_lastLayoutCx, g_lastLayoutCy, &editCy);
	if (hwndEdit != nullptr) {
		SetWindowPos(hwndEdit, nullptr, g_lastLayoutX, g_lastLayoutY, g_lastLayoutCx, editCy, SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

void UpdatePreviewPercentFromCursor() noexcept {
	if (!g_splitterDragging || g_lastLayoutCy <= 0) {
		return;
	}
	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(g_hwndMain, &pt);

	const int avail = max(0, g_lastLayoutCy - PREVIEW_SPLITTER_CY);
	if (avail <= PREVIEW_MIN_PANE_CY * 2) {
		return;
	}

	const int relY = pt.y - g_lastLayoutY;
	int previewCy = g_lastLayoutCy - relY - PREVIEW_SPLITTER_CY;
	previewCy = clamp(previewCy, PREVIEW_MIN_PANE_CY, avail - PREVIEW_MIN_PANE_CY);

	g_previewMaximized = false;
	g_previewPercent = previewCy * 100 / avail;
	RelayoutClientNow();
}

bool IsPreviewDarkSplitter() noexcept {
	return np2StyleTheme == StyleTheme_Dark && DarkMode_ShouldApply(true);
}

void PaintSplitter(HDC hdc, const RECT &rc) noexcept {
	const int midY = (rc.top + rc.bottom) / 2;
	if (IsPreviewDarkSplitter()) {
		FillRect(hdc, &rc, DarkMode_GetCtlColorBrush(true));
		HPEN hPen = CreatePen(PS_SOLID, 1, NP2_DARK_SHELL_HIGHLIGHT);
		HPEN hPenEdge = CreatePen(PS_SOLID, 1, RGB(0x1E, 0x1E, 0x1E));
		HPEN hOld = SelectPen(hdc, hPenEdge);
		MoveToEx(hdc, rc.left, rc.top, nullptr);
		LineTo(hdc, rc.right, rc.top);
		SelectPen(hdc, hPen);
		MoveToEx(hdc, rc.left + 2, midY, nullptr);
		LineTo(hdc, rc.right - 2, midY);
		SelectPen(hdc, hOld);
		DeleteObject(hPen);
		DeleteObject(hPenEdge);
		return;
	}

	FillRect(hdc, &rc, GetSysColorBrush(COLOR_3DFACE));
	HPEN hPenShadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
	HPEN hPenLight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT));
	HPEN hOld = SelectPen(hdc, hPenShadow);
	MoveToEx(hdc, rc.left + 2, midY, nullptr);
	LineTo(hdc, rc.right - 2, midY);
	SelectPen(hdc, hPenLight);
	MoveToEx(hdc, rc.left + 2, midY + 1, nullptr);
	LineTo(hdc, rc.right - 2, midY + 1);
	SelectPen(hdc, hOld);
	DeleteObject(hPenShadow);
	DeleteObject(hPenLight);
}

LRESULT CALLBACK PreviewSplitterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
	switch (msg) {
	case WM_LBUTTONDOWN:
		if (!g_active) {
			break;
		}
		SetCapture(hwnd);
		g_splitterDragging = true;
		g_splitterDragStartY = GET_Y_LPARAM(lParam);
		g_splitterDragStartPercent = g_previewPercent;
		return 0;

	case WM_MOUSEMOVE:
		if (g_splitterDragging) {
			UpdatePreviewPercentFromCursor();
		}
		return 0;

	case WM_LBUTTONUP:
		if (g_splitterDragging) {
			g_splitterDragging = false;
			ReleaseCapture();
			RelayoutClientNow();
			RequestRelayout();
		}
		return 0;

	case WM_CAPTURECHANGED:
		if (g_splitterDragging && AsPointer<HWND>(lParam) != hwnd) {
			g_splitterDragging = false;
			RelayoutClientNow();
			RequestRelayout();
		}
		return 0;

	case WM_LBUTTONDBLCLK:
		if (g_active) {
			PreviewMode_ToggleMaximize();
		}
		return 0;

	case WM_SETCURSOR:
		if (LOWORD(lParam) == HTCLIENT) {
			SetCursor(LoadCursor(nullptr, IDC_SIZENS));
			return TRUE;
		}
		break;

	case WM_ERASEBKGND:
		return TRUE;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);
		RECT rc;
		GetClientRect(hwnd, &rc);
		PaintSplitter(hdc, rc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK PreviewContainerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
	switch (msg) {
	case WM_ERASEBKGND:
		if (g_previewContainerBrush != nullptr) {
			RECT rc;
			GetClientRect(hwnd, &rc);
			FillRect(AsPointer<HDC>(wParam), &rc, g_previewContainerBrush);
			return TRUE;
		}
		break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void EnsureSplitterClass() noexcept {
	if (g_splitterClassRegistered) {
		return;
	}
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = PreviewSplitterProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_SIZENS);
	wc.hbrBackground = nullptr;
	wc.lpszClassName = PREVIEW_SPLITTER_CLASS;
	RegisterClassExW(&wc);
	g_splitterClassRegistered = true;
}

void EnsurePreviewContainerClass() noexcept {
	if (g_containerClassRegistered) {
		return;
	}
	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = PreviewContainerProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = nullptr;
	wc.lpszClassName = PREVIEW_CONTAINER_CLASS;
	RegisterClassExW(&wc);
	g_containerClassRegistered = true;
}

} // namespace

void PreviewMode_Init(HWND hwndMain) noexcept {
	PreviewMode_InitLog();
	g_hwndMain = hwndMain;
	EnsureSplitterClass();
	EnsurePreviewContainerClass();
	UpdatePreviewContainerBackground();
	g_hwndContainer = CreateWindowExW(0, PREVIEW_CONTAINER_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		0, 0, 0, 0, hwndMain, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(0xFB10)), GetModuleHandle(nullptr), nullptr);
	g_hwndSplitter = CreateWindowExW(0, PREVIEW_SPLITTER_CLASS, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		0, 0, 0, 0, hwndMain, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(0xFB11)), GetModuleHandle(nullptr), nullptr);
	ShowSplitter(false);
	PreviewMode_Log(L"PreviewMode_Init main=%p container=%p splitter=%p", hwndMain, g_hwndContainer, g_hwndSplitter);
}

void PreviewMode_Destroy() noexcept {
	if (g_hwndMain != nullptr) {
		KillTimer(g_hwndMain, ID_PREVIEW_TIMER);
	}
	g_updatePosted = false;
	g_inUpdate = false;
	DestroyBrowser();
	if (g_hwndSplitter != nullptr) {
		DestroyWindow(g_hwndSplitter);
		g_hwndSplitter = nullptr;
	}
	if (g_hwndContainer != nullptr) {
		DestroyWindow(g_hwndContainer);
		g_hwndContainer = nullptr;
	}
	if (g_previewContainerBrush != nullptr) {
		DeleteObject(g_previewContainerBrush);
		g_previewContainerBrush = nullptr;
	}
}

void PreviewMode_SetMainWindow(HWND hwndMain) noexcept {
	g_hwndMain = hwndMain;
}

bool PreviewMode_IsSupported(int lexerRid) noexcept {
	switch (lexerRid) {
	case NP2LEX_MARKDOWN:
	case NP2LEX_HTML:
	case NP2LEX_XML:
	case NP2LEX_CSV:
		return true;
	default:
		return false;
	}
}

bool PreviewMode_IsActive() noexcept {
	return g_active;
}

bool PreviewMode_GetAutoEnable() noexcept {
	return g_autoEnable;
}

void PreviewMode_SetAutoEnable(bool enable) noexcept {
	g_autoEnable = enable;
}

void PreviewMode_ApplyForLexer(int lexerRid, bool fileOpen) noexcept {
	PreviewMode_Log(L"ApplyForLexer rid=%d fileOpen=%d active=%d", lexerRid, fileOpen, g_active);
	if (!PreviewMode_IsSupported(lexerRid)) {
		if (g_active) {
			PreviewMode_SetActive(false);
		}
		return;
	}
	if (fileOpen && g_autoEnable && !g_active) {
		PreviewMode_SetActive(true);
	} else if (g_active) {
		PreviewMode_RequestUpdate();
	}
}

void PreviewMode_SetActive(bool active) noexcept {
	PreviewMode_Log(L"SetActive request=%d current=%d lex=%d", active, g_active, pLexCurrent ? pLexCurrent->rid : -1);
	if (active && (pLexCurrent == nullptr || !PreviewMode_IsSupported(pLexCurrent->rid))) {
		active = false;
	}
	if (g_active == active) {
		return;
	}
	g_active = active;
	if (g_active) {
		ShowContainer(true);
		g_dirty = true;
	} else {
		ShowContainer(false);
		ShowSplitter(false);
		if (g_hwndMain != nullptr) {
			KillTimer(g_hwndMain, ID_PREVIEW_TIMER);
		}
		g_updatePosted = false;
		g_dirty = false;
		DestroyBrowser();
	}
	if (g_hwndMain != nullptr) {
		RECT rc;
		GetClientRect(g_hwndMain, &rc);
		PostMessage(g_hwndMain, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rc.right, rc.bottom));
		SchedulePreviewUpdate();
	}
}

void PreviewMode_Toggle() noexcept {
	PreviewMode_SetActive(!g_active);
}

void PreviewMode_Layout(int x, int y, int cx, int cy, int *pEditHeight) noexcept {
	PreviewMode_Log(L"Layout active=%d xy=%d,%d size=%dx%d percent=%d max=%d", g_active, x, y, cx, cy, g_previewPercent, g_previewMaximized);
	if (!g_active || cx <= 0 || cy <= 0) {
		ShowContainer(false);
		ShowSplitter(false);
		if (pEditHeight) {
			*pEditHeight = cy;
		}
		return;
	}
	ApplyPaneLayout(x, y, cx, cy, pEditHeight);
}

void PreviewMode_SetHeightPercent(int percent) noexcept {
	g_previewPercent = clamp(percent, 1, 99);
	g_savedPreviewPercent = g_previewPercent;
}

int PreviewMode_GetHeightPercent() noexcept {
	return g_previewPercent;
}

bool PreviewMode_IsMaximized() noexcept {
	return g_previewMaximized;
}

void PreviewMode_ToggleMaximize() noexcept {
	if (!g_active) {
		return;
	}
	if (g_previewMaximized) {
		g_previewMaximized = false;
		g_previewPercent = g_savedPreviewPercent;
	} else {
		g_savedPreviewPercent = g_previewPercent;
		g_previewMaximized = true;
	}
	RequestRelayout();
}

void PreviewMode_SetMaximized(bool maximized) noexcept {
	g_previewMaximized = maximized;
	if (!maximized) {
		g_savedPreviewPercent = g_previewPercent;
	}
}

void PreviewMode_SetZoomPercent(int percent) noexcept {
	g_previewZoomPercent = clamp(percent, PREVIEW_ZOOM_MIN, PREVIEW_ZOOM_MAX);
	ApplyPreviewZoom();
}

int PreviewMode_GetZoomPercent() noexcept {
	return g_previewZoomPercent;
}

bool PreviewMode_HandleMouseWheel(WPARAM wParam, LPARAM lParam) noexcept {
	return HandlePreviewMouseWheel(wParam, lParam);
}

void PreviewMode_RequestUpdate() noexcept {
	if (!g_active) {
		return;
	}
	g_dirty = true;
	++g_dirtySerial;
	SchedulePreviewUpdate();
}

void PreviewMode_OnTimer() noexcept {
	KillTimer(g_hwndMain, ID_PREVIEW_TIMER);
	if (!g_dirty || !g_active) {
		return;
	}
	if (!g_updatePosted) {
		g_updatePosted = true;
		PreviewMode_Log(L"OnTimer PostMessage APPM_PREVIEW_UPDATE");
		PostMessage(g_hwndMain, APPM_PREVIEW_UPDATE, 0, 0);
	}
}

void PreviewMode_OnPostedUpdate() noexcept {
	PreviewMode_Log(L"OnPostedUpdate enter posted=%d dirty=%d active=%d inUpdate=%d",
		g_updatePosted, g_dirty, g_active, g_inUpdate);
	g_updatePosted = false;
	if (!g_active || !g_dirty) {
		return;
	}
	if (g_hwndContainer != nullptr) {
		RECT rc;
		GetClientRect(g_hwndContainer, &rc);
		if (rc.right <= 0 || rc.bottom <= 0) {
			PreviewMode_Log(L"OnPostedUpdate: container zero size, retry timer");
			SetTimer(g_hwndMain, ID_PREVIEW_TIMER, PREVIEW_LAYOUT_RETRY_MS, nullptr);
			return;
		}
	}
	UpdatePreviewContent();
	if (g_dirty) {
		PreviewMode_Log(L"OnPostedUpdate: still dirty, reschedule");
		SchedulePreviewUpdate();
	}
	PreviewMode_Log(L"OnPostedUpdate leave");
}

void PreviewMode_OnThemeChanged() noexcept {
	if (g_hwndSplitter != nullptr) {
		InvalidateRect(g_hwndSplitter, nullptr, TRUE);
	}
	UpdatePreviewContainerBackground();
#if defined(NP2_USE_WEBVIEW2)
	ApplyPreviewWebViewBackground();
	g_shellReady = false;
#endif
	if (g_active) {
		PreviewMode_RequestUpdate();
	}
}

bool PreviewMode_ShouldSkipMainAccelerator(const MSG *msg) noexcept {
	if (msg == nullptr || !g_active || !IsPreviewFocus()) {
		return false;
	}
	if (msg->message != WM_KEYDOWN && msg->message != WM_SYSKEYDOWN) {
		return false;
	}
	const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	if (!ctrl) {
		return false;
	}
	const WPARAM vk = msg->wParam;
	return vk == 'C' || vk == 'A' || vk == 'X' || vk == VK_INSERT;
}
