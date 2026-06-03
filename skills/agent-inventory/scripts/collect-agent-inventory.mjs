#!/usr/bin/env node
import { execFileSync } from "node:child_process";
import { existsSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
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

function commandPath(cmd) {
  try {
    return execFileSync("sh", ["-lc", `command -v ${cmd}`], { encoding: "utf8" }).trim() || null;
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

function scanSkills(root) {
  const found = [];
  for (const dir of listDirs(root)) {
    const skill = join(dir, "SKILL.md");
    if (!existsSync(skill)) continue;
    const text = readFileSync(skill, "utf8").slice(0, 3000);
    const name = text.match(/^name:\s*([^\n]+)/m)?.[1]?.trim() || basename(dir);
    const description = text.match(/^description:\s*([^\n]+)/m)?.[1]?.trim() || "";
    found.push({ name, description, path: dir });
  }
  return found;
}

function scanExtensions(root) {
  const found = [];
  for (const dir of listDirs(root)) {
    const pkg = safeJson(join(dir, "package.json"));
    if (!pkg) continue;
    found.push({
      id: pkg.publisher && pkg.name ? `${pkg.publisher}.${pkg.name}` : basename(dir),
      name: pkg.name || basename(dir),
      display_name: pkg.displayName || null,
      publisher: pkg.publisher || null,
      version: pkg.version || null,
      categories: pkg.categories || [],
      activation_events: Array.isArray(pkg.activationEvents) ? pkg.activationEvents : [],
      command_count: Array.isArray(pkg.contributes?.commands) ? pkg.contributes.commands.length : 0,
      extension_kind: pkg.extensionKind || null,
      path: dir
    });
  }
  return found;
}

const home = os.homedir();
const root = resolve(argValue("--root", process.cwd()));
const output = argValue("--output", "");
const skillRoots = [
  process.env.CODEX_HOME ? join(process.env.CODEX_HOME, "skills") : null,
  join(home, ".codex", "skills"),
  join(home, ".agents", "skills"),
  join(home, ".claude", "skills"),
  join(root, "skills")
].filter(Boolean);
const extensionRoots = [
  join(home, ".vscode", "extensions"),
  join(home, ".vscode-server", "extensions"),
  join(home, ".cursor", "extensions"),
  join(home, ".cursor-server", "extensions"),
  join(home, ".windsurf", "extensions")
];
const mcpCandidates = [
  join(root, ".mcp.json"),
  join(root, "mcp.json"),
  join(home, ".claude", "settings.json"),
  join(home, ".codex", "config.toml"),
  join(home, ".config", "claude", "mcp.json")
];

const doc = {
  schema: "https://exponencialadm.net/schemas/agent-inventory.v1.json",
  collected_at: new Date().toISOString(),
  root,
  agent_tools: ["agent-safe-guard", "asg-cli", "claude", "codex", "opencode", "openclaw", "code", "cursor", "windsurf"].map((name) => ({ name, path: commandPath(name) })).filter((item) => item.path),
  skill_roots: skillRoots.filter(existsSync).map((path) => ({ path, skills: scanSkills(path) })),
  extension_roots: extensionRoots.filter(existsSync).map((path) => ({ path, extensions: scanExtensions(path) })),
  mcp_config_candidates: mcpCandidates.filter(existsSync).map((path) => ({ path, bytes: statSync(path).size })),
  notes: [
    "Collector is read-only and does not read secret files.",
    "MCP config candidates are recorded by path and size only."
  ]
};

const json = `${JSON.stringify(doc, null, 2)}\n`;
if (output) writeFileSync(output, json);
else process.stdout.write(json);
