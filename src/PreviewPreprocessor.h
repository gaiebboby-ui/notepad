// Markdown preprocessors for MD++ preview (TOC, tabs, Rentry, media).
#pragma once

#include <cstddef>

struct MdTabSet;

using PreviewMdRenderFn = void (*)(const char *markdown, size_t len, char *html, size_t htmlCap);

// Allocates output with NP2HeapAlloc; caller frees with PreviewPreprocessFree.
bool PreviewPreprocessMarkdown(const char *input, size_t inputLen, char **outputOut, size_t *outputLenOut,
	PreviewMdRenderFn renderFn) noexcept;

void PreviewPreprocessFree(char *buf) noexcept;

bool Utf8ContainsMath(const char *text, size_t len) noexcept;
