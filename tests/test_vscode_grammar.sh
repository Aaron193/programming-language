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
  return collectCaptureRanges(line, "entity.name.type.mog");
}

function collectCaptureRanges(line, scopeName) {
  const ranges = [];
  for (const pattern of grammar.repository["type-context-expression"].patterns) {
    if (!pattern.match) {
      continue;
    }

    const regex = compileMatch(pattern);
    let match;
    while ((match = regex.exec(line)) !== null) {
      if (pattern.name === scopeName) {
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
        if (capture.name !== scopeName) {
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

function collectParameterCaptureRanges(line) {
  return collectCaptureRanges(line, "variable.parameter.mog");
}

function collectConstantCaptureRanges(line) {
  return collectCaptureRanges(line, "variable.other.constant.mog");
}

function collectReadwriteCaptureRanges(line) {
  return collectCaptureRanges(line, "variable.other.readwrite.mog");
}

function typeRangesForText(line, text) {
  const target = line.indexOf(text);
  requireCondition(target >= 0, `test fixture is missing '${text}'`);
  const end = target + text.length;
  return collectTypeCaptureRanges(line).filter(
    (range) => !(range.end <= target || range.start >= end)
  );
}

function captureRangesForText(line, text, ranges) {
  const target = line.indexOf(text);
  requireCondition(target >= 0, `test fixture is missing '${text}'`);
  const end = target + text.length;
  return ranges.filter((range) => !(range.end <= target || range.start >= end));
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
  hasPattern("\\b(var)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+([A-Za-z_][A-Za-z0-9_]*)(?!\\s*\\.)\\b") &&
    hasPattern("\\b(const)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+([A-Za-z_][A-Za-z0-9_]*)(?!\\s*\\.)\\b"),
  "grammar should color explicit var and const declaration types"
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
const postfixCastLine = "    var pipeX i64 = floor(pipe.x) as i64";
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
requireCondition(
  postfixCastLine.includes(") as i64"),
  "postfix cast fixture should still exercise the ') as i64' form"
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
const returnTypePattern = grammar.repository["type-context-expression"].patterns.find(
  (pattern) =>
    pattern.captures?.["1"]?.name === "punctuation.section.group.end.mog" &&
    pattern.captures?.["2"]?.name === "entity.name.type.mog"
);
requireCondition(
  returnTypePattern !== undefined,
  "grammar should keep the post-parameter return-type rule"
);
const returnTypeRegex = new RegExp(returnTypePattern.match, "gd");
requireCondition(
  !returnTypeRegex.test(postfixCastLine),
  "post-parameter return-type rule should not swallow ') as' cast syntax"
);
requireCondition(
  typeRangesForText(postfixCastLine, "i64").some((range) => range.text === "i64"),
  "postfix cast target types should still be colored"
);
requireCondition(
  !typeRangesForText(postfixCastLine, "as").some((range) => range.text === "as"),
  "postfix cast keyword should not be colored as a type"
);
requireCondition(
  captureRangesForText(
    postfixCastLine,
    "pipeX",
    collectReadwriteCaptureRanges(postfixCastLine)
  ).some((range) => range.text === "pipeX"),
  "typed var declarations should keep readwrite variable coloring"
);

const typedConstLine = "    const birdY i64 = floor(state.birdY) as i64";
requireCondition(
  captureRangesForText(
    typedConstLine,
    "birdY",
    collectConstantCaptureRanges(typedConstLine)
  ).some((range) => range.text === "birdY"),
  "typed const declarations should keep constant coloring"
);
requireCondition(
  captureRangesForText(
    typedConstLine,
    "birdY",
    collectReadwriteCaptureRanges(typedConstLine)
  ).length === 0,
  "typed const declarations should not be colored as readwrite variables"
);

const keywordPattern = grammar.patterns.find(
  (pattern) => pattern.name === "keyword.control.mog"
);
requireCondition(
  keywordPattern !== undefined && !new RegExp(keywordPattern.match, "g").test("print"),
  "print should not be highlighted as a keyword"
);
requireCondition(
  new RegExp(keywordPattern.match, "g").test("opaque"),
  "opaque should be highlighted as a keyword"
);
requireCondition(
  new RegExp(keywordPattern.match, "g").test("package"),
  "package should be highlighted as a keyword"
);

const annotationPattern = grammar.patterns.find(
  (pattern) => pattern.name === "storage.modifier.annotation.mog"
);
requireCondition(
  annotationPattern !== undefined &&
    new RegExp(annotationPattern.match, "g").test("@doc") &&
    new RegExp(annotationPattern.match, "g").test("@native_handle"),
  "package api annotations should be highlighted"
);

const packageDeclPattern = grammar.patterns.find(
  (pattern) => pattern.match === "\\b(package)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"
);
requireCondition(
  packageDeclPattern !== undefined,
  "grammar should include the package declaration rule"
);

const opaqueDeclPattern = grammar.patterns.find(
  (pattern) => pattern.match === "\\b(opaque)\\s+(type)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"
);
requireCondition(
  opaqueDeclPattern !== undefined,
  "grammar should include the opaque type declaration rule"
);

const functionPattern = grammar.patterns.find(
  (pattern) => pattern.name === "entity.name.function.mog"
);
requireCondition(
  functionPattern !== undefined && new RegExp(functionPattern.match, "g").test("print("),
  "print should still be highlighted by the function-call rule"
);

const functionDeclPattern = grammar.patterns.find(
  (pattern) => pattern.match === "\\b(fn)\\s+([A-Za-z_][A-Za-z0-9_]*)\\b"
);
requireCondition(
  functionDeclPattern !== undefined,
  "grammar should include the function declaration rule"
);

const rawHoverSignature =
  "fn init(id usize, name str, position Vec2, velocity Vec2, viewport Vec2) void";
const rawHoverDeclMatch = new RegExp(functionDeclPattern.match, "d").exec(rawHoverSignature);
requireCondition(
  rawHoverDeclMatch !== null &&
    rawHoverDeclMatch[1] === "fn" &&
    rawHoverDeclMatch[2] === "init",
  "hover-style signatures should still recognize fn and the function name"
);
requireCondition(
  collectParameterCaptureRanges(rawHoverSignature).filter((range) => range.text === "id").length === 1,
  "hover-style signatures should color the first parameter name"
);
requireCondition(
  collectParameterCaptureRanges(rawHoverSignature).filter((range) => range.text === "name").length === 1,
  "hover-style signatures should color later parameter names"
);
requireCondition(
  collectParameterCaptureRanges(rawHoverSignature).filter((range) => range.text === "position").length === 1,
  "hover-style signatures should color custom-typed parameter names"
);

const paramLine = "fn updateScore(state GameState) void {";
requireCondition(
  typeRangesForText(paramLine, "GameState").some(
    (range) => range.text === "GameState"
  ),
  "function parameter types should still be colored"
);
requireCondition(
  collectParameterCaptureRanges(paramLine).some((range) => range.text === "state"),
  "function parameter names should be colored separately from types"
);
requireCondition(
  typeRangesForText(paramLine, "void").some((range) => range.text === "void"),
  "function return types should still be colored"
);

const qualifiedParamLine =
  "fn renderPipes(win window.Window, pipes Array<Pipe>) counter.Counter {";
requireCondition(
  collectParameterCaptureRanges(qualifiedParamLine).some(
    (range) => range.text === "win"
  ),
  "qualified parameter types should still color the parameter name"
);
requireCondition(
  collectParameterCaptureRanges(qualifiedParamLine).some(
    (range) => range.text === "pipes"
  ),
  "generic parameter types should still color the parameter name"
);
requireCondition(
  typeRangesForText(qualifiedParamLine, "window.Window").some(
    (range) => range.text === "window.Window"
  ),
  "qualified parameter types should color the full qualified type"
);
requireCondition(
  typeRangesForText(qualifiedParamLine, "counter.Counter").some(
    (range) => range.text === "counter.Counter"
  ),
  "qualified return types should color the full qualified type"
);

const functionTypedParamLine = "fn each(callback fn(Player) void) void {";
requireCondition(
  collectParameterCaptureRanges(functionTypedParamLine).some(
    (range) => range.text === "callback"
  ),
  "function-typed parameters should color the parameter name"
);
requireCondition(
  collectTypeCaptureRanges(functionTypedParamLine).filter(
    (range) => range.text === "fn"
  ).length === 1,
  "function-typed parameters should color the fn type marker"
);

const standaloneParameterHover = "player Player";
requireCondition(
  collectParameterCaptureRanges(standaloneParameterHover).some(
    (range) => range.text === "player"
  ),
  "standalone parameter hover lines should color the parameter name"
);
requireCondition(
  typeRangesForText(standaloneParameterHover, "Player").some(
    (range) => range.text === "Player"
  ),
  "standalone parameter hover lines should color the parameter type"
);

const standaloneFunctionParameterHover = "callback fn(Player) void";
requireCondition(
  collectParameterCaptureRanges(standaloneFunctionParameterHover).some(
    (range) => range.text === "callback"
  ),
  "standalone function-typed parameter hover should color the parameter name"
);

const qualifiedConstLine = "const evt window.Event = window.pollEvent(win)";
requireCondition(
  captureRangesForText(
    qualifiedConstLine,
    "evt",
    collectConstantCaptureRanges(qualifiedConstLine)
  ).some((range) => range.text === "evt"),
  "qualified typed const declarations should color the const name"
);
requireCondition(
  typeRangesForText(qualifiedConstLine, "window.Event").some(
    (range) => range.text === "window.Event"
  ),
  "qualified typed const declarations should color the full qualified type"
);

requireCondition(
  typeRangesForText(rawHoverSignature, "usize").some((range) => range.text === "usize"),
  "hover-style signatures should color built-in parameter types"
);
requireCondition(
  typeRangesForText(rawHoverSignature, "str").some((range) => range.text === "str"),
  "hover-style signatures should color subsequent parameter types"
);
requireCondition(
  collectTypeCaptureRanges(rawHoverSignature).filter((range) => range.text === "Vec2").length === 3,
  "hover-style signatures should color custom parameter types"
);
requireCondition(
  typeRangesForText(rawHoverSignature, "void").some((range) => range.text === "void"),
  "hover-style signatures should color return types"
);

const genericCollectionPattern = grammar.repository["type-angle-expression"].patterns.find(
  (pattern) => pattern.name === "meta.type.generic.collection.mog"
);
const qualifiedGenericTypePattern = genericCollectionPattern?.patterns.find(
  (pattern) =>
    pattern.name === "entity.name.type.mog" &&
    pattern.match === "\\b(?:[A-Za-z_][A-Za-z0-9_]*\\.)+[A-Za-z_][A-Za-z0-9_]*\\b"
);
requireCondition(
  qualifiedGenericTypePattern !== undefined &&
    new RegExp(qualifiedGenericTypePattern.match, "g").test("window.Event"),
  "generic type contexts should support qualified type names"
);

console.log("[PASS] VS Code grammar regression checks");
NODE
