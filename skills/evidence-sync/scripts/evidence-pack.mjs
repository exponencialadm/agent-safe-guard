#!/usr/bin/env node
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

function values(name) {
  const out = [];
  for (let i = 0; i < process.argv.length; i++) if (process.argv[i] === name && process.argv[i + 1]) out.push(process.argv[i + 1]);
  return out;
}

function value(name, fallback = "") {
  return values(name)[0] || fallback;
}

const input = value("--input");
if (!input) {
  console.error("usage: evidence-pack.mjs --input FILE --control CONTROL [--control CONTROL] --source NAME [--output FILE]");
  process.exit(2);
}

const payloadText = readFileSync(input, "utf8");
let payload;
try {
  payload = JSON.parse(payloadText);
} catch {
  payload = { text: payloadText };
}

const source = value("--source", "manual");
const controls = values("--control");
const classification = value("--classification", "internal");
const createdAt = new Date().toISOString();
const hashInput = JSON.stringify({ source, controls, payload });
const evidenceId = createHash("sha256").update(hashInput).digest("hex").slice(0, 24);
const envelope = {
  schema: "https://exponencialadm.net/schemas/evidence-envelope.v1.json",
  evidence_id: evidenceId,
  created_at: createdAt,
  source,
  control_refs: controls,
  classification,
  payload,
  safety: {
    generated_locally: true,
    secrets_expected: false,
    send_requires_explicit_user_approval: true
  }
};

const json = `${JSON.stringify(envelope, null, 2)}\n`;
const output = value("--output");
if (output) writeFileSync(output, json);
else process.stdout.write(json);
