# WebView2 SDK (vendored)

Headers and `WebView2LoaderStatic.lib` from NuGet package
`Microsoft.Web.WebView2` **1.0.2903.40**
(`CORE_WEBVIEW_TARGET_PRODUCT_VERSION` `131.0.2903.40`).

## Runtime modes

| Build | Runtime |
|-------|---------|
| Slim (`Notepad_x64`) | System **Evergreen** WebView2 Runtime (usual on Windows 10/11) |
| Fat (`Notepad_x64_with_WebView2`) | Bundled **Fixed Version** `131.0.2903.146` under `<exe>\WebView2\` |

Fat pin is chosen for **compatibility** (Windows 10 ~19041/19044 and the SDK above), not the newest Chromium. When upgrading the SDK here, bump Fixed Version in the same 131.x / matching family via [`build/webview2-fixed-version.json`](../../build/webview2-fixed-version.json).

Host code prefers `<exe>\WebView2\msedgewebview2.exe` when present; otherwise Evergreen (`browserExecutableFolder = null`).

## Licenses

- SDK: NuGet / Microsoft terms (see root [`License.txt`](../../License.txt)).
- Fixed Version (fat only): full EULA + `ThirdPartyNotices*` stay inside `WebView2\` — do not strip. Also see root `NOTICE-WebView2.txt` in the fat zip.
- Fixed Version binaries are **not** committed to git; CI/local fat packaging downloads a Microsoft-acquired `.cab` (see pin `url` or `WEBVIEW2_FIXED_CAB` / `WEBVIEW2_FIXED_CAB_URL`).

## Packaging

```powershell
# After slim zip exists:
$env:WEBVIEW2_FIXED_CAB = 'C:\path\to\Microsoft.WebView2.FixedVersionRuntime.131.0.2903.146.x64.cab'
powershell -File build\make_zip_webview2.ps1
```
