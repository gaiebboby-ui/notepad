// This file is part of Notepad.

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <ole2.h>
#include <oleidl.h>
#include <ocidl.h>
#include <docobj.h>
#include <mshtmhst.h>
#include <mshtmcid.h>
#include <exdisp.h>
#include <mshtml.h>

#ifndef DISPID_BEFORENAVIGATE2
#define DISPID_BEFORENAVIGATE2	250
#endif
#ifndef DISPID_NEWWINDOW2
#define DISPID_NEWWINDOW2		259
#endif
#ifndef DISPID_NEWWINDOW3
#define DISPID_NEWWINDOW3		273
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdarg>
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

extern "C" int __cdecl md_html(const MD_CHAR *input, MD_SIZE input_size,
	void (*process_output)(const MD_CHAR *, MD_SIZE, void *),
	void *userdata, unsigned parser_flags, unsigned renderer_flags);

extern HWND hwndEdit;

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef WSB_PROP_HCOLOR
#define WSB_PROP_HCOLOR		11
#endif
#ifndef WSB_PROP_VCOLOR
#define WSB_PROP_VCOLOR		12
#endif

#ifndef NP2_COUNTOF
#define NP2_COUNTOF(ar) (sizeof(ar) / sizeof((ar)[0]))
#endif

#define PREVIEW_LAYOUT_RETRY_MS	16
#define PREVIEW_MAX_TEXT_BYTES	(2 * 1024 * 1024)
#define PREVIEW_READY_TIMEOUT_MS	5000
#define PREVIEW_SPLITTER_CY		5
#define PREVIEW_MIN_PANE_CY		40
#define PREVIEW_DEFAULT_PERCENT	50
#define PREVIEW_ZOOM_MIN		50
#define PREVIEW_ZOOM_MAX		250
#define PREVIEW_ZOOM_STEP		10
#define PREVIEW_ZOOM_DEFAULT	100

#define ID_PREVIEW_CTX_COPY			0xFB30
#define ID_PREVIEW_CTX_SELECTALL	0xFB31
#define ID_PREVIEW_CTX_OPEN_LINK	0xFB32
#define ID_PREVIEW_CTX_COPY_LINK	0xFB33
#define ID_PREVIEW_CTX_COPY_IMAGE	0xFB34

constexpr WCHAR PREVIEW_SPLITTER_CLASS[] = L"NP2PreviewSplitter";
constexpr UINT_PTR PREVIEW_WHEEL_SUBCLASS_ID = 1;

