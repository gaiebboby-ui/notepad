// This file is part of Notepad.
#pragma once

#include <windows.h>

struct EDITLEXER;

#define ID_PREVIEW_TIMER	0xA003

void PreviewMode_Init(HWND hwndMain) noexcept;
void PreviewMode_Destroy() noexcept;
void PreviewMode_SetMainWindow(HWND hwndMain) noexcept;

bool PreviewMode_IsSupported(int lexerRid) noexcept;
bool PreviewMode_IsActive() noexcept;
bool PreviewMode_GetAutoEnable() noexcept;
void PreviewMode_SetAutoEnable(bool enable) noexcept;

void PreviewMode_ApplyForLexer(int lexerRid, bool fileOpen) noexcept;
void PreviewMode_SetActive(bool active) noexcept;
void PreviewMode_Toggle() noexcept;

void PreviewMode_Layout(int x, int y, int cx, int cy, int *pEditHeight) noexcept;
void PreviewMode_SetHeightPercent(int percent) noexcept;
int PreviewMode_GetHeightPercent() noexcept;
bool PreviewMode_IsMaximized() noexcept;
void PreviewMode_SetMaximized(bool maximized) noexcept;
void PreviewMode_ToggleMaximize() noexcept;
void PreviewMode_SetZoomPercent(int percent) noexcept;
int PreviewMode_GetZoomPercent() noexcept;
bool PreviewMode_HandleMouseWheel(WPARAM wParam, LPARAM lParam) noexcept;
void PreviewMode_RequestUpdate() noexcept;
void PreviewMode_OnTimer() noexcept;
void PreviewMode_OnPostedUpdate() noexcept;
void PreviewMode_OnThemeChanged() noexcept;
