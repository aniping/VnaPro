#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { marked } from "./vendor/marked-17.0.5.esm.js";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const docsRoot = path.resolve(scriptDir, "..");

function markdownFiles(directory) {
  return fs.readdirSync(directory, { withFileTypes: true })
    .flatMap((entry) => {
      const target = path.join(directory, entry.name);
      if (entry.isDirectory()) return markdownFiles(target);
      return entry.isFile() && entry.name.endsWith(".md") ? [target] : [];
    })
    .sort();
}

function visibleInline(tokens = []) {
  return tokens.map((token) => {
    switch (token.type) {
      case "text":
      case "escape":
      case "codespan":
        return token.tokens ? visibleInline(token.tokens) : (token.text ?? "");
      case "strong":
      case "em":
      case "del":
      case "link":
        return visibleInline(token.tokens ?? []);
      case "image":
        return token.text ?? "";
      case "br":
        return " ";
      default:
        return token.tokens ? visibleInline(token.tokens) : (token.text ?? "");
    }
  }).join("");
}

// 当前文档标题只使用字母、数字、汉字、空格、连字符和下划线作为
// 可导航字符。下列规则与 github-slugger 对这些字符的处理一致：
// 转小写、移除控制符/标点/符号、把 ASCII 空格替换为连字符。
// 链接校验故意不接收 <a id="...">，因为目标渲染器可能清洗它。
function headingSlugBase(value) {
  let result = "";
  for (const character of value.toLowerCase()) {
    if (character === "-" || character === "_") {
      result += character;
      continue;
    }
    if (/[\p{C}\p{P}\p{S}]/u.test(character)) continue;
    result += character;
  }
  return result.replace(/ /g, "-");
}

function uniqueHeadingSlug(base, occurrences) {
  let result = base;
  while (Object.hasOwn(occurrences, result)) {
    occurrences[base] = (occurrences[base] ?? 0) + 1;
    result = `${base}-${occurrences[base]}`;
  }
  occurrences[result] = 0;
  return result;
}

function lineForHref(source, href) {
  const index = source.indexOf(href);
  if (index < 0) return 1;
  return source.slice(0, index).split("\n").length;
}

function singleSectionReference(visibleText) {
  const references = [
    ...new Set(
      [...visibleText.matchAll(/§\s*(\d+(?:\.\d+)+)/g)].map((match) => match[1]),
    ),
  ];
  return references.length === 1 ? references[0] : undefined;
}

function headingSectionNumber(headingText) {
  return headingText.match(/^(\d+(?:\.\d+)+)(?:\s|$)/)?.[1];
}

function exactPathExists(target) {
  const absolute = path.resolve(target);
  const parsed = path.parse(absolute);
  let current = parsed.root;
  for (const component of absolute.slice(parsed.root.length).split(path.sep)) {
    if (!component) continue;
    let entries;
    try {
      entries = fs.readdirSync(current);
    } catch {
      return false;
    }
    if (!entries.includes(component)) return false;
    current = path.join(current, component);
  }
  return true;
}

function decode(value, source, errors) {
  try {
    return decodeURIComponent(value);
  } catch {
    errors.push(`${source}: URI 编码无效：${value}`);
    return value;
  }
}

function documentIdsForFile(file) {
  const relative = path.relative(docsRoot, file).replaceAll(path.sep, "/");
  const detailed = relative.match(/^详细设计\/(0[1-5])([A-E])?-/);
  if (detailed !== null) {
    const family = `D${Number(detailed[1])}`;
    return new Set(
      detailed[2] === undefined ? [family] : [family, `${family}${detailed[2]}`],
    );
  }
  const architecture = relative.match(/^架构设计\/(0[0-6])-/);
  return architecture === null ? new Set() : new Set([`A${architecture[1]}`]);
}

