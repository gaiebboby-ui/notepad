// Preview frontmatter (minimal YAML) for MD++ preview.
#pragma once

#include <cstddef>

struct PreviewMetadata {
	bool hasFrontmatter;
	char pageTitle[256];
	char pageDescription[512];
	char containerMaxWidth[64];
	char contentFont[128];
	char contentTextColor[96];
	char theme[16];
	char textDirection[8];
	bool mediaBlur;
	char containerInnerBg[64];
	char containerShadowColor[48];
	int containerShadowOffset;
	int containerShadowBlur;
};

void PreviewMetadataInit(PreviewMetadata *meta) noexcept;

// Strips leading --- YAML --- block. Returns body pointer/length into input buffer.
bool PreviewExtractFrontmatter(const char *input, size_t inputLen, PreviewMetadata *meta,
	const char **bodyOut, size_t *bodyLenOut) noexcept;

// Serializes metadata for WebView JSON message (UTF-8).
void PreviewMetadataToJson(const PreviewMetadata *meta, char *json, size_t jsonCap) noexcept;
