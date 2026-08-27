(() => {
  "use strict";

  const root = document.getElementById("conversation");
  const md = window.markdownit({ html: false, linkify: false, typographer: false, breaks: true });
  const defaultFence = md.renderer.rules.fence.bind(md.renderer.rules);

  md.renderer.rules.image = (tokens, index) => {
    const alt = md.utils.escapeHtml(tokens[index].content || "image");
    return `<span class="image-omitted">[${alt}]</span>`;
  };
  md.renderer.rules.link_open = (tokens, index, options, env, self) => {
    const token = tokens[index];
    const hrefIndex = token.attrIndex("href");
    const href = hrefIndex >= 0 ? token.attrs[hrefIndex][1] : "";
    token.attrSet("href", "#");
    token.attrSet("data-external", href);
    token.attrSet("rel", "noreferrer noopener");
    return self.renderToken(tokens, index, options);
  };
  md.renderer.rules.fence = (tokens, index, options, env, self) => {
    const token = tokens[index];
    if ((token.info || "").trim().toLowerCase() === "mermaid") {
      return `<div class="mermaid" data-source="${encodeURIComponent(token.content)}"></div>`;
    }
    return defaultFence(tokens, index, options, env, self);
  };

  window.mermaid.initialize({
    startOnLoad: false,
    securityLevel: "strict",
    htmlLabels: false,
    suppressErrorRendering: true,
    maxTextSize: 50000,
    theme: "base"
  });

  function applyTheme(raw) {
    let values = {};
    try { values = JSON.parse(raw || "{}"); } catch (_) { return; }
    for (const key of ["canvas", "raised", "text", "secondary", "link", "border", "user"]) {
      if (/^#[0-9a-f]{6}$/i.test(values[key] || "")) {
        document.documentElement.style.setProperty(`--${key}`, values[key]);
      }
    }
  }

  async function renderDocument(raw) {
    let messages = [];
    try { messages = JSON.parse(raw || "[]"); } catch (_) { return; }
    root.replaceChildren();
    for (const message of messages.slice(0, 5000)) {
      const article = document.createElement("article");
      const role = message.role === "user" ? "user" : "agent";
      article.className = `message ${role}`;
      const author = document.createElement("div");
      author.className = "author";
      author.textContent = String(message.author || message.agent || "Agent").slice(0, 80);
      const content = document.createElement("div");
      content.className = "content";
      const taskText = String(message.text || "")
        .replace(/^(\s*[-*+]\s+)\[ \]/gm, "$1☐")
        .replace(/^(\s*[-*+]\s+)\[[xX]\]/gm, "$1☑");
      const rendered = md.render(taskText);
      content.innerHTML = DOMPurify.sanitize(rendered, {
        USE_PROFILES: { html: true },
        ADD_ATTR: ["data-external"],
        FORBID_TAGS: ["style", "script", "iframe", "object", "embed", "form", "img", "video", "audio"],
        FORBID_ATTR: ["style", "src", "srcset", "onerror", "onclick"]
      });
      article.append(author, content);
      root.append(article);
      try {
        renderMathInElement(content, {
          throwOnError: false,
          strict: "error",
          trust: false,
          delimiters: [
            { left: "$$", right: "$$", display: true },
            { left: "\\[", right: "\\]", display: true },
            { left: "\\(", right: "\\)", display: false }
          ]
        });
      } catch (_) {}
    }

    let diagramIndex = 0;
    for (const node of root.querySelectorAll(".mermaid[data-source]")) {
      const source = decodeURIComponent(node.dataset.source || "");
      node.removeAttribute("data-source");
      try {
        const result = await window.mermaid.render(`snack-diagram-${diagramIndex++}`, source);
        node.innerHTML = DOMPurify.sanitize(result.svg, { USE_PROFILES: { svg: true, svgFilters: true } });
      } catch (_) {
        node.className = "render-error";
        node.textContent = "Mermaid diagram could not be rendered.";
      }
    }
    window.scrollTo({ top: document.documentElement.scrollHeight, behavior: "auto" });
  }

  document.addEventListener("click", event => {
    const link = event.target.closest("a[data-external]");
    if (!link || !window.snackBridge) return;
    event.preventDefault();
    window.snackBridge.requestExternalLink(link.dataset.external || "");
  });

  new QWebChannel(qt.webChannelTransport, channel => {
    const bridge = channel.objects.snackRenderer;
    window.snackBridge = bridge;
    bridge.documentChanged.connect(() => renderDocument(bridge.documentJson));
    bridge.themeChanged.connect(() => applyTheme(bridge.themeJson));
    applyTheme(bridge.themeJson);
    renderDocument(bridge.documentJson);
  });
})();
