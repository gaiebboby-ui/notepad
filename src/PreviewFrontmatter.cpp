// Preview frontmatter (minimal YAML) for MD++ preview.

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Helpers.h"
#include "PreviewFrontmatter.h"

namespace {

void CopyMetaString(char *dst, size_t dstCap, const char *src, size_t srcLen) noexcept {
	if (dstCap == 0) {
		return;
	}
	if (srcLen >= dstCap) {
		srcLen = dstCap - 1;
	}
	memcpy(dst, src, srcLen);
	dst[srcLen] = '\0';
}

void TrimInPlace(char *s) noexcept {
	if (s == nullptr) {
		return;
	}
	char *start = s;
	while (*start == ' ' || *start == '\t') {
		++start;
	}
	if (start != s) {
		memmove(s, start, strlen(start) + 1);
	}
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
		s[--len] = '\0';
	}
}

bool StartsWith(const char *s, const char *prefix) noexcept {
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

void UnquoteYamlValue(char *value) noexcept {
	TrimInPlace(value);
	const size_t len = strlen(value);
	if (len >= 2 && ((value[0] == '"' && value[len - 1] == '"') || (value[0] == '\'' && value[len - 1] == '\''))) {
		value[len - 1] = '\0';
		memmove(value, value + 1, len - 1);
	}
}

bool ParseBool(const char *value) noexcept {
	return _stricmp(value, "true") == 0 || strcmp(value, "1") == 0 || _stricmp(value, "yes") == 0;
}

void ApplyMetaKey(PreviewMetadata *meta, const char *section, const char *key, char *value) noexcept {
	UnquoteYamlValue(value);
	if (section[0] == '\0') {
		if (_stricmp(key, "title") == 0) {
			CopyMetaString(meta->pageTitle, sizeof(meta->pageTitle), value, strlen(value));
		} else if (_stricmp(key, "description") == 0) {
			CopyMetaString(meta->pageDescription, sizeof(meta->pageDescription), value, strlen(value));
		}
		return;
	}
	if (_stricmp(section, "page") == 0) {
		if (_stricmp(key, "title") == 0) {
			CopyMetaString(meta->pageTitle, sizeof(meta->pageTitle), value, strlen(value));
		} else if (_stricmp(key, "description") == 0) {
			CopyMetaString(meta->pageDescription, sizeof(meta->pageDescription), value, strlen(value));
		}
	} else if (_stricmp(section, "style") == 0) {
		if (_stricmp(key, "container_max_width") == 0) {
			CopyMetaString(meta->containerMaxWidth, sizeof(meta->containerMaxWidth), value, strlen(value));
		} else if (_stricmp(key, "content_font") == 0) {
			CopyMetaString(meta->contentFont, sizeof(meta->contentFont), value, strlen(value));
		} else if (_stricmp(key, "content_text_color") == 0) {
			CopyMetaString(meta->contentTextColor, sizeof(meta->contentTextColor), value, strlen(value));
		} else if (_stricmp(key, "container_inner_background_color") == 0) {
			CopyMetaString(meta->containerInnerBg, sizeof(meta->containerInnerBg), value, strlen(value));
		} else if (_stricmp(key, "container_shadow_color") == 0) {
			CopyMetaString(meta->containerShadowColor, sizeof(meta->containerShadowColor), value, strlen(value));
		} else if (_stricmp(key, "container_shadow_offset") == 0) {
			meta->containerShadowOffset = atoi(value);
		} else if (_stricmp(key, "container_shadow_blur") == 0) {
			meta->containerShadowBlur = atoi(value);
		}
	} else if (_stricmp(section, "access") == 0) {
		if (_stricmp(key, "theme") == 0) {
			CopyMetaString(meta->theme, sizeof(meta->theme), value, strlen(value));
		} else if (_stricmp(key, "text_direction") == 0) {
			CopyMetaString(meta->textDirection, sizeof(meta->textDirection), value, strlen(value));
		}
	} else if (_stricmp(section, "safety") == 0) {
		if (_stricmp(key, "media_blur") == 0) {
			meta->mediaBlur = ParseBool(value);
		}
	}
}

void ParseFrontmatterYaml(const char *yaml, size_t yamlLen, PreviewMetadata *meta) noexcept {
	char lineBuf[1024];
	char section[64] = "";
	char key[128];
	char value[512];

	size_t i = 0;
	while (i < yamlLen) {
		size_t lineStart = i;
		while (i < yamlLen && yaml[i] != '\n' && yaml[i] != '\r') {
			++i;
		}
		const size_t lineLen = i - lineStart;
		if (lineLen >= sizeof(lineBuf)) {
			if (i < yamlLen && yaml[i] == '\r') ++i;
			if (i < yamlLen && yaml[i] == '\n') ++i;
			continue;
		}
		memcpy(lineBuf, yaml + lineStart, lineLen);
		lineBuf[lineLen] = '\0';
		if (i < yamlLen && yaml[i] == '\r') ++i;
		if (i < yamlLen && yaml[i] == '\n') ++i;

		TrimInPlace(lineBuf);
		if (lineBuf[0] == '\0' || lineBuf[0] == '#') {
			continue;
		}

		char *colon = strchr(lineBuf, ':');
		if (colon == nullptr) {
			continue;
		}
		*colon = '\0';
		char *left = lineBuf;
		char *right = colon + 1;
		TrimInPlace(left);
		TrimInPlace(right);

		if (right[0] == '\0') {
			CopyMetaString(section, sizeof(section), left, strlen(left));
			continue;
		}

		CopyMetaString(key, sizeof(key), left, strlen(left));
		CopyMetaString(value, sizeof(value), right, strlen(right));
		ApplyMetaKey(meta, section, key, value);
	}
}

void JsonAppendStr(char *json, size_t &pos, size_t cap, const char *key, const char *val) noexcept {
	if (val[0] == '\0') {
		return;
	}
	if (pos > 1) {
		if (pos + 1 < cap) json[pos++] = ',';
	}
	const int n = snprintf(json + pos, cap - pos, "\"%s\":\"", key);
	if (n <= 0) return;
	pos += static_cast<size_t>(n);
	for (const char *p = val; *p && pos + 2 < cap; ++p) {
		if (*p == '\\' || *p == '"') {
			json[pos++] = '\\';
		}
		if (pos + 1 < cap) {
			json[pos++] = *p;
		}
	}
	if (pos + 1 < cap) {
		json[pos++] = '"';
	}
}

} // namespace

