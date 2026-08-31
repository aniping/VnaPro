#!/usr/bin/env node

import fs from "node:fs";
import { marked } from "./vendor/marked-17.0.5.esm.js";

const source = fs.readFileSync(0, "utf8");

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
      case "html":
        return /^\s*<!--[\s\S]*-->\s*$/.test(token.raw) ? "" : "";
      default:
        return token.tokens ? visibleInline(token.tokens) : (token.text ?? "");
    }
  }).join("");
}

function tableRecord(token) {
  return {
    raw: token.raw,
    header: token.header.map((cell) => visibleInline(cell.tokens ?? [])),
    headerSource: token.header.map((cell) => cell.text ?? ""),
    rows: token.rows.map((row) =>
      row.map((cell) => visibleInline(cell.tokens ?? []))),
    rowsSource: token.rows.map((row) =>
      row.map((cell) => cell.text ?? "")),
  };
}

function leadingAssertionId(item) {
  const firstBlock = item.tokens?.[0];
  const firstInline = firstBlock?.tokens?.[0];
  const code = firstInline?.type === "strong"
    && firstInline.tokens?.length === 1
    && firstInline.tokens[0]?.type === "codespan"
    ? firstInline.tokens[0].text
    : null;
  return typeof code === "string" && /^ASSERT-[A-Z0-9-]+$/.test(code)
    ? code
    : null;
}

function assertionRecord(item) {
  const id = leadingAssertionId(item);
  return id === null ? null : { id, raw: item.raw };
}

function topLevelRecord(token) {
  const record = {
    type: token.type,
    raw: token.raw ?? "",
  };
  if (token.type === "heading") {
    record.depth = token.depth;
    record.text = visibleInline(token.tokens ?? []);
  } else if (token.type === "table") {
    Object.assign(record, tableRecord(token));
  } else if (token.type === "code") {
    record.lang = token.lang ?? "";
    record.text = token.text ?? "";
  } else if (token.type === "paragraph") {
    record.text = visibleInline(token.tokens ?? []);
  }
  return record;
}

try {
  const tokens = marked.lexer(source, {
    gfm: true,
    pedantic: false,
  });
  const headings = [];
  const tables = [];
  const html = [];
  const codes = [];
  const assertionItems = [];

  marked.walkTokens(tokens, (token) => {
    if (token.type === "heading") {
      headings.push({
        depth: token.depth,
        raw: token.raw,
        text: visibleInline(token.tokens ?? []),
      });
    } else if (token.type === "table") {
      tables.push(tableRecord(token));
    } else if (token.type === "html") {
      html.push(token.raw);
    } else if (token.type === "code") {
      codes.push({
        raw: token.raw,
        lang: token.lang ?? "",
        text: token.text ?? "",
      });
    } else if (token.type === "list_item") {
      const assertion = assertionRecord(token);
      if (assertion !== null) assertionItems.push(assertion);
    }
  });

  const topLevelAssertionItems = [];
  for (const token of tokens) {
    if (token.type !== "list") continue;
    for (const item of token.items ?? []) {
      const assertion = assertionRecord(item);
      if (assertion !== null) topLevelAssertionItems.push(assertion);
    }
  }

  process.stdout.write(JSON.stringify({
    parser: "marked@17.0.5",
    headings,
    tables,
    html,
    codes,
    assertionItems,
    topLevelAssertionItems,
    topLevel: tokens.map(topLevelRecord),
  }));
} catch (error) {
  process.stderr.write(`${error?.stack ?? error}\n`);
  process.exitCode = 1;
}
