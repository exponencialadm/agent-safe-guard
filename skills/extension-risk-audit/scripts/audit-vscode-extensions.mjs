#!/usr/bin/env node
import { existsSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { basename, join, resolve } from "node:path";
import os from "node:os";

function argValue(name, fallback) {
  const idx = process.argv.indexOf(name);
  return idx === -1 ? fallback : process.argv[idx + 1];
}

function safeJson(path) {
  try {
    return JSON.parse(readFileSync(path, "utf8"));
  } catch {
    return null;
  }
}

function listDirs(path) {
  try {
    return readdirSync(path, { withFileTypes: true }).filter((d) => d.isDirectory()).map((d) => join(path, d.name));
  } catch {
    return [];
  }
}

function riskFor(pkg) {
  const haystack = [
    pkg.name,
    pkg.displayName,
    pkg.description,
    ...(pkg.keywords || []),
    ...(pkg.categories || [])
  ].filter(Boolean).join(" ").toLowerCase();
  const activation = Array.isArray(pkg.activationEvents) ? pkg.activationEvents : [];
  const scripts = pkg.scripts && typeof pkg.scripts === "object" ? Object.keys(pkg.scripts) : [];
  const commandCount = Array.isArray(pkg.contributes?.commands) ? pkg.contributes.commands.length : 0;
  const flags = [];
  let score = 0;
  if (/\b(ai|agent|copilot|chatgpt|openai|claude|cursor|cody|codeium|llm|mcp)\b/.test(haystack)) { flags.push("ai_or_agent_terms"); score += 25; }
  if (activation.includes("*")) { flags.push("wildcard_activation"); score += 25; }
  if (activation.some((item) => item === "onStartupFinished" || item.startsWith("workspaceContains:"))) { flags.push("broad_activation"); score += 15; }
  if (commandCount >= 10) { flags.push("many_commands"); score += 10; }
  if (scripts.some((name) => /^(preinstall|install|postinstall|prepare)$/.test(name))) { flags.push("install_script_declared"); score += 20; }
  if (!pkg.publisher) { flags.push("missing_publisher"); score += 10; }
  return { score: Math.min(score, 100), level: score >= 60 ? "high" : score >= 30 ? "medium" : "low", flags };
}

const dirs = [];
const explicitDir = argValue("--dir", "");
if (explicitDir) dirs.push(resolve(explicitDir));
else {
  const home = os.homedir();
  dirs.push(join(home, ".vscode", "extensions"), join(home, ".vscode-server", "extensions"), join(home, ".cursor", "extensions"), join(home, ".cursor-server", "extensions"), join(home, ".windsurf", "extensions"));
}

const findings = [];
for (const root of dirs.filter(existsSync)) {
  for (const dir of listDirs(root)) {
    const pkg = safeJson(join(dir, "package.json"));
    if (!pkg) continue;
    const risk = riskFor(pkg);
    findings.push({
      id: pkg.publisher && pkg.name ? `${pkg.publisher}.${pkg.name}` : basename(dir),
      name: pkg.name || basename(dir),
      display_name: pkg.displayName || null,
      publisher: pkg.publisher || null,
      version: pkg.version || null,
      categories: pkg.categories || [],
      activation_events: Array.isArray(pkg.activationEvents) ? pkg.activationEvents : [],
      command_count: Array.isArray(pkg.contributes?.commands) ? pkg.contributes.commands.length : 0,
      declared_scripts: pkg.scripts && typeof pkg.scripts === "object" ? Object.keys(pkg.scripts) : [],
      path: dir,
      risk
    });
  }
}
findings.sort((a, b) => b.risk.score - a.risk.score || a.id.localeCompare(b.id));

if (argValue("--format", "json") === "table") {
  for (const item of findings) {
    console.log(`${String(item.risk.score).padStart(3)} ${item.risk.level.padEnd(6)} ${item.id} ${item.version || ""} ${item.risk.flags.join(",")}`);
  }
} else {
  const doc = { schema: "https://exponencialadm.net/schemas/extension-risk-audit.v1.json", collected_at: new Date().toISOString(), findings };
  const output = argValue("--output", "");
  const json = `${JSON.stringify(doc, null, 2)}\n`;
  if (output) writeFileSync(output, json);
  else process.stdout.write(json);
}
