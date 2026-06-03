#!/usr/bin/env node
import { spawnSync } from "node:child_process";
import { existsSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";

function value(name, fallback = "") {
  const idx = process.argv.indexOf(name);
  return idx === -1 ? fallback : process.argv[idx + 1];
}

const cwd = resolve(value("--cwd", process.cwd()));
const output = value("--output");
const startedAt = new Date().toISOString();
const hasPackage = existsSync(resolve(cwd, "package.json"));
const hasLock = existsSync(resolve(cwd, "package-lock.json")) || existsSync(resolve(cwd, "npm-shrinkwrap.json"));

let audit = null;
let auditExitCode = null;
let stderr = "";
if (hasPackage) {
  const result = spawnSync("npm", ["audit", "--json"], { cwd, encoding: "utf8" });
  auditExitCode = result.status;
  stderr = result.stderr || "";
  try {
    audit = JSON.parse(result.stdout || "{}");
  } catch {
    audit = { parse_error: true, stdout_bytes: Buffer.byteLength(result.stdout || "", "utf8") };
  }
}

const doc = {
  schema: "https://exponencialadm.net/schemas/npm-audit-evidence.v1.json",
  collected_at: startedAt,
  cwd,
  package_json_present: hasPackage,
  lockfile_present: hasLock,
  npm_audit_exit_code: auditExitCode,
  npm_audit_stderr_present: Boolean(stderr.trim()),
  summary: audit?.metadata?.vulnerabilities || null,
  audit,
  safety: {
    command: "npm audit --json",
    modified_files: false,
    secrets_expected: false
  }
};

const json = `${JSON.stringify(doc, null, 2)}\n`;
if (output) writeFileSync(output, json);
else process.stdout.write(json);