namespace {

HWND g_hwndMain;
HWND g_hwndContainer;
HWND g_hwndSplitter;
IWebBrowser2 *g_pBrowser = nullptr;
IOleObject *g_pOleObject = nullptr;
IOleInPlaceObject *g_pInPlaceObject = nullptr;

class PreviewOleSite;
PreviewOleSite *g_pOleSite = nullptr;

bool g_active;
bool g_autoEnable;
bool g_dirty;
unsigned g_dirtySerial;
bool g_blankLoaded;
bool g_updatePosted;
bool g_inUpdate;
int g_pumpDepth;
int g_waitReadyIters;

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

WCHAR g_previewLogPath[MAX_PATH];
bool g_previewLogEnabled = true;

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

void ApplyPreviewZoom() noexcept;
void ApplyPreviewBrowserChrome() noexcept;

void PreviewMode_InitLog() noexcept {
	WCHAR exePath[MAX_PATH];
	if (GetModuleFileNameW(nullptr, exePath, NP2_COUNTOF(exePath)) == 0) {
		g_previewLogPath[0] = L'\0';
		return;
	}
	PathRemoveFileSpecW(exePath);
	PathCombineW(g_previewLogPath, exePath, L"NotepadPreview.log");

	WCHAR env[8];
	g_previewLogEnabled = (GetEnvironmentVariableW(L"NP2_PREVIEW_LOG", env, NP2_COUNTOF(env)) == 0)
		|| (env[0] != L'0');

	DeleteFileW(g_previewLogPath);
	PreviewMode_Log(L"========== PreviewMode log started ==========");
	PreviewMode_Log(L"Log file: %s", g_previewLogPath);
	PreviewMode_Log(L"Disable logging: set NP2_PREVIEW_LOG=0");
}

LPCWSTR ReadyStateName(READYSTATE rs) noexcept {
	switch (rs) {
	case READYSTATE_UNINITIALIZED: return L"UNINITIALIZED";
	case READYSTATE_LOADING: return L"LOADING";
	case READYSTATE_LOADED: return L"LOADED";
	case READYSTATE_INTERACTIVE: return L"INTERACTIVE";
	case READYSTATE_COMPLETE: return L"COMPLETE";
	default: return L"UNKNOWN";
	}
}

bool IsPreviewDarkChrome() noexcept {
	return np2StyleTheme == StyleTheme_Dark && DarkMode_ShouldApply(true);
}

IHTMLDocument2 *GetPreviewHtmlDocument() noexcept {
	if (g_pBrowser == nullptr) {
		return nullptr;
	}
	IDispatch *pDocDisp = nullptr;
	if (FAILED(g_pBrowser->get_Document(&pDocDisp)) || pDocDisp == nullptr) {
		return nullptr;
	}
	IHTMLDocument2 *pHTML = nullptr;
	pDocDisp->QueryInterface(IID_IHTMLDocument2, reinterpret_cast<void **>(&pHTML));
	pDocDisp->Release();
	return pHTML;
}

bool QueryOleCommandEnabled(IUnknown *target, OLECMDID cmdId) noexcept {
	if (target == nullptr) {
		return false;
	}
	IOleCommandTarget *pCmd = nullptr;
	if (FAILED(target->QueryInterface(IID_IOleCommandTarget, reinterpret_cast<void **>(&pCmd)))) {
		return false;
	}
	OLECMD cmd = { static_cast<ULONG>(cmdId), 0 };
	const HRESULT hr = pCmd->QueryStatus(nullptr, 1, &cmd, nullptr);
	pCmd->Release();
	return SUCCEEDED(hr) && (cmd.cmdf & OLECMDF_ENABLED) != 0;
}

void ExecOleCommand(IUnknown *target, OLECMDID cmdId) noexcept {
	if (target == nullptr) {
		return;
	}
	IOleCommandTarget *pCmd = nullptr;
	if (SUCCEEDED(target->QueryInterface(IID_IOleCommandTarget, reinterpret_cast<void **>(&pCmd)))) {
		pCmd->Exec(nullptr, static_cast<ULONG>(cmdId), OLECMDEXECOPT_DODEFAULT, nullptr, nullptr);
		pCmd->Release();
	}
}

bool ExecHtmlCommand(IHTMLDocument2 *pDoc, LPCWSTR command) noexcept {
	if (pDoc == nullptr || command == nullptr) {
		return false;
	}
	VARIANT empty;
	VariantInit(&empty);
	BSTR bCmd = SysAllocString(command);
	if (bCmd == nullptr) {
		return false;
	}
	const HRESULT hr = pDoc->execCommand(bCmd, VARIANT_FALSE, empty, nullptr);
	SysFreeString(bCmd);
	return SUCCEEDED(hr);
}

void CopyTextToClipboard(HWND hwnd, LPCWSTR text) noexcept {
	if (text == nullptr || text[0] == L'\0') {
		return;
	}
	if (!OpenClipboard(hwnd)) {
		return;
	}
	EmptyClipboard();
	const size_t bytes = (wcslen(text) + 1) * sizeof(WCHAR);
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (hMem != nullptr) {
		WCHAR *dest = static_cast<WCHAR *>(GlobalLock(hMem));
		if (dest != nullptr) {
			memcpy(dest, text, bytes);
			GlobalUnlock(hMem);
			SetClipboardData(CF_UNICODETEXT, hMem);
		} else {
			GlobalFree(hMem);
		}
	}
	CloseClipboard();
}

bool GetElementTagIs(IHTMLElement *pElem, LPCWSTR tagName) noexcept {
	if (pElem == nullptr || tagName == nullptr) {
		return false;
	}
	BSTR tag = nullptr;
	if (FAILED(pElem->get_tagName(&tag)) || tag == nullptr) {
		return false;
	}
	const bool match = _wcsicmp(tag, tagName) == 0;
	SysFreeString(tag);
	return match;
}

bool GetAnchorHref(IHTMLElement *pElem, LPWSTR url, size_t urlCch) noexcept {
	if (pElem == nullptr || urlCch == 0) {
		return false;
	}
	url[0] = L'\0';
	IHTMLAnchorElement *pAnchor = nullptr;
	if (FAILED(pElem->QueryInterface(IID_IHTMLAnchorElement, reinterpret_cast<void **>(&pAnchor)))) {
		IHTMLElement *pParent = nullptr;
		if (SUCCEEDED(pElem->get_parentElement(&pParent)) && pParent != nullptr) {
			pParent->QueryInterface(IID_IHTMLAnchorElement, reinterpret_cast<void **>(&pAnchor));
			pParent->Release();
		}
	}
	if (pAnchor == nullptr) {
		return false;
	}
	BSTR href = nullptr;
	pAnchor->get_href(&href);
	pAnchor->Release();
	if (href == nullptr) {
		return false;
	}
	wcsncpy(url, href, urlCch - 1);
	url[urlCch - 1] = L'\0';
	SysFreeString(href);
	return url[0] != L'\0';
}

bool GetImageSrc(IHTMLElement *pElem, LPWSTR url, size_t urlCch) noexcept {
	if (pElem == nullptr || urlCch == 0) {
		return false;
	}
	url[0] = L'\0';
	IHTMLImgElement *pImg = nullptr;
	if (FAILED(pElem->QueryInterface(IID_IHTMLImgElement, reinterpret_cast<void **>(&pImg)))) {
		return false;
	}
	BSTR src = nullptr;
	pImg->get_src(&src);
	pImg->Release();
	if (src == nullptr) {
		return false;
	}
	wcsncpy(url, src, urlCch - 1);
	url[urlCch - 1] = L'\0';
	SysFreeString(src);
	return url[0] != L'\0';
}

void OpenUrl(LPCWSTR url) noexcept {
	if (url == nullptr || url[0] == L'\0') {
		return;
	}
	ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
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
	// In-document anchors on the preview shell (about:blank#section).
	if (wcsncmp(url, L"about:blank", 11) == 0 && url[11] == L'#') {
		return true;
	}
	return false;
}

void NotifyPreviewLinkCopied(LPCWSTR url) noexcept {
	WCHAR display[200];
	display[0] = L'\0';
	if (url != nullptr && url[0] != L'\0') {
		const size_t maxDisp = NP2_COUNTOF(display) - 4;
		wcsncpy(display, url, maxDisp);
		display[maxDisp] = L'\0';
		if (wcslen(url) > maxDisp) {
			wcscat(display, L"...");
		}
	}
	WCHAR msg[280];
	swprintf(msg, NP2_COUNTOF(msg), L"Link copied to clipboard\n%s", display);
	ShowNotificationW(SC_NOTIFICATIONPOSITION_BOTTOMRIGHT, msg);
}

void HandlePreviewHyperlink(LPCWSTR url) noexcept {
	if (url == nullptr || url[0] == L'\0') {
		return;
	}
	const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
	if (ctrlDown) {
		OpenUrl(url);
		return;
	}
	CopyTextToClipboard(g_hwndMain, url);
	NotifyPreviewLinkCopied(url);
}

LPCWSTR DispParamBstr(const DISPPARAMS *params, UINT index) noexcept {
	if (params == nullptr || index >= params->cArgs) {
		return nullptr;
	}
	const VARIANT &v = params->rgvarg[index];
	if (v.vt == VT_BSTR && v.bstrVal != nullptr) {
		return v.bstrVal;
	}
	return nullptr;
}

void SetDispBoolRef(DISPPARAMS *params, UINT index, VARIANT_BOOL value) noexcept {
	if (params == nullptr || index >= params->cArgs) {
		return;
	}
	VARIANT &v = params->rgvarg[index];
	if (v.vt == (VT_BYREF | VT_BOOL) && v.pboolVal != nullptr) {
		*v.pboolVal = value;
	}
}

void SetDispCancel(DISPPARAMS *params, VARIANT_BOOL cancel) noexcept {
	SetDispBoolRef(params, 0, cancel);
}

class PreviewBrowserEvents final : public IDispatch {
	ULONG m_ref = 1;

public:
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) noexcept override {
		if (ppv == nullptr) {
			return E_POINTER;
		}
		if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch)) {
			*ppv = static_cast<IDispatch *>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++m_ref; }
	STDMETHODIMP_(ULONG) Release() noexcept override {
		const ULONG ref = --m_ref;
		if (ref == 0) {
			delete this;
		}
		return ref;
	}
	STDMETHODIMP GetTypeInfoCount(UINT *) noexcept override { return E_NOTIMPL; }
	STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo **) noexcept override { return E_NOTIMPL; }
	STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR *, UINT, LCID, DISPID *) noexcept override { return E_NOTIMPL; }
	STDMETHODIMP Invoke(DISPID dispIdMember, REFIID, LCID, WORD wFlags,
		DISPPARAMS *pDispParams, VARIANT *, EXCEPINFO *, UINT *) noexcept override {
		if (!(wFlags & DISPATCH_METHOD) || pDispParams == nullptr) {
			return S_OK;
		}
		if (dispIdMember == DISPID_BEFORENAVIGATE2 && pDispParams->cArgs >= 6) {
			const LPCWSTR url = DispParamBstr(pDispParams, 5);
			if (!IsAllowedPreviewNavigation(url)) {
				SetDispCancel(pDispParams, VARIANT_TRUE);
				HandlePreviewHyperlink(url);
			}
			return S_OK;
		}
		if (dispIdMember == DISPID_NEWWINDOW2 && pDispParams->cArgs >= 4) {
			const LPCWSTR url = DispParamBstr(pDispParams, 0);
			SetDispBoolRef(pDispParams, 3, VARIANT_TRUE);
			if (url != nullptr && url[0] != L'\0' && !IsAllowedPreviewNavigation(url)) {
				HandlePreviewHyperlink(url);
			}
			return S_OK;
		}
		if (dispIdMember == DISPID_NEWWINDOW3 && pDispParams->cArgs >= 7) {
			const LPCWSTR url = DispParamBstr(pDispParams, 3);
			SetDispBoolRef(pDispParams, 6, VARIANT_TRUE);
			if (url != nullptr && url[0] != L'\0' && !IsAllowedPreviewNavigation(url)) {
				HandlePreviewHyperlink(url);
			}
			return S_OK;
		}
		return S_OK;
	}
};