// 已存在的链接能否跳转，与正文有没有忘记把“D4A 某协议”写成链接，
// 是两个不同问题。这里移除代码、标题和完整 Markdown 链接后，要求所有
// 指向其他详设篇章的 D1–D5 / D1A–D5E 标签都可点击。当前篇章及其专题
// 家族名可以直接使用，避免把“本篇属于 D3”误判成跨文档引用。
function unlinkedDocumentReferences(source, file) {
  const allowed = documentIdsForFile(file);
  const sanitized = source
    .replace(/```[^\n]*\n[\s\S]*?```/g, (value) => value.replace(/[^\n]/g, " "))
    .replace(/~~~[^\n]*\n[\s\S]*?~~~/g, (value) => value.replace(/[^\n]/g, " "))
    .replace(/^#{1,6}[^\n]*$/gm, (value) => " ".repeat(value.length))
    .replace(/!?\[[^\]\n]*\]\([^\n)]*\)/g, (value) => value.replace(/[^\n]/g, " "))
    .replace(/`[^`\n]*`/g, (value) => " ".repeat(value.length));

  const violations = [];
  for (const match of sanitized.matchAll(/\b(?:D[1-5](?:[A-E])?|A0[0-6])\b/g)) {
    if (allowed.has(match[0])) continue;
    violations.push({
      id: match[0],
      line: sanitized.slice(0, match.index).split("\n").length,
    });
  }
  return violations;
}

const files = markdownFiles(docsRoot);
const metadata = new Map();
const errors = [];
const externalLinks = new Set();
let headingCount = 0;

for (const file of files) {
  const source = fs.readFileSync(file, "utf8");
  const tokens = marked.lexer(source, { gfm: true, pedantic: false });
  const occurrences = Object.create(null);
  const baseOwners = new Map();
  const headings = new Map();
  const links = [];
  const rawAnchors = new Set();

  marked.walkTokens(tokens, (token) => {
    if (token.type === "heading") {
      const text = visibleInline(token.tokens ?? []);
      const base = headingSlugBase(text);
      const prior = baseOwners.get(base);
      if (prior !== undefined) {
        errors.push(
          `${path.relative(docsRoot, file)}: 标题“${text}”与“${prior}”生成重复锚点 ${base}`,
        );
      } else {
        baseOwners.set(base, text);
      }
      const slug = uniqueHeadingSlug(base, occurrences);
      headings.set(slug, text);
      headingCount += 1;
    } else if (token.type === "link" || token.type === "image") {
      links.push({
        href: token.href,
        type: token.type,
        visibleText: token.type === "link"
          ? visibleInline(token.tokens ?? [])
          : (token.text ?? ""),
      });
    }
  });

  for (const match of source.matchAll(/<a\s+(?:id|name)=["']([^"']+)["']/gi)) {
    if (rawAnchors.has(match[1])) {
      errors.push(`${path.relative(docsRoot, file)}: raw HTML 锚点重复：${match[1]}`);
    }
    rawAnchors.add(match[1]);
  }

  metadata.set(file, { source, headings, links, rawAnchors });

  for (const violation of unlinkedDocumentReferences(source, file)) {
    errors.push(
      `${path.relative(docsRoot, file)}:${violation.line}: `
        + `跨文档标签 ${violation.id} 不是可点击链接`,
    );
  }

  for (const link of links) {
    const href = link.href?.trim();
    if (/^https?:\/\//i.test(href ?? "")) externalLinks.add(href);
  }
}

let localReferenceCount = 0;
let fragmentCount = 0;
let rawOnlyCount = 0;

for (const [sourceFile, sourceMeta] of metadata) {
  for (const link of sourceMeta.links) {
    const href = link.href?.trim();
    if (!href || /^(?:[a-z][a-z0-9+.-]*:|\/\/)/i.test(href)) continue;

    localReferenceCount += 1;
    const hashIndex = href.indexOf("#");
    const beforeHash = hashIndex < 0 ? href : href.slice(0, hashIndex);
    const fragmentRaw = hashIndex < 0 ? "" : href.slice(hashIndex + 1);
    const queryIndex = beforeHash.indexOf("?");
    const pathRaw = queryIndex < 0 ? beforeHash : beforeHash.slice(0, queryIndex);
    const decodedPath = decode(pathRaw.replace(/^<|>$/g, ""), path.relative(docsRoot, sourceFile), errors);
    const targetFile = decodedPath
      ? path.resolve(path.dirname(sourceFile), decodedPath)
      : sourceFile;
    const sourceLabel = `${path.relative(docsRoot, sourceFile)}:${lineForHref(sourceMeta.source, href)}`;

    if (!exactPathExists(targetFile)) {
      errors.push(`${sourceLabel}: 目标路径不存在或大小写不精确：${href}`);
      continue;
    }
    if (!fragmentRaw) continue;

    fragmentCount += 1;
    const fragment = decode(fragmentRaw, sourceLabel, errors);
    const targetMeta = metadata.get(targetFile);
    if (targetMeta === undefined) {
      errors.push(`${sourceLabel}: 带片段的目标不是 docs 内 Markdown：${href}`);
      continue;
    }
    const targetHeading = targetMeta.headings.get(fragment);
    if (targetHeading !== undefined) {
      if (link.type === "link") {
        const visibleSection = singleSectionReference(link.visibleText ?? "");
        const targetSection = headingSectionNumber(targetHeading);
        if (visibleSection !== undefined && targetSection !== visibleSection) {
          errors.push(
            `${sourceLabel}: 链接文字写“§${visibleSection}”，`
              + `但目标标题是“${targetHeading}”：${href}`,
          );
        }
      }
      continue;
    }

    if (targetMeta.rawAnchors.has(fragment)) {
      rawOnlyCount += 1;
      errors.push(
        `${sourceLabel}: ${href} 只命中 raw HTML 锚点；请改为可见标题生成的原生锚点`,
      );
    } else {
      errors.push(`${sourceLabel}: 目标标题锚点不存在：${href}`);
    }
  }
}

const smokeLinks = [
  ["README.md", "./架构设计/06-架构约束与验收索引.md#6-阶段切片与十六个阻断性验收门"],
  ["架构设计/02-产品对象与关系.md", "../详细设计/04E-产品对象删除与后台处理.md#914-校准数据集的目录移除"],
  ["架构设计/04-运行时与触发.md", "../详细设计/04D-持久化结果与恢复.md#955-持久交付的唯一提交与恢复协议"],
  ["详细设计/04-应用事务校准与存储.md", "./04A-应用会话与事务.md#91-仪器会话instrumentsession"],
  ["reference/sna-binary-evidence-manifest.md", "../架构依据/02-SNA逆向证据.md#ev-rev-main-import-001"],
];

for (const [relativeFile, href] of smokeLinks) {
  const file = path.join(docsRoot, relativeFile);
  const source = metadata.get(file)?.source ?? "";
  if (!source.includes(`](${href})`)) {
    errors.push(`${relativeFile}: 缺少固定导航冒烟链接 ${href}`);
  }
}

const slugFixtures = new Map([
  ["9.1.2 `Command`、`Ticket` 与外部完成", "912-commandticket-与外部完成"],
  ["9.1.4 校准数据集的目录移除", "914-校准数据集的目录移除"],
  ["6. 阶段、切片与十六个阻断性验收门", "6-阶段切片与十六个阻断性验收门"],
]);
for (const [heading, expected] of slugFixtures) {
  // fixture 中的反引号模拟 marked 对代码片段输出可见文字后的结果。
  const visible = heading.replace(/`([^`]*)`/g, "$1");
  const actual = headingSlugBase(visible);
  if (actual !== expected) {
    errors.push(`slug fixture 失败：${heading} => ${actual}，预期 ${expected}`);
  }
}

