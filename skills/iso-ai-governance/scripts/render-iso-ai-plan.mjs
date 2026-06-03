#!/usr/bin/env node
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

function value(name, fallback = "") {
  const idx = process.argv.indexOf(name);
  return idx === -1 ? fallback : process.argv[idx + 1];
}

const dir = dirname(fileURLToPath(import.meta.url));
const controlMap = JSON.parse(readFileSync(join(dir, "..", "references", "control-map.json"), "utf8"));
const format = value("--format", "md");

let out;
if (format === "json") {
  out = `${JSON.stringify(controlMap, null, 2)}\n`;
} else {
  out = "# ISO + AI Governance Plan\n\n";
  out += "Use this plan to connect AI-agent visibility, policy, enforcement, vulnerability management, evidence, and incident response to ISO 27001 and ISO 42001.\n\n";
  for (const theme of controlMap.themes) {
    out += `## ${theme.title}\n\n`;
    out += `Question: ${theme.question}\n\n`;
    out += `Skills: ${theme.skills.map((name) => `$${name}`).join(", ")}\n\n`;
    out += `ISO 27001: ${theme.iso27001.join(", ")}\n\n`;
    out += `ISO 42001: ${theme.iso42001.join(", ")}\n\n`;
    out += `Evidence: ${theme.evidence.join(", ")}\n\n`;
  }
  out += "## First 30 Days\n\n";
  out += "1. Run agent inventory and extension risk audit.\n";
  out += "2. Approve or quarantine high-risk agents, skills, MCPs, and extensions.\n";
  out += "3. Render the AI policy pack and assign owners.\n";
  out += "4. Install Agent Safe Guard for enforcement on pilot workstations.\n";
  out += "5. Package evidence envelopes for the initial ISO control review.\n";
}

const output = value("--output");
if (output) writeFileSync(output, out);
else process.stdout.write(out);