void PreviewMetadataInit(PreviewMetadata *meta) noexcept {
	memset(meta, 0, sizeof(*meta));
	strcpy(meta->theme, "auto");
	strcpy(meta->textDirection, "ltr");
}

bool PreviewExtractFrontmatter(const char *input, size_t inputLen, PreviewMetadata *meta,
	const char **bodyOut, size_t *bodyLenOut) noexcept {

	PreviewMetadataInit(meta);
	*bodyOut = input;
	*bodyLenOut = inputLen;

	if (input == nullptr || inputLen < 4) {
		return false;
	}
	if (!(input[0] == '-' && input[1] == '-' && input[2] == '-')) {
		return false;
	}
	size_t i = 3;
	if (i < inputLen && input[i] == '\r') ++i;
	if (i < inputLen && input[i] == '\n') ++i;
	else if (!(i < inputLen && input[i] == '\n')) {
		return false;
	}

	const size_t yamlStart = i;
	size_t yamlEnd = inputLen;
	for (; i + 2 < inputLen; ++i) {
		if (input[i] == '-' && input[i + 1] == '-' && input[i + 2] == '-') {
			if (i == yamlStart || input[i - 1] == '\n' || (i > yamlStart && input[i - 1] == '\r')) {
				yamlEnd = i;
				i += 3;
				if (i < inputLen && input[i] == '\r') ++i;
				if (i < inputLen && input[i] == '\n') ++i;
				break;
			}
		}
	}

	if (yamlEnd == inputLen) {
		return false;
	}

	meta->hasFrontmatter = true;
	ParseFrontmatterYaml(input + yamlStart, yamlEnd - yamlStart, meta);
	*bodyOut = input + i;
	*bodyLenOut = inputLen - i;
	return true;
}

void PreviewMetadataToJson(const PreviewMetadata *meta, char *json, size_t jsonCap) noexcept {
	if (jsonCap < 8) {
		if (jsonCap > 0) json[0] = '\0';
		return;
	}
	size_t pos = 0;
	json[pos++] = '{';

	JsonAppendStr(json, pos, jsonCap, "theme", meta->theme);
	JsonAppendStr(json, pos, jsonCap, "textDirection", meta->textDirection);
	JsonAppendStr(json, pos, jsonCap, "containerMaxWidth", meta->containerMaxWidth);
	JsonAppendStr(json, pos, jsonCap, "contentFont", meta->contentFont);
	JsonAppendStr(json, pos, jsonCap, "contentTextColor", meta->contentTextColor);
	JsonAppendStr(json, pos, jsonCap, "containerInnerBg", meta->containerInnerBg);
	JsonAppendStr(json, pos, jsonCap, "containerShadowColor", meta->containerShadowColor);

	if (meta->mediaBlur && pos + 24 < jsonCap) {
		if (pos > 1) json[pos++] = ',';
		memcpy(json + pos, "\"mediaBlur\":true", 16);
		pos += 16;
	}
	if (meta->containerShadowOffset != 0 && pos + 32 < jsonCap) {
		if (pos > 1) json[pos++] = ',';
		pos += static_cast<size_t>(snprintf(json + pos, jsonCap - pos, "\"containerShadowOffset\":%d", meta->containerShadowOffset));
	}
	if (meta->containerShadowBlur != 0 && pos + 32 < jsonCap) {
		if (pos > 1) json[pos++] = ',';
		pos += static_cast<size_t>(snprintf(json + pos, jsonCap - pos, "\"containerShadowBlur\":%d", meta->containerShadowBlur));
	}

	if (pos + 1 < jsonCap) {
		json[pos++] = '}';
		json[pos] = '\0';
	}
}