IConnectionPoint *g_pBrowserEventsCP = nullptr;
DWORD g_browserEventsCookie = 0;
PreviewBrowserEvents *g_pBrowserEvents = nullptr;

void DisconnectBrowserEvents() noexcept {
	if (g_pBrowserEventsCP != nullptr && g_browserEventsCookie != 0) {
		g_pBrowserEventsCP->Unadvise(g_browserEventsCookie);
		g_browserEventsCookie = 0;
	}
	if (g_pBrowserEventsCP != nullptr) {
		g_pBrowserEventsCP->Release();
		g_pBrowserEventsCP = nullptr;
	}
	if (g_pBrowserEvents != nullptr) {
		g_pBrowserEvents->Release();
		g_pBrowserEvents = nullptr;
	}
}

bool ConnectBrowserEvents() noexcept {
	if (g_pBrowser == nullptr || g_pBrowserEventsCP != nullptr) {
		return g_pBrowserEventsCP != nullptr;
	}
	IConnectionPointContainer *pCPC = nullptr;
	HRESULT hr = g_pBrowser->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void **>(&pCPC));
	if (FAILED(hr) || pCPC == nullptr) {
		PreviewMode_Log(L"ConnectBrowserEvents: no IConnectionPointContainer hr=0x%08lX", hr);
		return false;
	}
	hr = pCPC->FindConnectionPoint(DIID_DWebBrowserEvents2, &g_pBrowserEventsCP);
	pCPC->Release();
	if (FAILED(hr) || g_pBrowserEventsCP == nullptr) {
		PreviewMode_Log(L"ConnectBrowserEvents: FindConnectionPoint hr=0x%08lX", hr);
		g_pBrowserEventsCP = nullptr;
		return false;
	}
	g_pBrowserEvents = new PreviewBrowserEvents();
	if (g_pBrowserEvents == nullptr) {
		g_pBrowserEventsCP->Release();
		g_pBrowserEventsCP = nullptr;
		return false;
	}
	hr = g_pBrowserEventsCP->Advise(g_pBrowserEvents, &g_browserEventsCookie);
	if (FAILED(hr)) {
		PreviewMode_Log(L"ConnectBrowserEvents: Advise hr=0x%08lX", hr);
		DisconnectBrowserEvents();
		return false;
	}
	PreviewMode_Log(L"ConnectBrowserEvents: OK");
	return true;
}

bool ShowPreviewContextMenu(DWORD dwID, POINT *ppt, IUnknown *pcmdtReserved, IDispatch *pdispObject, HWND hwndParent) noexcept {
	if (ppt == nullptr || pcmdtReserved == nullptr) {
		return false;
	}

	IHTMLElement *pElem = nullptr;
	if (pdispObject != nullptr) {
		pdispObject->QueryInterface(IID_IHTMLElement, reinterpret_cast<void **>(&pElem));
	}

	WCHAR linkUrl[2048] = {};
	WCHAR imageUrl[2048] = {};
	const bool isImage = (dwID == CONTEXT_MENU_IMAGE) || (pElem != nullptr && GetElementTagIs(pElem, L"IMG"));
	const bool isAnchor = (dwID == CONTEXT_MENU_ANCHOR) || (pElem != nullptr && GetAnchorHref(pElem, linkUrl, NP2_COUNTOF(linkUrl)));
	if (isImage && pElem != nullptr) {
		GetImageSrc(pElem, imageUrl, NP2_COUNTOF(imageUrl));
	}
	if (!isAnchor && pElem != nullptr && linkUrl[0] == L'\0') {
		GetAnchorHref(pElem, linkUrl, NP2_COUNTOF(linkUrl));
	}

	const bool canCopy = QueryOleCommandEnabled(pcmdtReserved, OLECMDID_COPY);
	const bool canSelectAll = QueryOleCommandEnabled(pcmdtReserved, OLECMDID_SELECTALL);
	const bool showCopy = canCopy || dwID == CONTEXT_MENU_TEXTSELECT;

	HMENU hMenu = CreatePopupMenu();
	if (hMenu == nullptr) {
		if (pElem != nullptr) {
			pElem->Release();
		}
		return false;
	}

	if (showCopy) {
		AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_COPY, L"Copy");
	}
	if (canSelectAll) {
		AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_SELECTALL, L"Select &All");
	}
	if (isAnchor && linkUrl[0] != L'\0') {
		AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_OPEN_LINK, L"Open &Link");
		AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_COPY_LINK, L"Copy &Link");
	}
	if (isImage) {
		AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
		if (canCopy) {
			AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_COPY_IMAGE, L"Copy &Image");
		} else if (imageUrl[0] != L'\0') {
			AppendMenuW(hMenu, MF_STRING, ID_PREVIEW_CTX_COPY_LINK, L"Copy Image &URL");
		}
	}

	if (GetMenuItemCount(hMenu) == 0) {
		DestroyMenu(hMenu);
		if (pElem != nullptr) {
			pElem->Release();
		}
		return false;
	}

	POINT pt;
	if (!GetCursorPos(&pt)) {
		pt = *ppt;
	}
	const HWND hwndMenu = g_hwndMain != nullptr ? g_hwndMain : hwndParent;
	const UINT selected = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN,
		pt.x, pt.y, 0, hwndMenu, nullptr);
	DestroyMenu(hMenu);

	if (selected == 0) {
		if (pElem != nullptr) {
			pElem->Release();
		}
		return true;
	}

	IHTMLDocument2 *pDoc = GetPreviewHtmlDocument();
	switch (selected) {
	case ID_PREVIEW_CTX_COPY:
		if (!ExecHtmlCommand(pDoc, L"Copy")) {
			ExecOleCommand(pcmdtReserved, OLECMDID_COPY);
		}
		break;
	case ID_PREVIEW_CTX_SELECTALL:
		if (!ExecHtmlCommand(pDoc, L"SelectAll")) {
			ExecOleCommand(pcmdtReserved, OLECMDID_SELECTALL);
		}
		break;
	case ID_PREVIEW_CTX_OPEN_LINK:
		OpenUrl(linkUrl);
		break;
	case ID_PREVIEW_CTX_COPY_LINK:
		CopyTextToClipboard(hwndMenu, linkUrl[0] != L'\0' ? linkUrl : imageUrl);
		break;
	case ID_PREVIEW_CTX_COPY_IMAGE:
		ExecOleCommand(pcmdtReserved, OLECMDID_COPY);
		break;
	}

	if (pDoc != nullptr) {
		pDoc->Release();
	}
	if (pElem != nullptr) {
		pElem->Release();
	}
	PostMessage(hwndMenu, WM_NULL, 0, 0);
	return true;
}

