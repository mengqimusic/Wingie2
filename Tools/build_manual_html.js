#!/usr/bin/env node
// 把 MANUAL.zh.md / MANUAL.en.md / MPE.md / ALT_TUNING.md 转成自包含 HTML，
// 风格对齐 gh-pages 的 Web 1.0 instrument-manual 页面。
//
// 用法（在仓库根目录运行）：
//   npx --yes markdown-it-named-headings markdown-it   # 一次性确认依赖可用
//   node Tools/build_manual_html.js                    # 生成到 /tmp/wingie2-manual/
//
// 需要的 npm 包：markdown-it, markdown-it-named-headings, markdown-it-linkify
// 通过 npx 或本地 node_modules 提供。脚本本身不安装依赖。

"use strict";

const fs = require("fs");
const path = require("path");

const MarkdownIt = require("markdown-it");

// GitHub 风格 slug：保留中文，小写，空格转 -，去标点（保留 - 和字母数字及非 ASCII）。
// 匹配 MANUAL.md 目录里手写的锚点（如 #1-电源、#8-midi-通道与控制信息）。
function githubSlug(text) {
  return text
    .toLowerCase()
    .replace(/[`*_~()（）{}\[\]#+.!<>:"'/\\|,]/g, "")   // 去标点（含半角全角括号 / .）
    .replace(/\s/g, "-")                            // 每个空格各转 -（不合并，匹配 GitHub）
    .replace(/^-|-$/g, "");                          // 去首尾 -
}

const REPO = path.resolve(__dirname, "..");
const OUT = "/tmp/wingie2-manual";

// ---- markdown-it 实例 ----
const md = new MarkdownIt({
  html: true,        // 允许内联 HTML（手册里有 <br>）
  linkify: true,     // 自动识别 URL
  typographer: false // 不做引号转换，避免改中文标点
});

// heading_open 后紧跟 inline token，其 content 是标题文本，据此生成 GitHub 风格 id。
md.core.ruler.push("github_heading_id", function (state) {
  for (let i = 0; i < state.tokens.length - 1; i++) {
    if (state.tokens[i].type === "heading_open") {
      // 收集后续 inline token 的文本内容
      let text = "";
      for (let j = i + 1; j < state.tokens.length; j++) {
        if (state.tokens[j].type === "heading_close") break;
        if (state.tokens[j].type === "inline") {
          for (const c of state.tokens[j].children) {
            if (c.content) text += c.content;
          }
        }
      }
      const slug = githubSlug(text);
      if (slug) state.tokens[i].attrSet("id", slug);
    }
  }
});

// 相对链接改写：.md → .html（同目录）
const defaultLinkOpen =
  md.renderer.rules.link_open ||
  function (tokens, idx, options, env, self) {
    return self.renderToken(tokens, idx, options);
  };
md.renderer.rules.link_open = function (tokens, idx, options, env, self) {
  const hrefIndex = tokens[idx].attrIndex("href");
  if (hrefIndex >= 0) {
    const v = tokens[idx].attrs[hrefIndex][1];
    if (/\.md$/i.test(v) && !/^https?:/i.test(v)) {
      tokens[idx].attrs[hrefIndex][1] = v.replace(/\.md$/i, ".html");
    }
    // 外部链接加 target=_blank
    if (/^https?:/i.test(v)) {
      tokens[idx].attrSet("target", "_blank");
      tokens[idx].attrSet("rel", "noopener");
    }
  }
  return defaultLinkOpen(tokens, idx, options, env, self);
};

// ---- 内联 CSS（Web 1.0 instrument-manual 风格）----
const CSS = `
  :root { color-scheme: light; }
  html { box-sizing: border-box; }
  *, *::before, *::after { box-sizing: inherit; }
  body {
    margin: 0; padding: 16px;
    color: #000; background: #c0c0c0;
    font: 16px/1.6 "Times New Roman", Times, "Noto Serif", serif;
    font-variant-numeric: tabular-nums;
  }
  a { color: #0000EE; text-decoration: underline; }
  a:visited { color: #551A8B; }
  a:active { color: #FF0000; }
  #wingie-manual {
    width: min(760px, 100%);
    min-height: calc(100vh - 32px);
    margin: 0 auto;
    padding: 28px 32px;
    border: 2px outset #fff;
    background: #fff;
  }
  #wingie-manual h1 {
    margin: 0 0 6px;
    font-family: Georgia, "Times New Roman", serif;
    font-size: 30px; line-height: 1.25; font-weight: 700;
    text-align: center; letter-spacing: .01em;
  }
  #wingie-manual h2 {
    margin: 28px 0 8px;
    font-family: Georgia, "Times New Roman", serif;
    font-size: 21px; font-weight: 700;
    border-bottom: 2px solid #000; padding-bottom: 3px;
  }
  #wingie-manual h3 {
    margin: 20px 0 6px;
    font-family: Georgia, "Times New Roman", serif;
    font-size: 17px; font-weight: 700;
  }
  #wingie-manual p { margin: 8px 0; }
  #wingie-manual ul, #wingie-manual ol { margin: 8px 0; padding-left: 1.6rem; }
  #wingie-manual li + li { margin-top: 4px; }
  #wingie-manual hr { border: none; border-top: 1px solid #ccc; margin: 24px 0; }
  #wingie-manual blockquote {
    margin: 12px 0; padding: 6px 16px;
    border-left: 3px solid #999; background: #f5f5f5; color: #333;
  }
  #wingie-manual blockquote p { margin: 4px 0; }
  #wingie-manual code {
    font-family: "Courier New", Courier, monospace;
    background: #eee; padding: 1px 4px; border-radius: 2px; font-size: 0.92em;
  }
  #wingie-manual pre {
    background: #f5f5f5; border: 1px solid #ddd; padding: 12px;
    overflow-x: auto; margin: 12px 0;
  }
  #wingie-manual pre code { background: none; padding: 0; }
  #wingie-manual .table-wrap { border: 2px outset #ddd; overflow-x: auto; margin: 12px 0; }
  #wingie-manual table { width: 100%; border-collapse: collapse; }
  #wingie-manual th, #wingie-manual td {
    padding: 5px 8px; border: 1px inset #ccc; text-align: left; background: #fff;
    vertical-align: top;
  }
  #wingie-manual th {
    background: #c0c0c0; font-family: "MS Sans Serif", Geneva, sans-serif;
    font-size: 12px; text-transform: uppercase; letter-spacing: .03em;
  }
  #wingie-manual strong { font-weight: 700; }
  #wingie-manual img {
    max-width: 100%; height: auto; display: block;
    margin: 16px auto; border: 1px solid #ccc;
  }
  @media (max-width: 600px) {
    #wingie-manual { padding: 18px 16px; }
    #wingie-manual h1 { font-size: 24px; }
  }
