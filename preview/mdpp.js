(function () {
	'use strict';

	var script = document.currentScript;
	var dark = script && script.getAttribute('data-dark') === '1';

	function setDarkMode(isDark) {
		dark = !!isDark;
		document.body.classList.toggle('np2-dark', dark);
		var hljsLight = document.getElementById('np2-hljs-light');
		var hljsDark = document.getElementById('np2-hljs-dark');
		if (hljsLight) hljsLight.disabled = dark;
		if (hljsDark) hljsDark.disabled = !dark;
		try {
			if (window.mermaid) {
				mermaid.initialize({ startOnLoad: false, securityLevel: 'loose', theme: dark ? 'dark' : 'default' });
			}
		} catch (e) { /* ignore */ }
	}

	function slugify(text) {
		return text.toLowerCase()
			.replace(/<[^>]+>/g, '')
			.trim()
			.replace(/[^\w\u0400-\u04FF\s-]/g, '')
			.replace(/\s+/g, '-')
			.replace(/-+/g, '-');
	}

	function assignHeadingIds(root) {
		var used = {};
		root.querySelectorAll('h1,h2,h3,h4,h5,h6').forEach(function (h) {
			if (h.id) return;
			var base = slugify(h.textContent || 'section');
			if (!base) base = 'section';
			var id = base;
			var n = 2;
			while (used[id]) { id = base + '-' + (n++); }
			used[id] = true;
			h.id = id;
		});
	}

	function fillToc(root) {
		root.querySelectorAll('nav.np2-toc[data-auto="1"]').forEach(function (nav) {
			var maxDepth = parseInt(nav.getAttribute('data-depth') || '6', 10);
			var ul = document.createElement('ul');
			root.querySelectorAll('h1,h2,h3,h4,h5,h6').forEach(function (h) {
				var level = parseInt(h.tagName.substring(1), 10);
				if (level > maxDepth) return;
				var li = document.createElement('li');
				li.style.marginLeft = ((level - 1) * 12) + 'px';
				var a = document.createElement('a');
				a.href = '#' + h.id;
				a.textContent = h.textContent;
				li.appendChild(a);
				ul.appendChild(li);
			});
			nav.appendChild(ul);
		});
	}

	function initSpoilers(root) {
		root.querySelectorAll('x-spoiler').forEach(function (el) {
			el.classList.add('spoiler');
			el.setAttribute('tabindex', '0');
			function reveal() { el.classList.add('revealed'); }
			el.addEventListener('click', reveal);
			el.addEventListener('keydown', function (e) {
				if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); reveal(); }
			});
		});
	}

	function initTabs(root) {
		root.querySelectorAll('.tab-set').forEach(function (set) {
			var buttons = set.querySelectorAll('.tab-btn');
			var panels = set.querySelectorAll('.tab-panel');
			buttons.forEach(function (btn, i) {
				btn.addEventListener('click', function () {
					buttons.forEach(function (b) { b.classList.remove('active'); });
					panels.forEach(function (p) { p.classList.remove('active'); });
					btn.classList.add('active');
					if (panels[i]) panels[i].classList.add('active');
				});
			});
		});
	}

	function initMediaBlur(root) {
		if (!document.body.classList.contains('np2-media-blur')) return;
		root.querySelectorAll('img').forEach(function (img) {
			img.addEventListener('click', function () { img.classList.add('revealed'); });
		});
	}

	function convertMermaid(root) {
		root.querySelectorAll('pre code.language-mermaid').forEach(function (code) {
			var pre = code.parentElement;
			if (!pre) return;
			var div = document.createElement('div');
			div.className = 'mermaid';
			div.textContent = code.textContent;
			pre.replaceWith(div);
		});
	}

	function renderKaTeX(root) {
		if (!window.katex) {
			console.warn('np2: KaTeX not loaded');
			return;
		}
		root.querySelectorAll('x-equation').forEach(function (el) {
			var display = el.getAttribute('type') === 'display';
			try {
				var html = katex.renderToString(el.textContent, {
					displayMode: display,
					throwOnError: false,
					trust: false
				});
				var span = document.createElement(display ? 'div' : 'span');
				span.className = display ? 'katex-display' : 'katex';
				span.innerHTML = html;
				el.replaceWith(span);
			} catch (e) { /* keep source */ }
		});
	}

	function highlightCode(root) {
		if (!window.hljs) {
			console.warn('np2: highlight.js not loaded');
			return;
		}
		root.querySelectorAll('pre code').forEach(function (block) {
			if (block.classList.contains('language-mermaid')) return;
			var lang = null;
			block.classList.forEach(function (c) {
				if (c.indexOf('language-') === 0) lang = c;
			});
			try {
				if (lang) {
					var res = hljs.highlight(block.textContent, { language: lang.slice(9) });
					block.innerHTML = res.value;
					block.classList.add('hljs');
				} else {
					hljs.highlightElement(block);
				}
			} catch (e) { /* ignore */ }
		});
	}

	function embedYouTube(root) {
		root.querySelectorAll('a[href*="youtube.com/watch"], a[href*="youtu.be/"]').forEach(function (a) {
			if (a.classList.contains('yt-preview')) return;
			var href = a.getAttribute('href') || '';
			var m = href.match(/[?&]v=([A-Za-z0-9_-]{11})/) || href.match(/youtu\.be\/([A-Za-z0-9_-]{11})/);
			if (!m) return;
			var id = m[1];
			var wrap = document.createElement('a');
			wrap.className = 'yt-preview';
			wrap.href = href;
			wrap.target = '_blank';
			wrap.rel = 'noopener';
			var img = document.createElement('img');
			img.src = 'https://img.youtube.com/vi/' + id + '/hqdefault.jpg';
			img.alt = 'YouTube: ' + id;
			wrap.appendChild(img);
			a.replaceWith(wrap);
		});
	}

	function applyMetadata(meta) {
		if (!meta) return;
		var content = document.getElementById('np2-content');
		var theme = meta.theme || 'auto';
		if (theme === 'dark') setDarkMode(true);
		else if (theme === 'light') setDarkMode(false);

		if (meta.textDirection === 'rtl') document.body.setAttribute('dir', 'rtl');
		else document.body.setAttribute('dir', 'ltr');
		if (meta.contentFont) {
			document.body.style.fontFamily = meta.contentFont;
		}
		if (meta.contentTextColor) {
			var parts = meta.contentTextColor.split(/\s+/);
			if (parts.length >= 2) {
				document.documentElement.style.setProperty('--np2-fg', dark ? parts[1] : parts[0]);
			} else if (parts[0]) {
				document.documentElement.style.setProperty('--np2-fg', parts[0]);
			}
		}
		if (meta.mediaBlur) {
			document.body.classList.add('np2-media-blur');
		} else {
			document.body.classList.remove('np2-media-blur');
		}
		if (meta.containerInnerBg) {
			document.body.style.background = meta.containerInnerBg;
		}
		if (meta.containerShadowBlur > 0) {
			var ox = meta.containerShadowOffset || 0;
			var color = meta.containerShadowColor || 'rgba(0,0,0,.2)';
			if (content) content.style.boxShadow = ox + 'px ' + ox + 'px ' + meta.containerShadowBlur + 'px ' + color;
		}
	}

	function applyMetadataFromDom(root) {
		var body = root.querySelector('#np2-body');
		if (!body) return;
		var meta = {
			theme: body.getAttribute('data-theme') || 'auto',
			textDirection: body.getAttribute('dir') || body.getAttribute('data-text-direction') || 'ltr',
			mediaBlur: body.getAttribute('data-media-blur') === '1',
			contentFont: body.getAttribute('data-content-font') || '',
			contentTextColor: body.getAttribute('data-content-text-color') || '',
			containerInnerBg: body.getAttribute('data-container-inner-bg') || '',
			containerShadowColor: body.getAttribute('data-container-shadow-color') || '',
			containerShadowOffset: parseInt(body.getAttribute('data-container-shadow-offset') || '0', 10),
			containerShadowBlur: parseInt(body.getAttribute('data-container-shadow-blur') || '0', 10)
		};
		applyMetadata(meta);
	}

	async function np2Apply(payload) {
		var root = document.getElementById('np2-content');
		if (!root) return;

		var html = '';
		var meta = null;
		if (typeof payload === 'string') {
			html = payload;
		} else if (payload) {
			html = payload.html || '';
			meta = payload.meta || null;
		}

		var y = window.scrollY || document.documentElement.scrollTop || 0;
		root.innerHTML = html;
		applyMetadata(meta);
		applyMetadataFromDom(root);
		assignHeadingIds(root);
		fillToc(root);
		initSpoilers(root);
		initTabs(root);
		convertMermaid(root);
		renderKaTeX(root);
		highlightCode(root);
		embedYouTube(root);
		initMediaBlur(root);

		try {
			if (window.mermaid) {
				await mermaid.run({ querySelector: '#np2-content .mermaid' });
			}
		} catch (e) { /* ignore */ }

		window.scrollTo(0, y);
	}

	setDarkMode(dark);
	try {
		if (window.mermaid) {
			mermaid.initialize({ startOnLoad: false, securityLevel: 'loose', theme: dark ? 'dark' : 'default' });
		}
	} catch (e) { /* ignore */ }

	window.np2Apply = np2Apply;

	function initPreviewZoom() {
		if (!window.chrome || !window.chrome.webview) return;
		window.addEventListener('wheel', function (e) {
			if (!e.ctrlKey) return;
			e.preventDefault();
			var dir = e.deltaY < 0 ? '+1' : '-1';
			window.chrome.webview.postMessage('np2:zoom:' + dir);
		}, { passive: false });
	}

	initPreviewZoom();

	if (window.chrome && window.chrome.webview) {
		window.chrome.webview.addEventListener('message', function (e) {
			var data = e.data;
			if (typeof data === 'string') {
				try {
					var parsed = JSON.parse(data);
					if (parsed && (parsed.html !== undefined || parsed.meta)) {
						np2Apply(parsed);
						return;
					}
				} catch (ex) { /* plain html */ }
				np2Apply(data);
			} else {
				np2Apply(data);
			}
		});
	}
})();
