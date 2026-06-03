#!/usr/bin/env node
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, readdirSync, statSync, cpSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import os from "node:os";

const __dirname = dirname(fileURLToPath(import.meta.url));
const root = resolve(__dirname, "..");
const pkg = JSON.parse(readFileSync(join(root, "package.json"), "utf8"));
const repoUrl = "https://github.com/exponencialadm/agent-safe-guard.git";

function usage() {
  console.log(`Agent Safe Guard ${pkg.version}

Usage:
  agent-safe-guard --help
  agent-safe-guard skills --list
  agent-safe-guard skills [--dest DIR] [--force]
  agent-safe-guard install [--dir DIR] [--ref REF] [--no-systemd]

Commands:
  skills   Install the bundled Codex skill pack. Default destination:
           $CODEX_HOME/skills or ~/.codex/skills.
  install  Clone the public repository and run the native source installer.
           Default ref: v${pkg.version}.

The npm package has no postinstall hook. Nothing is installed until one of
the commands above is run explicitly.`);
}

function argValue(args, name, fallback) {
  const idx = args.indexOf(name);
  if (idx === -1) return fallback;
  if (!args[idx + 1]) throw new Error(`${name} requires a value`);
  return args[idx + 1];
}

function run(cmd, args, cwd) {
  execFileSync(cmd, args, { cwd, stdio: "inherit" });
}

function listSkills() {
  const skillsDir = join(root, "skills");
  return readdirSync(skillsDir)
    .filter((name) => existsSync(join(skillsDir, name, "SKILL.md")))
    .sort();
}

function installSkills(args) {
  if (args.includes("--list")) {
    for (const name of listSkills()) console.log(name);
    return;
  }

  const dest = resolve(argValue(args, "--dest", process.env.CODEX_HOME ? join(process.env.CODEX_HOME, "skills") : join(os.homedir(), ".codex", "skills")));
  const force = args.includes("--force");
  mkdirSync(dest, { recursive: true });

  for (const name of listSkills()) {
    const src = join(root, "skills", name);
    const target = join(dest, name);
    if (existsSync(target) && !force) {
      console.log(`skip ${name}: already exists (${target}); pass --force to update`);
      continue;
    }
    cpSync(src, target, { recursive: true, force: true });
    console.log(`installed ${name} -> ${target}`);
  }
}

function installNative(args) {
  const targetDir = resolve(argValue(args, "--dir", join(os.homedir(), ".cache", "agent-safe-guard", "source")));
  const ref = argValue(args, "--ref", `v${pkg.version}`);
  const noSystemd = args.includes("--no-systemd");
  mkdirSync(dirname(targetDir), { recursive: true });

  if (!existsSync(targetDir)) {
    run("git", ["clone", "--recurse-submodules", "--branch", ref, repoUrl, targetDir], process.cwd());
  } else {
    if (!existsSync(join(targetDir, ".git"))) {
      throw new Error(`${targetDir} exists but is not a git repository`);
    }
    run("git", ["fetch", "--tags", "origin"], targetDir);
    run("git", ["checkout", ref], targetDir);
    if (ref === "main") run("git", ["pull", "--ff-only", "origin", "main"], targetDir);
    run("git", ["submodule", "update", "--init", "--recursive"], targetDir);
  }

  const installer = join(targetDir, "scripts", "install-from-source.sh");
  const installArgs = noSystemd ? ["--no-enable-systemd-user"] : [];
  if (!statSync(installer).mode) throw new Error(`installer not found: ${installer}`);
  run(installer, installArgs, targetDir);
}

try {
  const [command, ...args] = process.argv.slice(2);
  if (!command || command === "--help" || command === "-h") {
    usage();
  } else if (command === "skills") {
    installSkills(args);
  } else if (command === "install") {
    installNative(args);
  } else {
    throw new Error(`unknown command: ${command}`);
  }
} catch (err) {
  console.error(`agent-safe-guard: ${err.message}`);
  process.exitCode = 1;
}
