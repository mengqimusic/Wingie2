// Wingie2 跨页面顶部导航栏。
// 每个子页面 <body> 顶部引 <script src="../nav.js"></script> 即可加载。
// 改导航栏只改这一处。当前页高亮由 location.pathname 自动判断。
(function () {
  "use strict";

  var items = [
    { label: "Wingie2", href: "../", match: "$root" },
    { label: "手册 / Manual", href: "../manual/", match: "/manual/" },
    { label: "配置 / Configuration", href: "../config/", match: "/config/" },
    // 每次发版同步改 href 与 match 为新 vX.Y/ 目录；只有最新目录命中选中态。
    { label: "最新固件 / Latest Firmware", href: "../v4.30/", match: "/v4.30/" }
  ];

  var path = location.pathname;

  function isCurrent(item) {
    if (item.match === "$root") {
      // 根入口页：路径以 /Wingie2/ 结尾或就是根，且不含 manual/config/v 数字
      return /\/Wingie2\/?$/.test(path) && !/\/(manual|config|v\d)/.test(path);
    }
    return path.indexOf(item.match) !== -1;
  }

  // 生成导航链接
  var links = items.map(function (item) {
    if (isCurrent(item)) {
      return '<span class="wg-nav-current">' + item.label + "</span>";
    }
    return '<a href="' + item.href + '">' + item.label + "</a>";
  });

  // 旧版本页（/vX.Y/ 但非最新目录）：末尾追加置灰标记，其余项均不选中。
  var oldVersionMatch = path.match(/\/(v[\d.]+)\//);
  if (oldVersionMatch && path.indexOf(items[items.length - 1].match) === -1) {
    links.push('<span class="wg-nav-old">旧版本 ' + oldVersionMatch[1] + " / Old version</span>");
  }

  var nav = document.createElement("nav");
  nav.id = "wg-nav";
  nav.innerHTML = links.join('<span class="wg-nav-sep"> · </span>');

  // 内联样式（不依赖页面现有 CSS，保证各页面一致）
  var style = document.createElement("style");
  style.textContent = [
    "#wg-nav {",
    "  position: sticky; top: 0; z-index: 9999;",
    "  display: flex; flex-wrap: wrap; align-items: center; gap: 0;",
    "  margin: 0; padding: 6px 16px;",
    "  background: #c0c0c0;",
    "  border-bottom: 2px outset #fff;",
    "  font: 14px/1.4 Georgia, \"Times New Roman\", serif;",
    "  color: #000;",
    "}",
    "#wg-nav a { color: #0000EE; text-decoration: underline; padding: 2px 0; }",
    "#wg-nav a:visited { color: #551A8B; }",
    "#wg-nav a:hover { color: #FF0000; }",
    "#wg-nav .wg-nav-current { font-weight: 700; color: #000; padding: 2px 0; }",
    "#wg-nav .wg-nav-sep { color: #666; margin: 0 6px; user-select: none; }",
    "#wg-nav .wg-nav-old { color: #666; padding: 2px 0; user-select: none; }",
    "@media (max-width: 600px) {",
    "  #wg-nav { font-size: 12px; padding: 5px 10px; }",
    "  #wg-nav .wg-nav-sep { margin: 0 4px; }",
    "}"
  ].join("\n");

  document.head.appendChild(style);
  document.body.insertBefore(nav, document.body.firstChild);
})();