class PreviewOleSite final : public IOleClientSite, public IOleInPlaceSite, public IDocHostUIHandler {
	ULONG m_ref = 1;
	HWND m_hwndParent;

public:
	explicit PreviewOleSite(HWND hwndParent) noexcept : m_hwndParent(hwndParent) {}

	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) noexcept override {
		if (ppv == nullptr) {
			return E_POINTER;
		}
		if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IOleClientSite)) {
			*ppv = static_cast<IOleClientSite *>(this);
		} else if (IsEqualIID(riid, IID_IOleInPlaceSite)) {
			*ppv = static_cast<IOleInPlaceSite *>(this);
		} else if (IsEqualIID(riid, IID_IDocHostUIHandler)) {
			*ppv = static_cast<IDocHostUIHandler *>(this);
		} else {
			*ppv = nullptr;
			return E_NOINTERFACE;
		}
		AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++m_ref; }
	STDMETHODIMP_(ULONG) Release() noexcept override {
		const ULONG ref = --m_ref;
		if (ref == 0) {
			delete this;
		}
		return ref;
	}

	STDMETHODIMP SaveObject() noexcept override { return S_OK; }
	STDMETHODIMP GetMoniker(DWORD, DWORD, IMoniker **) noexcept override { return E_NOTIMPL; }
	STDMETHODIMP GetContainer(IOleContainer **ppContainer) noexcept override {
		if (ppContainer) {
			*ppContainer = nullptr;
		}
		return E_NOINTERFACE;
	}
	STDMETHODIMP ShowObject() noexcept override { return S_OK; }
	STDMETHODIMP OnShowWindow(BOOL) noexcept override { return S_OK; }
	STDMETHODIMP RequestNewObjectLayout() noexcept override { return E_NOTIMPL; }

	STDMETHODIMP GetWindow(HWND *phwnd) noexcept override {
		if (phwnd) {
			*phwnd = m_hwndParent;
		}
		return S_OK;
	}
	STDMETHODIMP ContextSensitiveHelp(BOOL) noexcept override { return E_NOTIMPL; }

	STDMETHODIMP CanInPlaceActivate() noexcept override { return S_OK; }
	STDMETHODIMP OnInPlaceActivate() noexcept override { return S_OK; }
	STDMETHODIMP OnUIActivate() noexcept override { return S_OK; }
	STDMETHODIMP GetWindowContext(IOleInPlaceFrame **ppFrame, IOleInPlaceUIWindow **ppDoc, LPRECT, LPRECT, LPOLEINPLACEFRAMEINFO) noexcept override {
		if (ppFrame) {
			*ppFrame = nullptr;
		}
		if (ppDoc) {
			*ppDoc = nullptr;
		}
		return S_OK;
	}
	STDMETHODIMP Scroll(SIZE) noexcept override { return E_NOTIMPL; }
	STDMETHODIMP OnUIDeactivate(BOOL) noexcept override { return S_OK; }
	STDMETHODIMP OnInPlaceDeactivate() noexcept override { return S_OK; }
	STDMETHODIMP DiscardUndoState() noexcept override { return S_OK; }
	STDMETHODIMP DeactivateAndUndo() noexcept override { return S_OK; }
	STDMETHODIMP OnPosRectChange(LPCRECT) noexcept override { return S_OK; }

	STDMETHODIMP ShowContextMenu(DWORD dwID, POINT *ppt, IUnknown *pcmdtReserved, IDispatch *pdispObject) noexcept override {
		if (ShowPreviewContextMenu(dwID, ppt, pcmdtReserved, pdispObject, m_hwndParent)) {
			return S_OK;
		}
		return S_FALSE;
	}
	STDMETHODIMP GetHostInfo(DOCHOSTUIINFO *pInfo) noexcept override {
		if (pInfo) {
			pInfo->dwFlags = DOCHOSTUIFLAG_DPI_AWARE | DOCHOSTUIFLAG_THEME | DOCHOSTUIFLAG_FLAT_SCROLLBAR;
			pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
		}
		return S_OK;
	}
	STDMETHODIMP ShowUI(DWORD, IOleInPlaceActiveObject *, IOleCommandTarget *, IOleInPlaceFrame *, IOleInPlaceUIWindow *) noexcept override { return S_FALSE; }
	STDMETHODIMP HideUI() noexcept override { return S_OK; }
	STDMETHODIMP UpdateUI() noexcept override { return S_OK; }
	STDMETHODIMP EnableModeless(BOOL) noexcept override { return S_OK; }
	STDMETHODIMP OnDocWindowActivate(BOOL) noexcept override { return S_OK; }
	STDMETHODIMP OnFrameWindowActivate(BOOL) noexcept override { return S_OK; }
	STDMETHODIMP ResizeBorder(LPCRECT, IOleInPlaceUIWindow *, BOOL) noexcept override { return S_OK; }
	STDMETHODIMP TranslateAccelerator(LPMSG, const GUID *, DWORD) noexcept override { return S_FALSE; }
	STDMETHODIMP GetOptionKeyPath(LPOLESTR *, DWORD) noexcept override { return S_FALSE; }
	STDMETHODIMP GetDropTarget(IDropTarget *, IDropTarget **) noexcept override { return E_NOTIMPL; }
	STDMETHODIMP GetExternal(IDispatch **) noexcept override { return S_FALSE; }
	STDMETHODIMP TranslateUrl(DWORD, LPWSTR, LPWSTR *) noexcept override { return S_FALSE; }
	STDMETHODIMP FilterDataObject(IDataObject *, IDataObject **) noexcept override { return S_FALSE; }
};

