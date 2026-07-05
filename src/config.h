// This file is part of Notepad.
// See License.txt for details about distribution and modification.
#pragma once

//! Enable customize toolbar labels
// When enabled, user can change toolbar button tooltip text
// inside [Toolbar Labels] section in Notepad.ini
#define NP2_ENABLE_CUSTOMIZE_TOOLBAR_LABELS		0

//! Enable building with Hi-DPI image resources.
// When disabled, only build with image resources at 100% scaling.
#define NP2_ENABLE_HIDPI_IMAGE_RESOURCE			1

//! Default toolbar UI scale (percent of window DPI).
#define NP2_TOOLBAR_UI_SCALE_PERCENT			173

//! Enable the .LOG feature
// When enabled and Notepad opens a file starts with .LOG,
// Notepad will append current time to the file.
// This is a hidden feature in Windows Notepad.
#define NP2_ENABLE_DOT_LOG_FEATURE				0

//! Enable localization with satellite resource DLLs.
#define NP2_ENABLE_APP_LOCALIZATION_DLL			1
//! Enable test localization dialog layout with default UI font for target locale.
#define NP2_ENABLE_TEST_LOCALIZATION_LAYOUT		0

//! Enable localization for scheme/lexer names.
#define NP2_ENABLE_LOCALIZE_LEXER_NAME			1
//! Enable localization for scheme/lexer style names.
#define NP2_ENABLE_LOCALIZE_STYLE_NAME			1

//! File types listed in Windows Default Apps (Capabilities\FileAssociations).
#define NP2_FILE_ASSOCIATIONS	L".txt;.log;.ini;.inf;.md;.json;.xml;.bat;.cmd;.reg;.csv;.yaml;.yml;.cfg;.conf;.js;.ts;.css;.html;.htm"

// scintilla\include\LaTeXInput.h defined NP2_ENABLE_LATEX_LIKE_EMOJI_INPUT
