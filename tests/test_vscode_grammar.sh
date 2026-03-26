#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

node - "$PROJECT_ROOT" <<'NODE'
const fs = require("fs");
const path = require("path");

const projectRoot = process.argv[2];
const grammarPath = path.join(
  projectRoot,
  "tooling",
  "vscode-mog",
  "syntaxes",
  "mog.tmLanguage.json"
);
const languageConfigPath = path.join(
  projectRoot,
  "tooling",
  "vscode-mog",
  "language-configuration.json"
);
const grammar = JSON.parse(fs.readFileSync(grammarPath, "utf8"));
const languageConfig = JSON.parse(fs.readFileSync(languageConfigPath, "utf8"));

function fail(message) {
  console.error(`[FAIL] ${message}`);
  process.exit(1);
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

function compileMatch(pattern) {
  return new RegExp(pattern.match, "gd");
}

function collectTypeCaptureRanges(line) {
  const ranges = [];
  for (const pattern of grammar.repository["type-context-expression"].patterns) {
    if (!pattern.match) {
      continue;
    }

    const regex = compileMatch(pattern);
    let match;
    while ((match = regex.exec(line)) !== null) {
      if (pattern.name === "entity.name.type.mog") {
        const indices = match.indices[0];
        if (indices) {
          ranges.push({
            start: indices[0],
            end: indices[1],
            text: match[0]
          });
        }
      }

      if (!pattern.captures) {
        if (match[0].length === 0) {
          regex.lastIndex += 1;
        }
        continue;
      }

      for (const [groupIndex, capture] of Object.entries(pattern.captures)) {
        if (capture.name !== "entity.name.type.mog") {
          continue;
        }

        const index = Number(groupIndex);
        const captureText = match[index];
        if (captureText === undefined) {
          continue;
        }

        const indices = match.indices[index];
        if (!indices) {
          continue;
        }

        ranges.push({
          start: indices[0],
          end: indices[1],
          text: captureText
        });
      }

      if (match[0].length === 0) {
        regex.lastIndex += 1;
      }
    }
  }
  return ranges;
}

function typeRangesForText(line, text) {
  const target = line.indexOf(text);
  requireCondition(target >= 0, `test fixture is missing '${text}'`);
  const end = target + text.length;
  return collectTypeCaptureRanges(line).filter(
    (range) => !(range.end <= target || range.start >= end)
  );
}

function hasPattern(patternText) {
  return grammar.repository["type-context-expression"].patterns.some(
    (pattern) => pattern.match === patternText
  );
}

requireCondition(
  !grammar.patterns.some(
    (pattern) =>
      pattern.name === "entity.name.type.mog" &&
      pattern.match === "\\b[A-Z][A-Za-z0-9_]*\\b"
  ),
  "grammar should not classify every uppercase identifier as a type"
);

requireCondition(
  hasPattern("\\b(var|const)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"),
  "grammar should color explicit variable declaration types"
);

const blockCommentPattern = grammar.patterns.find(
  (pattern) => pattern.name === "comment.block.mog"
);
requireCondition(
  blockCommentPattern !== undefined &&
    blockCommentPattern.begin === "/\\*" &&
    blockCommentPattern.end === "\\*/",
  "grammar should include a block comment rule"
);
requireCondition(
  languageConfig.comments?.blockComment?.[0] === "/*" &&
    languageConfig.comments?.blockComment?.[1] === "*/",
  "language configuration should advertise block comments"
);

const reproLine =
  "        if (pipe.passed == false && (pipe.x + (C.PIPE_WIDTH as f64)) < (C.BIRD_X as f64)) {";
requireCondition(
  typeRangesForText(reproLine, "PIPE_WIDTH").length === 0,
  "PIPE_WIDTH should not be colored as a type inside the long single-line expression"
);
requireCondition(
  typeRangesForText(reproLine, "BIRD_X").length === 0,
  "BIRD_X should not be colored as a type inside the long single-line expression"
);

const propertyRegex = new RegExp(
  grammar.patterns.find((pattern) => pattern.name === "variable.other.property.mog").match,
  "g"
);
const propertyMatches = [...reproLine.matchAll(propertyRegex)].map((match) => match[0]);
requireCondition(
  propertyMatches.includes("passed") && propertyMatches.includes("x"),
  "lowercase member accesses should still be colored as properties"
);
requireCondition(
  !propertyMatches.includes("PIPE_WIDTH") && !propertyMatches.includes("BIRD_X"),
  "uppercase names after '.' should not be treated as properties"
);

const castLine = "    const birdLeft f64 = C.BIRD_X as f64";
requireCondition(
  grammar.patterns.some(
    (pattern) =>
      pattern.name === "entity.name.type.mog" &&
      pattern.match === "\\b(?:i8|i16|i32|i64|u8|u16|u32|u64|u|usize|f32|f64|bool|str|void|null|fn)\\b"
  ),
  "built-in types should still be highlighted without a cast-specific rule"
);
requireCondition(
  castLine.includes(" as f64"),
  "cast fixture should still exercise the 'as f64' form"
);
const parameterPattern = grammar.repository["type-context-expression"].patterns.find(
  (pattern) => pattern.match.includes("(?=\\s*(?:,|\\)|:))")
);
requireCondition(
  parameterPattern !== undefined,
  "grammar should keep the parameter/type-context rule"
);
requireCondition(
  !new RegExp(parameterPattern.match, "g").test("as f64)"),
  "parameter/type-context rule should not swallow cast syntax"
);

const keywordPattern = grammar.patterns.find(
  (pattern) => pattern.name === "keyword.control.mog"
);
requireCondition(
  keywordPattern !== undefined && !new RegExp(keywordPattern.match, "g").test("print"),
  "print should not be highlighted as a keyword"
);

const functionPattern = grammar.patterns.find(
  (pattern) => pattern.name === "entity.name.function.mog"
);
requireCondition(
  functionPattern !== undefined && new RegExp(functionPattern.match, "g").test("print("),
  "print should still be highlighted by the function-call rule"
);

const paramLine = "fn updateScore(state GameState) void {";
requireCondition(
  typeRangesForText(paramLine, "GameState").some(
    (range) => range.text === "GameState"
  ),
  "function parameter types should still be colored"
);
requireCondition(
  typeRangesForText(paramLine, "void").some((range) => range.text === "void"),
  "function return types should still be colored"
);

console.log("[PASS] VS Code grammar regression checks");
NODE