void AppendHtmlEscapedUtf8(const char *text, size_t len, LPWSTR &dst, size_t &remaining) noexcept {
	WCHAR wbuf[512];
	while (len > 0 && remaining > 1) {
		const int chunk = static_cast<int>(min<size_t>(len, NP2_COUNTOF(wbuf) - 1));
		const int wlen = MultiByteToWideChar(CP_UTF8, 0, text, chunk, wbuf, chunk);
		if (wlen <= 0) {
			break;
		}
		for (int i = 0; i < wlen && remaining > 1; ++i) {
			const WCHAR ch = wbuf[i];
			LPCWSTR rep = nullptr;
			switch (ch) {
			case L'&': rep = L"&amp;"; break;
			case L'<': rep = L"&lt;"; break;
			case L'>': rep = L"&gt;"; break;
			case L'"': rep = L"&quot;"; break;
			default:
				*dst++ = ch;
				--remaining;
				continue;
			}
			while (rep && *rep && remaining > 1) {
				*dst++ = *rep++;
				--remaining;
			}
		}
		text += chunk;
		len -= chunk;
	}
}

void AppendPreviewShell(LPWSTR &dst, size_t &remaining, LPCWSTR body, bool dark) noexcept {
	static const WCHAR prefixLight[] =
		L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><base target=\"_self\"><style>"
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
		L"html,body,*{-ms-user-select:text;user-select:text;}"
		L"::selection{background:#B3D4FC;}"
		L"</style></head><body>";
	static const WCHAR prefixDark[] =
		L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><base target=\"_self\"><style>"
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
		L"html,body,*{-ms-user-select:text;user-select:text;}"
		L"::selection{background:#264F78;}"
		L"html,body,pre{scrollbar-base-color:#2d2d30;scrollbar-face-color:#3e3e42;scrollbar-track-color:#0d1117;"
		L"scrollbar-arrow-color:#c9d1d9;scrollbar-shadow-color:#1e1e1e;scrollbar-highlight-color:#3e3e42;"
		L"scrollbar-3dlight-color:#2d2d30;scrollbar-darkshadow-color:#1e1e1e;}"
		L"</style></head><body>";
	LPCWSTR prefix = dark ? prefixDark : prefixLight;
	while (*prefix && remaining > 1) {
		*dst++ = *prefix++;
		--remaining;
	}
	while (*body && remaining > 1) {
		*dst++ = *body++;
		--remaining;
	}
}

void ClosePreviewShell(LPWSTR &dst, size_t &remaining) noexcept {
	const WCHAR suffix[] = L"</body></html>";
	for (size_t i = 0; suffix[i] && remaining > 1; ++i) {
		*dst++ = suffix[i];
		--remaining;
	}
	*dst = L'\0';
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
		utf8Len -= chunk;
	}
}

void MarkdownToHtml(const char *text, size_t len, LPWSTR html, size_t htmlCch) noexcept {
	LPWSTR dst = html;
	size_t remaining = htmlCch;
	const bool dark = (np2StyleTheme == StyleTheme_Dark);
	AppendPreviewShell(dst, remaining, L"", dark);

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

	ClosePreviewShell(dst, remaining);
	PreviewMode_Log(L"MarkdownToHtml done");
}

