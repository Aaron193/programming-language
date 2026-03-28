#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

node - "$PROJECT_ROOT" <<'NODE'
const fs = require("fs");
const path = require("path");

const projectRoot = process.argv[2];
const manifestPath = path.join(
  projectRoot,
  "tooling",
  "vscode-mog",
  "package.json"
);
const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));

function fail(message) {
  console.error(`[FAIL] ${message}`);
  process.exit(1);
}

function requireCondition(condition, message) {
  if (!condition) {
    fail(message);
  }
}

const languages = manifest.contributes?.languages;
requireCondition(Array.isArray(languages), "manifest should contribute languages");

const mogLanguage = languages.find((language) => language.id === "mog");
requireCondition(mogLanguage !== undefined, "manifest should contribute the mog language");
requireCondition(
  Array.isArray(mogLanguage.extensions) && mogLanguage.extensions.includes(".mog"),
  "mog language should still register the .mog extension"
);

const icon = mogLanguage.icon;
requireCondition(icon && typeof icon === "object", "mog language should declare an icon");
requireCondition(
  icon.light === "./images/fileicons/icon.png",
  "mog language should use the expected light icon path"
);
requireCondition(
  icon.dark === "./images/fileicons/icon.png",
  "mog language should use the expected dark icon path"
);

for (const variant of ["light", "dark"]) {
  const iconPath = path.join(projectRoot, "tooling", "vscode-mog", icon[variant].slice(2));
  requireCondition(fs.existsSync(iconPath), `${variant} icon should exist at ${icon[variant]}`);
}

console.log("[PASS] VS Code manifest registers the Mog file icon");
NODE