const sectionReferenceFixtures = [
  ["D3C §8.5.6 目标准入", "8.5.6"],
  ["D3C 目标准入", undefined],
  ["D3 §8.1 与 §8.2", undefined],
];
for (const [visible, expected] of sectionReferenceFixtures) {
  const actual = singleSectionReference(visible);
  if (actual !== expected) {
    errors.push(`section-reference fixture 失败：${visible} => ${actual}，预期 ${expected}`);
  }
}

let checkedExternalCount = 0;
if (process.argv.includes("--external")) {
  const pending = [...externalLinks].sort();
  const workerCount = Math.min(4, pending.length);
  const retriableStatuses = new Set([408, 425, 429, 500, 502, 503, 504]);
  const maxAttempts = 3;
  let cursor = 0;

  const wait = (milliseconds) => new Promise((resolve) => {
    setTimeout(resolve, milliseconds);
  });

  async function verifyExternalLink(href) {
    let lastError;
    for (let attempt = 1; attempt <= maxAttempts; attempt += 1) {
      try {
        const response = await fetch(href, {
          method: "GET",
          redirect: "follow",
          headers: {
            "user-agent": "VnaPro-doc-navigation-check/1.0",
            range: "bytes=0-0",
          },
          signal: AbortSignal.timeout(25_000),
        });
        await response.body?.cancel();

        if (response.ok) {
          checkedExternalCount += 1;
          return;
        }
        if (!retriableStatuses.has(response.status) || attempt === maxAttempts) {
          checkedExternalCount += 1;
          errors.push(`外部链接返回 HTTP ${response.status}：${href}`);
          return;
        }
        lastError = new Error(`HTTP ${response.status}`);
      } catch (error) {
        lastError = error;
        if (attempt === maxAttempts) break;
      }
      await wait(400 * (2 ** (attempt - 1)));
    }
    errors.push(
      `外部链接不可达（重试 ${maxAttempts} 次）：${href}`
        + `（${lastError?.message ?? "unknown error"}）`,
    );
  }

  async function verifyNextExternalLink() {
    for (;;) {
      const index = cursor;
      cursor += 1;
      if (index >= pending.length) return;
      const href = pending[index];
      await verifyExternalLink(href);
    }
  }

  await Promise.all(Array.from({ length: workerCount }, verifyNextExternalLink));
}

const externalSummary = process.argv.includes("--external")
  ? `，${checkedExternalCount}/${externalLinks.size} 条外部链接已请求`
  : "";

if (errors.length > 0) {
  process.stderr.write(`${errors.join("\n")}\n`);
  process.stderr.write(
    `FAIL: ${files.length} 篇 Markdown，${localReferenceCount} 条本地引用，`
      + `${fragmentCount} 条片段引用，${rawOnlyCount} 条仅命中 raw HTML${externalSummary}。\n`,
  );
  process.exitCode = 1;
} else {
  process.stdout.write(
    `PASS: ${files.length} 篇 Markdown，${headingCount} 个唯一标题锚点，`
      + `${localReferenceCount} 条本地引用，${fragmentCount} 条片段引用；`
      + `0 条引用依赖 raw HTML 锚点${externalSummary}。\n`,
  );
}