void CsvToHtml(const char *text, size_t len, LPWSTR html, size_t htmlCch) noexcept {
	LPWSTR dst = html;
	size_t remaining = htmlCch;
	const bool dark = (np2StyleTheme == StyleTheme_Dark);
	AppendPreviewShell(dst, remaining, L"<table>", dark);

	const char *lineStart = text;
	const char *end = text + len;
	bool firstRow = true;
	while (lineStart < end && remaining > 16) {
		const char *lineEnd = lineStart;
		while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r') {
			++lineEnd;
		}
		if (lineEnd > lineStart) {
			const WCHAR *rowOpen = firstRow ? L"<tr><th>" : L"<tr><td>";
			const WCHAR *cellSep = firstRow ? L"</th><th>" : L"</td><td>";
			const WCHAR *rowClose = firstRow ? L"</th></tr>" : L"</td></tr>";
			firstRow = false;

			const char *cellStart = lineStart;
			for (const char *p = lineStart; p <= lineEnd; ++p) {
				if (p == lineEnd || *p == ',') {
					for (LPCWSTR s = rowOpen; *s && remaining > 1; ++s) {
						*dst++ = *s;
						--remaining;
					}
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
	ClosePreviewShell(dst, remaining);
}

void WrapDocumentHtml(const char *text, size_t len, LPWSTR html, size_t htmlCch) noexcept {
	if (len >= 5 && (_strnicmp(text, "<html", 5) == 0 || _strnicmp(text, "<!DOC", 5) == 0)) {
		MultiByteToWideChar(CP_UTF8, 0, text, static_cast<int>(len), html, static_cast<int>(htmlCch));
		return;
	}
	LPWSTR dst = html;
	size_t remaining = htmlCch;
	const bool dark = (np2StyleTheme == StyleTheme_Dark);
	AppendPreviewShell(dst, remaining, L"", dark);
	AppendHtmlEscapedUtf8(text, len, dst, remaining);
	ClosePreviewShell(dst, remaining);
}

void PumpPendingMessages() noexcept {
	++g_pumpDepth;
	int count = 0;
	MSG msg;
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		++count;
		if (count <= 8 || (count % 50) == 0) {
			PreviewMode_Log(L"PumpPendingMessages depth=%d n=%d msg=0x%04X hwnd=%p",
				g_pumpDepth, count, msg.message, msg.hwnd);
		}
		if (count > 500) {
			PreviewMode_Log(L"PumpPendingMessages ABORT after %d messages (depth=%d)", count, g_pumpDepth);
			break;
		}
		if (msg.message == WM_QUIT) {
			PostQuitMessage(static_cast<int>(msg.wParam));
			break;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if (count > 0) {
		PreviewMode_Log(L"PumpPendingMessages done depth=%d pumped=%d", g_pumpDepth, count);
	}
	--g_pumpDepth;
}

bool WaitBrowserReady(int maxMs = PREVIEW_READY_TIMEOUT_MS) noexcept {
	if (g_pBrowser == nullptr) {
		PreviewMode_Log(L"WaitBrowserReady: no browser");
		return false;
	}
	const DWORD start = GetTickCount();
	g_waitReadyIters = 0;
	while (GetTickCount() - start < static_cast<DWORD>(maxMs)) {
		++g_waitReadyIters;
		READYSTATE rs = READYSTATE_UNINITIALIZED;
		const HRESULT hr = g_pBrowser->get_ReadyState(&rs);
		if (g_waitReadyIters <= 5 || (g_waitReadyIters % 25) == 0) {
			PreviewMode_Log(L"WaitBrowserReady iter=%d hr=0x%08lX state=%s elapsed=%lums",
				g_waitReadyIters, hr, SUCCEEDED(hr) ? ReadyStateName(rs) : L"?", GetTickCount() - start);
		}
		if (SUCCEEDED(hr) && rs >= READYSTATE_INTERACTIVE) {
			PreviewMode_Log(L"WaitBrowserReady OK after %d iters (%lums)", g_waitReadyIters, GetTickCount() - start);
			return true;
		}
		PumpPendingMessages();
		Sleep(10);
	}
	PreviewMode_Log(L"WaitBrowserReady TIMEOUT after %d iters (%dms)", g_waitReadyIters, maxMs);
	return false;
}

void SchedulePreviewUpdate() noexcept {
	if (!g_active || g_hwndMain == nullptr) {
		PreviewMode_Log(L"SchedulePreviewUpdate skipped active=%d hwnd=%p", g_active, g_hwndMain);
		return;
	}
	if (!g_updatePosted) {
		g_updatePosted = true;
		PreviewMode_Log(L"SchedulePreviewUpdate PostMessage APPM_PREVIEW_UPDATE");
		PostMessage(g_hwndMain, APPM_PREVIEW_UPDATE, 0, 0);
	}
}

void DestroyBrowser() noexcept {
	PreviewMode_Log(L"DestroyBrowser begin");
	g_blankLoaded = false;
	DisconnectBrowserEvents();
	if (g_pInPlaceObject) {
		g_pInPlaceObject->InPlaceDeactivate();
		g_pInPlaceObject->Release();
		g_pInPlaceObject = nullptr;
	}
	if (g_pOleObject) {
		g_pOleObject->Close(OLECLOSE_NOSAVE);
		g_pOleObject->Release();
		g_pOleObject = nullptr;
	}
	if (g_pBrowser) {
		g_pBrowser->Release();
		g_pBrowser = nullptr;
	}
	if (g_pOleSite) {
		g_pOleSite->Release();
		g_pOleSite = nullptr;
	}
	PreviewMode_Log(L"DestroyBrowser end");
}

bool EnsureBrowser() noexcept {
	if (g_pBrowser != nullptr) {
		PreviewMode_Log(L"EnsureBrowser: reuse existing browser");
		return true;
	}
	if (g_hwndContainer == nullptr) {
		PreviewMode_Log(L"EnsureBrowser: no container hwnd");
		return false;
	}
	RECT rc;
	GetClientRect(g_hwndContainer, &rc);
	PreviewMode_Log(L"EnsureBrowser: container rect=%dx%d", rc.right, rc.bottom);
	if (rc.right <= 0 || rc.bottom <= 0) {
		PreviewMode_Log(L"EnsureBrowser: container size zero");
		return false;
	}

	PreviewMode_Log(L"EnsureBrowser: creating OleSite");
	g_pOleSite = new PreviewOleSite(g_hwndContainer);
	if (g_pOleSite == nullptr) {
		PreviewMode_Log(L"EnsureBrowser: OleSite alloc failed");
		return false;
	}

	PreviewMode_Log(L"EnsureBrowser: CoCreateInstance WebBrowser...");
	const DWORD t0 = GetTickCount();
	HRESULT hr = CoCreateInstance(CLSID_WebBrowser, nullptr,
		CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER, IID_IOleObject,
		reinterpret_cast<void **>(&g_pOleObject));
	PreviewMode_Log(L"EnsureBrowser: CoCreateInstance hr=0x%08lX (%lums)", hr, GetTickCount() - t0);
	if (FAILED(hr) || g_pOleObject == nullptr) {
		g_pOleSite->Release();
		g_pOleSite = nullptr;
		return false;
	}

	hr = g_pOleObject->SetClientSite(g_pOleSite);
	PreviewMode_Log(L"EnsureBrowser: SetClientSite hr=0x%08lX", hr);
	if (FAILED(hr)) {
		DestroyBrowser();
		return false;
	}

	OleSetContainedObject(g_pOleObject, TRUE);
	g_pOleObject->QueryInterface(IID_IWebBrowser2, reinterpret_cast<void **>(&g_pBrowser));
	g_pOleObject->QueryInterface(IID_IOleInPlaceObject, reinterpret_cast<void **>(&g_pInPlaceObject));

	PreviewMode_Log(L"EnsureBrowser: DoVerb INPLACEACTIVATE...");
	const DWORD t1 = GetTickCount();
	hr = g_pOleObject->DoVerb(OLEIVERB_INPLACEACTIVATE, nullptr, g_pOleSite, 0, g_hwndContainer, &rc);
	PreviewMode_Log(L"EnsureBrowser: DoVerb hr=0x%08lX (%lums)", hr, GetTickCount() - t1);
	if (FAILED(hr)) {
		DestroyBrowser();
		return false;
	}
	if (g_pInPlaceObject) {
		g_pInPlaceObject->SetObjectRects(&rc, &rc);
	}
	if (g_pBrowser) {
		g_pBrowser->put_Silent(VARIANT_TRUE);
		ConnectBrowserEvents();

		PreviewMode_Log(L"EnsureBrowser: Navigate2 about:blank...");
		const DWORD t2 = GetTickCount();
		VARIANT v;
		VariantInit(&v);
		v.vt = VT_BSTR;
		v.bstrVal = SysAllocString(L"about:blank");
		hr = g_pBrowser->Navigate2(&v, nullptr, nullptr, nullptr, nullptr);
		VariantClear(&v);
		PreviewMode_Log(L"EnsureBrowser: Navigate2 hr=0x%08lX (%lums)", hr, GetTickCount() - t2);
		const bool ready = WaitBrowserReady();
		PreviewMode_Log(L"EnsureBrowser: WaitBrowserReady after navigate=%s", ready ? L"yes" : L"no");
		g_blankLoaded = ready;
	}
	const bool ok = g_pBrowser != nullptr;
	PreviewMode_Log(L"EnsureBrowser: done ok=%d", ok);
	if (ok) {
		ApplyPreviewBrowserChrome();
	}
	return ok;
}

bool LoadHtmlIntoBrowser(LPCWSTR html) noexcept {
	if (g_pBrowser == nullptr || html == nullptr) {
		PreviewMode_Log(L"LoadHtmlIntoBrowser: null browser/html");
		return false;
	}
	const size_t htmlLen = wcslen(html);
	PreviewMode_Log(L"LoadHtmlIntoBrowser: html chars=%lu blankLoaded=%d", static_cast<unsigned long>(htmlLen), g_blankLoaded);
	if (!g_blankLoaded) {
		VARIANT v;
		VariantInit(&v);
		v.vt = VT_BSTR;
		v.bstrVal = SysAllocString(L"about:blank");
		const HRESULT hrNav = g_pBrowser->Navigate2(&v, nullptr, nullptr, nullptr, nullptr);
		VariantClear(&v);
		PreviewMode_Log(L"LoadHtmlIntoBrowser: Navigate2 hr=0x%08lX", hrNav);
		if (!WaitBrowserReady()) {
			return false;
		}
		g_blankLoaded = true;
	}

	if (!WaitBrowserReady(500)) {
		PreviewMode_Log(L"LoadHtmlIntoBrowser: short WaitBrowserReady failed");
		return false;
	}

	PreviewMode_Log(L"LoadHtmlIntoBrowser: get_Document...");
	IDispatch *pDoc = nullptr;
	const HRESULT hrDoc = g_pBrowser->get_Document(&pDoc);
	PreviewMode_Log(L"LoadHtmlIntoBrowser: get_Document hr=0x%08lX doc=%p", hrDoc, pDoc);
	if (FAILED(hrDoc) || pDoc == nullptr) {
		return false;
	}
	IHTMLDocument2 *pHTML = nullptr;
	const HRESULT hrQI = pDoc->QueryInterface(IID_IHTMLDocument2, reinterpret_cast<void **>(&pHTML));
	pDoc->Release();
	PreviewMode_Log(L"LoadHtmlIntoBrowser: QI IHTMLDocument2 hr=0x%08lX", hrQI);
	if (FAILED(hrQI) || pHTML == nullptr) {
		return false;
	}

	SAFEARRAY *psa = SafeArrayCreateVector(VT_VARIANT, 0, 1);
	if (psa == nullptr) {
		pHTML->Release();
		PreviewMode_Log(L"LoadHtmlIntoBrowser: SafeArrayCreate failed");
		return false;
	}
	VARIANT *pVar = nullptr;
	if (FAILED(SafeArrayAccessData(psa, reinterpret_cast<void **>(&pVar)))) {
		SafeArrayDestroy(psa);
		pHTML->Release();
		PreviewMode_Log(L"LoadHtmlIntoBrowser: SafeArrayAccessData failed");
		return false;
	}
	pVar->vt = VT_BSTR;
	pVar->bstrVal = SysAllocString(html);
	SafeArrayUnaccessData(psa);

	PreviewMode_Log(L"LoadHtmlIntoBrowser: clear+write...");
	const DWORD t0 = GetTickCount();
	pHTML->clear();
	const HRESULT hrWrite = pHTML->write(psa);
	PreviewMode_Log(L"LoadHtmlIntoBrowser: write hr=0x%08lX (%lums)", hrWrite, GetTickCount() - t0);
	SafeArrayDestroy(psa);
	pHTML->Release();
	if (SUCCEEDED(hrWrite)) {
		ApplyPreviewZoom();
		ApplyPreviewBrowserChrome();
	}
	return SUCCEEDED(hrWrite);
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

void UpdatePreviewContent() noexcept {
	PreviewMode_Log(L"UpdatePreviewContent enter active=%d inUpdate=%d dirty=%d lex=%d",
		g_active, g_inUpdate, g_dirty, pLexCurrent ? pLexCurrent->rid : -1);
	if (!g_active || pLexCurrent == nullptr || g_inUpdate) {
		return;
	}
	g_inUpdate = true;
	const unsigned serialAtStart = g_dirtySerial;
	if (!EnsureBrowser()) {
		PreviewMode_Log(L"UpdatePreviewContent: EnsureBrowser failed");
		g_inUpdate = false;
		g_dirty = true;
		SetTimer(g_hwndMain, ID_PREVIEW_TIMER, PREVIEW_LAYOUT_RETRY_MS, nullptr);
		return;
	}

	const Sci_Position length = min<Sci_Position>(SciCall_GetLength(), PREVIEW_MAX_TEXT_BYTES);
	PreviewMode_Log(L"UpdatePreviewContent: doc bytes=%lld", static_cast<long long>(length));
	const size_t textCapacity = static_cast<size_t>(max<Sci_Position>(length, 1)) + 1;
	char *text = static_cast<char *>(NP2HeapAlloc(textCapacity));
	GetDocumentUtf8(text, textCapacity);
	const size_t textLen = strlen(text);

	const size_t htmlCch = textCapacity * 8 + 4096;
	LPWSTR html = static_cast<LPWSTR>(NP2HeapAlloc(htmlCch * sizeof(WCHAR)));

	switch (pLexCurrent->rid) {
	case NP2LEX_MARKDOWN:
		PreviewMode_Log(L"UpdatePreviewContent: MarkdownToHtml");
		MarkdownToHtml(text, textLen, html, htmlCch);
		break;
	case NP2LEX_CSV:
		PreviewMode_Log(L"UpdatePreviewContent: CsvToHtml");
		CsvToHtml(text, textLen, html, htmlCch);
		break;
	default:
		PreviewMode_Log(L"UpdatePreviewContent: WrapDocumentHtml");
		WrapDocumentHtml(text, textLen, html, htmlCch);
		break;
	}

	const bool loaded = LoadHtmlIntoBrowser(html);
	PreviewMode_Log(L"UpdatePreviewContent: LoadHtmlIntoBrowser=%s", loaded ? L"ok" : L"fail");

	NP2HeapFree(html);
	NP2HeapFree(text);
	g_inUpdate = false;
	if (serialAtStart == g_dirtySerial) {
		g_dirty = false;
	} else {
		SchedulePreviewUpdate();
	}
	PreviewMode_Log(L"UpdatePreviewContent leave");
}

void ShowContainer(bool show) noexcept {
	if (g_hwndContainer != nullptr) {
		ShowWindow(g_hwndContainer, show ? SW_SHOW : SW_HIDE);
	}
}

void ShowSplitter(bool show) noexcept {
	if (g_hwndSplitter != nullptr) {
		ShowWindow(g_hwndSplitter, show ? SW_SHOW : SW_HIDE);
	}
}

void ApplyPreviewZoom() noexcept {
	if (g_pBrowser == nullptr) {
		return;
	}
	IDispatch *pDocDisp = nullptr;
	if (FAILED(g_pBrowser->get_Document(&pDocDisp)) || pDocDisp == nullptr) {
		return;
	}
	IHTMLDocument2 *pHTML = nullptr;
	const HRESULT hrQI = pDocDisp->QueryInterface(IID_IHTMLDocument2, reinterpret_cast<void **>(&pHTML));
	pDocDisp->Release();
	if (FAILED(hrQI) || pHTML == nullptr) {
		return;
	}
	IHTMLElement *pBody = nullptr;
	if (SUCCEEDED(pHTML->get_body(&pBody)) && pBody != nullptr) {
		IHTMLStyle *pStyle = nullptr;
		if (SUCCEEDED(pBody->get_style(&pStyle)) && pStyle != nullptr) {
			WCHAR zoomText[16];
			swprintf(zoomText, NP2_COUNTOF(zoomText), L"%d%%", g_previewZoomPercent);
			VARIANT v;
			VariantInit(&v);
			v.vt = VT_BSTR;
			v.bstrVal = SysAllocString(zoomText);
			if (v.bstrVal != nullptr) {
				pStyle->put_fontSize(v);
				SysFreeString(v.bstrVal);
			}
			pStyle->Release();
		}
		pBody->Release();
	}
	pHTML->Release();
}

LRESULT CALLBACK PreviewWheelSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
	UNREFERENCED_PARAMETER(hwnd);
	UNREFERENCED_PARAMETER(uIdSubclass);
	UNREFERENCED_PARAMETER(dwRefData);
	if (msg == WM_MOUSEWHEEL && g_active && (GetKeyState(VK_CONTROL) & 0x8000)) {
		const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
		if (delta != 0) {
			const int step = (delta > 0) ? PREVIEW_ZOOM_STEP : -PREVIEW_ZOOM_STEP;
			g_previewZoomPercent = clamp(g_previewZoomPercent + step, PREVIEW_ZOOM_MIN, PREVIEW_ZOOM_MAX);
			ApplyPreviewZoom();
			return 0;
		}
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void AttachPreviewWheelHandler(HWND hwnd) noexcept {
	if (hwnd != nullptr) {
		SetWindowSubclass(hwnd, PreviewWheelSubclassProc, PREVIEW_WHEEL_SUBCLASS_ID, 0);
	}
}

void ApplyPreviewBrowserChrome() noexcept {
	if (g_hwndContainer == nullptr) {
		return;
	}
	const bool apply = IsPreviewDarkChrome() && DarkMode_ShouldApply(true);

	auto styleBrowserHwnd = [&](HWND hwnd) noexcept {
		if (hwnd == nullptr) {
			return;
		}
		DarkMode_AllowWindow(hwnd, apply);
		SetWindowTheme(hwnd, apply ? L"DarkMode_Explorer" : L"Explorer", nullptr);
		if (apply) {
			if (InitializeFlatSB(hwnd)) {
				const COLORREF track = RGB(0x0D, 0x11, 0x17);
				const COLORREF thumb = RGB(0x3E, 0x3E, 0x42);
				FlatSB_SetScrollProp(hwnd, WSB_PROP_VBKGCOLOR, track, TRUE);
				FlatSB_SetScrollProp(hwnd, WSB_PROP_HBKGCOLOR, track, TRUE);
				FlatSB_SetScrollProp(hwnd, WSB_PROP_VCOLOR, thumb, TRUE);
				FlatSB_SetScrollProp(hwnd, WSB_PROP_HCOLOR, thumb, TRUE);
			}
		} else {
			UninitializeFlatSB(hwnd);
		}
		InvalidateRect(hwnd, nullptr, TRUE);
	};

	HWND hwndDoc = FindWindowExW(g_hwndContainer, nullptr, L"Internet Explorer_Server", nullptr);
	styleBrowserHwnd(hwndDoc);
	AttachPreviewWheelHandler(g_hwndContainer);
	AttachPreviewWheelHandler(hwndDoc);

	HWND hwndScroll = nullptr;
	while ((hwndScroll = FindWindowExW(g_hwndContainer, hwndScroll, L"ScrollBar", nullptr)) != nullptr) {
		styleBrowserHwnd(hwndScroll);
	}
	if (hwndDoc != nullptr) {
		hwndScroll = nullptr;
		while ((hwndScroll = FindWindowExW(hwndDoc, hwndScroll, L"ScrollBar", nullptr)) != nullptr) {
			styleBrowserHwnd(hwndScroll);
		}
	}
}

void RequestRelayout() noexcept {
	if (g_hwndMain != nullptr) {
		RECT rc;
		GetClientRect(g_hwndMain, &rc);
		PostMessage(g_hwndMain, APPM_PREVIEW_RELAYOUT, 0, MAKELPARAM(rc.right, rc.bottom));
	}
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
	if (g_pInPlaceObject && previewCy > 0 && cx > 0) {
		RECT rc = { 0, 0, cx, previewCy };
		g_pInPlaceObject->SetObjectRects(&rc, &rc);
	}
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
			RequestRelayout();
		}
		return 0;

	case WM_CAPTURECHANGED:
		if (g_splitterDragging && AsPointer<HWND>(lParam) != hwnd) {
			g_splitterDragging = false;
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

} // namespace

void PreviewMode_Init(HWND hwndMain) noexcept {
	PreviewMode_InitLog();
	g_hwndMain = hwndMain;
	EnsureSplitterClass();
	g_hwndContainer = CreateWindowExW(0, WC_STATIC, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
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
	PreviewMode_Log(L"OnTimer schedule update");
	SchedulePreviewUpdate();
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
	if (g_inUpdate) {
		SchedulePreviewUpdate();
		return;
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
	ApplyPreviewBrowserChrome();
	if (g_active) {
		PreviewMode_RequestUpdate();
	}
}
