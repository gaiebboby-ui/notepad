// Markdown preprocessors for MD++ preview (TOC, tabs, Rentry, media).

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Helpers.h"
#include "PreviewPreprocessor.h"

namespace {

struct GrowBuf {
	char *data = nullptr;
	size_t len = 0;
	size_t cap = 0;
	bool oom = false;
};

bool Grow(GrowBuf *b, size_t add) noexcept {
	const size_t need = b->len + add + 1;
	if (need <= b->cap) return true;
	size_t newCap = b->cap ? b->cap : 4096;
	while (newCap < need) newCap *= 2;
	char *next = static_cast<char *>(NP2HeapAlloc(newCap));
	if (!next) { b->oom = true; return false; }
	if (b->data) {
		memcpy(next, b->data, b->len);
		NP2HeapFree(b->data);
	}
	b->data = next;
	b->cap = newCap;
	return true;
}

void Append(GrowBuf *b, const char *s, size_t n) noexcept {
	if (b->oom || n == 0) return;
	if (!Grow(b, n)) return;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
}

void AppendCStr(GrowBuf *b, const char *s) noexcept {
	if (s) Append(b, s, strlen(s));
}

bool IsLineStart(const char *text, size_t len, size_t pos) noexcept {
	if (pos == 0) return true;
	return text[pos - 1] == '\n' || text[pos - 1] == '\r';
}

const char *SkipLine(const char *p, const char *end) noexcept {
	while (p < end && *p != '\n' && *p != '\r') ++p;
	if (p < end && *p == '\r') ++p;
	if (p < end && *p == '\n') ++p;
	return p;
}

const char *LineEnd(const char *p, const char *end) noexcept {
	const char *q = p;
	while (q < end && *q != '\n' && *q != '\r') ++q;
	return q;
}

bool LineEquals(const char *line, const char *lineEnd, const char *lit) noexcept {
	const size_t n = strlen(lit);
	if (static_cast<size_t>(lineEnd - line) != n) return false;
	return memcmp(line, lit, n) == 0;
}

void Slugify(const char *title, size_t titleLen, char *out, size_t outCap) noexcept {
	size_t o = 0;
	bool lastDash = false;
	for (size_t i = 0; i < titleLen && o + 1 < outCap; ++i) {
		unsigned char c = static_cast<unsigned char>(title[i]);
		if (isalnum(c) || c >= 128) {
			out[o++] = static_cast<char>(tolower(c));
			lastDash = false;
		} else if (c == ' ' || c == '-' || c == '_') {
			if (!lastDash && o > 0) {
				out[o++] = '-';
				lastDash = true;
			}
		}
	}
	while (o > 0 && out[o - 1] == '-') --o;
	out[o] = '\0';
	if (o == 0 && outCap > 7) {
		strcpy(out, "section");
	}
}

void BuildTocHtml(const char *text, size_t len, int maxDepth, GrowBuf *out) noexcept {
	AppendCStr(out, "<nav class=\"np2-toc\" data-auto=\"1\" data-depth=\"");
	char depthBuf[8];
	snprintf(depthBuf, sizeof(depthBuf), "%d", maxDepth);
	AppendCStr(out, depthBuf);
	AppendCStr(out, "\"><strong>Contents</strong></nav>");
	(void)text;
	(void)len;
}

void ReplaceTocMarkers(GrowBuf *buf) noexcept {
	if (buf->data == nullptr) return;
	const char *markers[] = { "[TOC2]", "[TOC]" };
	const int depths[] = { 2, 6 };
	for (int m = 0; m < 2; ++m) {
		const char *needle = markers[m];
		const size_t nlen = strlen(needle);
		char *pos = buf->data;
		while ((pos = strstr(pos, needle)) != nullptr) {
			if (!IsLineStart(buf->data, buf->len, static_cast<size_t>(pos - buf->data))) {
				pos += nlen;
				continue;
			}
			const char *lineEnd = pos + nlen;
			while (*lineEnd == ' ' || *lineEnd == '\t') ++lineEnd;
			if (*lineEnd != '\0' && *lineEnd != '\r' && *lineEnd != '\n') {
				pos += nlen;
				continue;
			}
			GrowBuf toc {};
			BuildTocHtml(buf->data, buf->len, depths[m], &toc);
			if (toc.data == nullptr) return;
			const size_t tail = buf->len - static_cast<size_t>(lineEnd - buf->data);
			const size_t newLen = static_cast<size_t>(pos - buf->data) + toc.len + tail;
			if (!Grow(buf, newLen - buf->len + toc.len)) {
				NP2HeapFree(toc.data);
				return;
			}
			memmove(pos + toc.len, lineEnd, tail + 1);
			memcpy(pos, toc.data, toc.len);
			buf->len = newLen;
			NP2HeapFree(toc.data);
			pos += toc.len;
		}
	}
}

const char *MapRentryColor(const char *name, size_t len) noexcept {
	struct ColorMap { const char *name; const char *css; };
	static const ColorMap map[] = {
		{ "red", "#e74c3c" }, { "green", "#27ae60" }, { "blue", "#3498db" },
		{ "yellow", "#f1c40f" }, { "orange", "#e67e22" }, { "purple", "#9b59b6" },
		{ "pink", "#e91e63" }, { "gray", "#95a5a6" }, { "grey", "#95a5a6" },
	};
	for (const auto &e : map) {
		if (_strnicmp(name, e.name, len) == 0 && strlen(e.name) == len) {
			return e.css;
		}
	}
	return nullptr;
}

bool IsBlankLine(const char *line, const char *lineEnd) noexcept {
	for (const char *q = line; q < lineEnd; ++q) {
		if (*q != ' ' && *q != '\t' && *q != '\r') {
			return false;
		}
	}
	return true;
}

bool IsGfmAdmonitionTagLine(const char *line, const char *lineEnd) noexcept {
	if (static_cast<size_t>(lineEnd - line) < 5) {
		return false;
	}
	if (line[0] != '>' || line[1] != ' ') {
		return false;
	}
	const char *p = line + 2;
	while (p < lineEnd && *p == ' ') {
		++p;
	}
	if (p >= lineEnd || *p != '[' || p + 1 >= lineEnd || p[1] != '!') {
		return false;
	}
	p += 2;
	bool hasWord = false;
	while (p < lineEnd && *p != ']') {
		if (!isalpha(static_cast<unsigned char>(*p))) {
			return false;
		}
		hasWord = true;
		++p;
	}
	return hasWord && p < lineEnd && *p == ']';
}

void NormalizeGfmAdmonitions(GrowBuf *buf) noexcept {
	if (buf->data == nullptr || buf->len == 0) {
		return;
	}
	GrowBuf out {};
	if (!Grow(&out, buf->len)) {
		return;
	}

	const char *p = buf->data;
	const char *end = buf->data + buf->len;
	while (p < end) {
		const char *lineStart = p;
		const char *lineEnd = LineEnd(p, end);
		const char *next = SkipLine(p, end);

		if (IsGfmAdmonitionTagLine(lineStart, lineEnd)) {
			while (lineEnd > lineStart && (lineEnd[-1] == ' ' || lineEnd[-1] == '\t')) {
				--lineEnd;
			}
			Append(&out, lineStart, static_cast<size_t>(lineEnd - lineStart));
			AppendCStr(&out, "\n");
			p = next;
			while (p < end) {
				const char *blEnd = LineEnd(p, end);
				if (!IsBlankLine(p, blEnd)) {
					break;
				}
				p = SkipLine(p, end);
			}
			continue;
		}

		Append(&out, lineStart, static_cast<size_t>(next - lineStart));
		p = next;
	}

	if (!out.oom && out.data) {
		NP2HeapFree(buf->data);
		*buf = out;
	} else if (out.data) {
		NP2HeapFree(out.data);
	}
}

void ConvertRentryAdmonitions(GrowBuf *buf) noexcept {
	if (buf->data == nullptr || buf->len == 0) return;
	GrowBuf out {};
	if (!Grow(&out, buf->len + 64)) {
		return;
	}
	const char *p = buf->data;
	const char *end = buf->data + buf->len;
	while (p < end) {
		const char *lineStart = p;
		const char *lineEnd = LineEnd(p, end);
		const char *next = SkipLine(p, end);
		if (static_cast<size_t>(lineEnd - lineStart) > 4 && memcmp(lineStart, "!!! ", 4) == 0) {
			const char *typeStart = lineStart + 4;
			const char *typeEnd = typeStart;
			while (typeEnd < lineEnd && !isspace(static_cast<unsigned char>(*typeEnd))) ++typeEnd;
			char gfmType[32];
			size_t typeLen = static_cast<size_t>(typeEnd - typeStart);
			typeLen = min(typeLen, sizeof(gfmType) - 1);
			memcpy(gfmType, typeStart, typeLen);
			gfmType[typeLen] = '\0';
			_strupr(gfmType);
			const char *titleStart = typeEnd;
			while (titleStart < lineEnd && (*titleStart == ' ' || *titleStart == '\t')) ++titleStart;
			AppendCStr(&out, "> [!");
			AppendCStr(&out, gfmType);
			AppendCStr(&out, "]\n");
			if (titleStart < lineEnd) {
				AppendCStr(&out, "> **");
				Append(&out, titleStart, static_cast<size_t>(lineEnd - titleStart));
				AppendCStr(&out, "**\n");
			}

			p = next;
			while (p < end) {
				const char *bodyStart = p;
				const char *bodyEnd = LineEnd(p, end);
				const char *bodyNext = SkipLine(p, end);
				if (bodyStart >= bodyEnd) {
					AppendCStr(&out, ">\n");
					p = bodyNext;
					continue;
				}
				if (*bodyStart == ' ' || *bodyStart == '\t') {
					while (bodyStart < bodyEnd && (*bodyStart == ' ' || *bodyStart == '\t')) ++bodyStart;
					AppendCStr(&out, "> ");
					if (bodyStart < bodyEnd) {
						Append(&out, bodyStart, static_cast<size_t>(bodyEnd - bodyStart));
					}
					AppendCStr(&out, "\n");
					p = bodyNext;
					continue;
				}
				break;
			}
			continue;
		}

		Append(&out, lineStart, static_cast<size_t>(next - lineStart));
		p = next;
	}
	if (out.data) {
		NP2HeapFree(buf->data);
		*buf = out;
	}
}

void ConvertRentryColors(GrowBuf *buf) noexcept {
	// %color% text %%  or  %color% text<EOL>  ->  <span class="np2-c" style="color:...">text</span>
	if (buf->data == nullptr) return;
	size_t read = 0;
	GrowBuf out {};
	while (read < buf->len) {
		if (buf->data[read] == '%') {
			size_t c1 = read + 1;
			while (c1 < buf->len && buf->data[c1] != '%' && buf->data[c1] != '\n' && buf->data[c1] != '\r') {
				++c1;
			}
			if (c1 < buf->len && buf->data[c1] == '%') {
				const size_t colorLen = c1 - read - 1;
				const char *colorName = buf->data + read + 1;
				const char *css = nullptr;
				char hexBuf[16];
				if (colorLen > 0 && colorLen < sizeof(hexBuf) && colorName[0] == '#') {
					memcpy(hexBuf, colorName, colorLen);
					hexBuf[colorLen] = '\0';
					css = hexBuf;
				} else if (colorLen > 0) {
					css = MapRentryColor(colorName, colorLen);
				}
				if (css) {
					size_t close = c1 + 1;
					bool closedByPercent = false;
					while (close < buf->len) {
						if (close + 1 < buf->len && buf->data[close] == '%' && buf->data[close + 1] == '%') {
							closedByPercent = true;
							break;
						}
						if (buf->data[close] == '\n' || buf->data[close] == '\r') {
							break;
						}
						++close;
					}
					char open[160];
					snprintf(open, sizeof(open), "<span class=\"np2-c\" style=\"color:%s\">", css);
					AppendCStr(&out, open);
					if (close > c1 + 1) {
						Append(&out, buf->data + c1 + 1, close - c1 - 1);
					}
					AppendCStr(&out, "</span>");
					read = closedByPercent ? close + 2 : close;
					continue;
				}
			}
		}
		Append(&out, buf->data + read, 1);
		++read;
	}
	if (out.data) {
		NP2HeapFree(buf->data);
		*buf = out;
	}
}

void ConvertAlignmentLines(GrowBuf *buf) noexcept {
	if (buf->data == nullptr || buf->len == 0) return;
	GrowBuf out {};
	const char *p = buf->data;
	const char *end = buf->data + buf->len;
	while (p < end) {
		const char *lineStart = p;
		const char *lineEnd = LineEnd(p, end);
		const char *next = SkipLine(p, end);
		if (static_cast<size_t>(lineEnd - lineStart) > 2 && memcmp(lineStart, "->", 2) == 0) {
			const char *content = lineStart + 2;
			while (content < lineEnd && *content == ' ') ++content;
			if (lineEnd - content >= 2 && lineEnd[-2] == '<' && lineEnd[-1] == '-') {
				AppendCStr(&out, "<p class=\"np2-align-center\">");
				Append(&out, content, static_cast<size_t>((lineEnd - 2) - content));
				AppendCStr(&out, "</p>\n");
				p = next;
				continue;
			}
			if (lineEnd - content >= 2 && lineEnd[-2] == '-' && lineEnd[-1] == '>') {
				AppendCStr(&out, "<p class=\"np2-align-right\">");
				Append(&out, content, static_cast<size_t>((lineEnd - 2) - content));
				AppendCStr(&out, "</p>\n");
				p = next;
				continue;
			}
		}
		Append(&out, lineStart, static_cast<size_t>(next - lineStart));
		p = next;
	}
	if (out.data) {
		NP2HeapFree(buf->data);
		*buf = out;
	}
}

void ConvertRentrySpoilers(GrowBuf *buf) noexcept {
	if (buf->data == nullptr) return;
	size_t read = 0;
	GrowBuf out {};
	while (read < buf->len) {
		if (buf->data[read] == '!' && read + 1 < buf->len && buf->data[read + 1] == '>') {
			size_t end = read + 2;
			while (end < buf->len && buf->data[end] != '\n' && buf->data[end] != '\r') ++end;
			AppendCStr(&out, "||");
			Append(&out, buf->data + read + 2, end - read - 2);
			AppendCStr(&out, "||");
			read = end;
			continue;
		}
		Append(&out, buf->data + read, 1);
		++read;
	}
	if (out.data) {
		NP2HeapFree(buf->data);
		*buf = out;
	}
}

void ConvertImageAttributes(GrowBuf *buf) noexcept {
	if (buf->data == nullptr) return;
	size_t read = 0;
	GrowBuf out {};
	while (read < buf->len) {
		if (buf->data[read] == '!' && read + 1 < buf->len && buf->data[read + 1] == '[') {
			size_t rb = read + 2;
			while (rb < buf->len && buf->data[rb] != ']') ++rb;
			if (rb < buf->len && rb + 1 < buf->len && buf->data[rb + 1] == '(') {
				size_t pb = rb + 2;
				size_t pe = pb;
				while (pe < buf->len && buf->data[pe] != ')') ++pe;
				if (pe < buf->len && pe + 1 < buf->len && buf->data[pe + 1] == '{') {
					size_t cb = pe + 2;
					size_t ce = cb;
					while (ce < buf->len && buf->data[ce] != '}') ++ce;
					if (ce < buf->len) {
						char alt[256] = "";
						char url[512] = "";
						char attrs[256] = "";
						memcpy(alt, buf->data + read + 2, min<size_t>(rb - read - 2, sizeof(alt) - 1));
						memcpy(url, buf->data + pb, min<size_t>(pe - pb, sizeof(url) - 1));
						memcpy(attrs, buf->data + cb, min<size_t>(ce - cb, sizeof(attrs) - 1));

						char width[32] = "";
						char height[32] = "";
						char floatCls[32] = "";
						char title[256] = "";
						char *tok = attrs;
						char *ctx = nullptr;
						for (char *part = strtok_s(tok, " ", &ctx); part; part = strtok_s(nullptr, " ", &ctx)) {
							if (strchr(part, ':')) {
								char *colon = strchr(part, ':');
								*colon = '\0';
								strncpy(width, part, sizeof(width) - 1);
								strncpy(height, colon + 1, sizeof(height) - 1);
							} else if (part[0] == '#') {
								if (_stricmp(part, "#left") == 0) strcpy(floatCls, "float-left");
								else if (_stricmp(part, "#right") == 0) strcpy(floatCls, "float-right");
							} else if (part[0] == '"') {
								strncpy(title, part + 1, sizeof(title) - 1);
								char *qt = strchr(title, '"');
								if (qt) *qt = '\0';
							}
						}

						char tag[1200];
						snprintf(tag, sizeof(tag),
							"<img src=\"%s\" alt=\"%s\"%s%s%s%s%s>",
							url, alt,
							width[0] ? " width=\"" : "", width,
							width[0] ? "\"" : "",
							height[0] ? " height=\"" : "", height);
						// rebuild properly
						snprintf(tag, sizeof(tag),
							"<img src=\"%s\" alt=\"%s\"%s%s%s%s%s%s%s>",
							url, alt,
							width[0] ? " width=\"" : "", width, width[0] ? "\"" : "",
							height[0] ? " height=\"" : "", height, height[0] ? "\"" : "",
							floatCls[0] ? " class=\"" : "");
						// simpler manual build
						char open[1100];
						int n = snprintf(open, sizeof(open), "<img src=\"%s\" alt=\"%s\"", url, alt);
						if (width[0] && n > 0) n += snprintf(open + n, sizeof(open) - n, " width=\"%s\"", width);
						if (height[0] && n > 0) n += snprintf(open + n, sizeof(open) - n, " height=\"%s\"", height);
						if (floatCls[0] && n > 0) n += snprintf(open + n, sizeof(open) - n, " class=\"%s\"", floatCls);
						if (title[0] && n > 0) n += snprintf(open + n, sizeof(open) - n, " title=\"%s\"", title);
						if (n > 0) snprintf(open + n, sizeof(open) - n, ">");
						AppendCStr(&out, open);
						read = ce + 1;
						continue;
					}
				}
			}
		}
		Append(&out, buf->data + read, 1);
		++read;
	}
	if (out.data) {
		NP2HeapFree(buf->data);
		*buf = out;
	}
}

void ConvertTableCellNewlines(GrowBuf *buf) noexcept {
	if (buf->data == nullptr) return;
	bool inTable = false;
	size_t read = 0;
	GrowBuf out {};
	while (read < buf->len) {
		const char *ls = buf->data + read;
		const char *le = LineEnd(ls, buf->data + buf->len);
		const bool isTableLine = (le > ls && strchr(ls, '|') != nullptr);
		if (isTableLine) {
			inTable = true;
			for (const char *p = ls; p < le; ++p) {
				if (p + 1 < le && p[0] == '\\' && p[1] == 'n') {
					AppendCStr(&out, "<br>");
					++p;
				} else {
					Append(&out, p, 1);
				}
			}
		} else {
			inTable = false;
			Append(&out, ls, static_cast<size_t>(le - ls));
		}
		if (le < buf->data + buf->len) {
			if (*le == '\r') { Append(&out, le, 1); ++le; }
			if (le < buf->data + buf->len && *le == '\n') Append(&out, le, 1);
		}
		read = static_cast<size_t>(le - buf->data);
		if (le < buf->data + buf->len) {
			if (buf->data[read] == '\r') ++read;
			if (read < buf->len && buf->data[read] == '\n') ++read;
		}
		(void)inTable;
	}
	if (out.data) {
		NP2HeapFree(buf->data);
		*buf = out;
	}
}

struct TabBlock {
	char title[128];
	char *content;
	size_t contentLen;
};

bool IsTabHeaderLine(const char *p, const char *lineEnd) noexcept {
	return (lineEnd - p) >= 5 && p[0] == '=' && p[1] == '=' && p[2] == '=' && p[3] == ' ' && p[4] == '"';
}

void ProcessTabs(GrowBuf *buf, PreviewMdRenderFn renderFn) noexcept {
	if (buf->data == nullptr || renderFn == nullptr) {
		return;
	}

	const char *p = buf->data;
	const char *end = buf->data + buf->len;
	GrowBuf rebuilt {};
	TabBlock tabs[16];

	while (p < end) {
		const char *lineEnd = LineEnd(p, end);
		if (!IsTabHeaderLine(p, lineEnd)) {
			Append(&rebuilt, p, static_cast<size_t>(lineEnd - p));
			if (lineEnd < end) {
				if (*lineEnd == '\r') {
					Append(&rebuilt, lineEnd, 1);
					++lineEnd;
				}
				if (lineEnd < end && *lineEnd == '\n') {
					Append(&rebuilt, lineEnd, 1);
				}
			}
			p = lineEnd;
			if (p < end && *p == '\r') {
				++p;
			}
			if (p < end && *p == '\n') {
				++p;
			}
			continue;
		}

		int tabCount = 0;
		while (p < end && IsTabHeaderLine(p, LineEnd(p, end)) && tabCount < 16) {
			lineEnd = LineEnd(p, end);
			TabBlock &tab = tabs[tabCount++];
			const char *titleStart = p + 5;
			const char *titleEnd = titleStart;
			while (titleEnd < lineEnd && *titleEnd != '"') {
				++titleEnd;
			}
			const size_t tlen = min<size_t>(static_cast<size_t>(titleEnd - titleStart), sizeof(tab.title) - 1);
			memcpy(tab.title, titleStart, tlen);
			tab.title[tlen] = '\0';

			p = lineEnd;
			if (p < end && *p == '\r') {
				++p;
			}
			if (p < end && *p == '\n') {
				++p;
			}

			const char *cStart = p;
			while (p < end) {
				lineEnd = LineEnd(p, end);
				if (IsTabHeaderLine(p, lineEnd)) {
					break;
				}
				p = lineEnd;
				if (p < end && *p == '\r') {
					++p;
				}
				if (p < end && *p == '\n') {
					++p;
				}
			}

			tab.contentLen = static_cast<size_t>(p - cStart);
			tab.content = static_cast<char *>(NP2HeapAlloc(tab.contentLen + 1));
			if (tab.content) {
				memcpy(tab.content, cStart, tab.contentLen);
				tab.content[tab.contentLen] = '\0';
			} else {
				tab.contentLen = 0;
			}
		}

		if (tabCount > 0) {
			AppendCStr(&rebuilt, "<div class=\"tab-set\"><div class=\"tab-buttons\">");
			for (int i = 0; i < tabCount; ++i) {
				AppendCStr(&rebuilt, "<button type=\"button\" class=\"tab-btn");
				if (i == 0) {
					AppendCStr(&rebuilt, " active");
				}
				AppendCStr(&rebuilt, "\">");
				AppendCStr(&rebuilt, tabs[i].title);
				AppendCStr(&rebuilt, "</button>");
			}
			AppendCStr(&rebuilt, "</div><div class=\"tab-panels\">");
			char panelHtml[65536];
			for (int i = 0; i < tabCount; ++i) {
				panelHtml[0] = '\0';
				if (tabs[i].content && tabs[i].contentLen > 0) {
					renderFn(tabs[i].content, tabs[i].contentLen, panelHtml, sizeof(panelHtml));
				}
				AppendCStr(&rebuilt, "<div class=\"tab-panel");
				if (i == 0) {
					AppendCStr(&rebuilt, " active");
				}
				AppendCStr(&rebuilt, "\">");
				AppendCStr(&rebuilt, panelHtml);
				AppendCStr(&rebuilt, "</div>");
				NP2HeapFree(tabs[i].content);
				tabs[i].content = nullptr;
			}
			AppendCStr(&rebuilt, "</div></div>");
		}
	}

	if (rebuilt.data) {
		NP2HeapFree(buf->data);
		*buf = rebuilt;
	}
}

} // namespace