`;

// 表格包一层 div.table-wrap（Web 1.0 风格外框 + 横向滚动）
const defaultTableOpen =
  md.renderer.rules.table_open ||
  function (tokens, idx, options, env, self) {
    return self.renderToken(tokens, idx, options);
  };
const defaultTableClose =
  md.renderer.rules.table_close ||
  function (tokens, idx, options, env, self) {
    return self.renderToken(tokens, idx, options);
  };
md.renderer.rules.table_open = function () {
  return '<div class="table-wrap">' + defaultTableOpen.apply(null, arguments);
};
md.renderer.rules.table_close = function () {
  return defaultTableClose.apply(null, arguments) + "</div>";
};

// ---- HTML 外壳 ----
function wrap(body, title) {
  return `<!doctype html>
<html lang="${title.includes("English") || title.includes("User Manual") ? "en" : "zh-CN"}">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <title>${title}</title>
  <style>${CSS}</style>
</head>
<body>
  <script src="../nav.js"></script>
  <div id="wingie-manual">
${body}
  </div>
</body>
</html>
`;
}

// ---- 转换配置 ----
const jobs = [
  { src: "MANUAL.zh.md", out: "index.html", title: "小羽二代 Wingie2 用户手册 · v4" },
  { src: "MANUAL.en.md", out: "en.html", title: "Wingie2 User Manual · v4" },
  { src: "MPE.md", out: "MPE.html", title: "Wingie2 MPE" },
  { src: "ALT_TUNING.md", out: "ALT_TUNING.html", title: "Wingie2 Alternate Tunings" }
];

// ---- 执行 ----
fs.mkdirSync(OUT, { recursive: true });

for (const job of jobs) {
  const srcPath = path.join(REPO, job.src);
  if (!fs.existsSync(srcPath)) {
    console.error(`跳过（源文件不存在）：${job.src}`);
    continue;
  }
  const raw = fs.readFileSync(srcPath, "utf-8");
  const body = md.render(raw);
  const html = wrap(body, job.title);
  const dest = path.join(OUT, job.out);
  fs.writeFileSync(dest, html, "utf-8");
  console.log(`✓ ${job.src} → ${dest} (${html.length} bytes)`);
}

console.log(`\n完成，输出目录：${OUT}`);
