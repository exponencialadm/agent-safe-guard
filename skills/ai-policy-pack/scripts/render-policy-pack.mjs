#!/usr/bin/env node
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

function value(name, fallback = "") {
  const idx = process.argv.indexOf(name);
  return idx === -1 ? fallback : process.argv[idx + 1];
}

const dir = dirname(fileURLToPath(import.meta.url));
const pack = JSON.parse(readFileSync(join(dir, "..", "references", "policy-pack.json"), "utf8"));
const format = value("--format", "md");

let out;
if (format === "json") {
  out = `${JSON.stringify(pack, null, 2)}\n`;
} else {
  out = `# AI-Agent Governance Policy Pack\n\n`;
  for (const control of pack.controls) {
    out += `## ${control.id} — ${control.title}\n\n`;
    out += `${control.objective}\n\n`;
    out += `Owner: ${control.owner}\n\n`;
    out += `Evidence: ${control.evidence.join(", ")}\n\n`;
    out += `ISO 27001: ${control.iso27001.join(", ")}\n\n`;
    out += `ISO 42001: ${control.iso42001.join(", ")}\n\n`;
  }
}

const output = value("--output");
if (output) writeFileSync(output, out);
else process.stdout.write(out);