bool Utf8ContainsMath(const char *text, size_t len) noexcept {
	if (text == nullptr || len < 3) return false;
	for (size_t i = 0; i + 1 < len; ++i) {
		if (text[i] == '$') {
			if (text[i + 1] == '$' && i + 2 < len) return true;
			for (size_t j = i + 1; j < len; ++j) {
				if (text[j] == '$' && j > i + 1) return true;
				if (text[j] == '\n') break;
			}
		}
	}
	return false;
}

bool PreviewPreprocessMarkdown(const char *input, size_t inputLen, char **outputOut, size_t *outputLenOut,
	PreviewMdRenderFn renderFn) noexcept {

	if (outputOut == nullptr || outputLenOut == nullptr) return false;
	*outputOut = nullptr;
	*outputLenOut = 0;
	if (input == nullptr) return false;

	GrowBuf buf {};
	if (!Grow(&buf, inputLen)) return false;
	Append(&buf, input, inputLen);

	NormalizeGfmAdmonitions(&buf);
	ConvertRentryAdmonitions(&buf);
	ConvertRentryColors(&buf);
	ConvertAlignmentLines(&buf);
	ConvertRentrySpoilers(&buf);
	ConvertImageAttributes(&buf);
	ConvertTableCellNewlines(&buf);
	ReplaceTocMarkers(&buf);
	ProcessTabs(&buf, renderFn);

	if (buf.oom || buf.data == nullptr) {
		NP2HeapFree(buf.data);
		return false;
	}

	*outputOut = buf.data;
	*outputLenOut = buf.len;
	return true;
}

void PreviewPreprocessFree(char *buf) noexcept {
	NP2HeapFree(buf);
}
