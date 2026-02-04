#!/usr/bin/env node
"use strict";

import {execFile} from "node:child_process";
import {promisify} from "node:util";
import {fileURLToPath} from "node:url";
import path from "node:path";

const execFileAsync = promisify(execFile);
const __filename = fileURLToPath(import.meta.url);
const ROOT = path.resolve(path.dirname(__filename), "..");

function parseArgs(argv) {
  const args = {top: 15, idf: "idf.py"};
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg === "--top") {
      const next = argv[++i];
      if (!next) {
        throw new Error("--top requires a value");
      }
      args.top = Math.max(1, Number.parseInt(next, 10));
    } else if (arg === "--idf") {
      const next = argv[++i];
      if (!next) {
        throw new Error("--idf requires a value");
      }
      args.idf = next;
    } else if (arg === "-h" || arg === "--help") {
      printHelp();
      process.exit(0);
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  return args;
}

function printHelp() {
  console.log(`Usage: scripts/report-iram-usage.mjs [--top N] [--idf path]\n`);
  console.log("Options:");
  console.log("  --top N     Number of rows to display per table (default 15)");
  console.log("  --idf PATH  idf.py executable to invoke (default 'idf.py')");
}

function humanBytes(value) {
  const units = ["B", "KiB", "MiB", "GiB"];
  let amount = Number(value);
  for (const unit of units) {
    if (amount < 1024 || unit === units.at(-1)) {
      return unit === "B" ? `${Math.round(amount)}B` : `${amount.toFixed(1)}${unit}`;
    }
    amount /= 1024;
  }
  return `${value}B`;
}

function parseJsonBlock(stdout, cmd) {
  const start = stdout.indexOf("{");
  const end = stdout.lastIndexOf("}");
  if (start === -1 || end === -1) {
    throw new Error(`Command ${cmd.join(" ")} did not emit JSON. stdout:\n${stdout}`);
  }
  return JSON.parse(stdout.slice(start, end + 1));
}

async function runSize(idf, target) {
  const cmd = [idf, target, "--format", "json2"];
  try {
    const {stdout} = await execFileAsync(cmd[0], cmd.slice(1), {
      cwd: ROOT,
      env: process.env,
      maxBuffer: 10 * 1024 * 1024,
    });
    return parseJsonBlock(stdout, cmd);
  } catch (err) {
    const stderr = err?.stderr ? `\nstderr:\n${err.stderr}` : "";
    const stdout = err?.stdout ? `\nstdout:\n${err.stdout}` : "";
    throw new Error(`Command ${cmd.join(" ")} failed (${err?.code ?? "unknown"})${stdout}${stderr}`);
  }
}

function gatherDiramEntries(report) {
  const entries = [];
  let total = 0;
  for (const [name, meta] of Object.entries(report)) {
    const diram = meta?.memory_types?.DIRAM;
    if (!diram) continue;
    const size = Number(diram.size ?? 0);
    if (size <= 0) continue;
    total += size;
    entries.push({name, size, sections: diram.sections ?? {}});
  }
  entries.sort((a, b) => b.size - a.size);
  return {total, entries};
}

function describeSections(sections) {
  const order = [
    [".text", "text"],
    [".data", "data"],
    [".bss", "bss"],
    [".rodata", "ro"],
    [".init_array", "init"],
    [".appdesc", "app"],
    [".rtc_reserved", "rtc"],
  ];
  const parts = [];
  for (const [key, label] of order) {
    const info = sections?.[key];
    if (!info) continue;
    const size = Number(info.size ?? 0);
    if (size <= 0) continue;
    parts.push(`${label} ${humanBytes(size)}`);
  }
  return parts.length ? parts.join(" / ") : "-";
}

function renderTable(title, total, entries, top) {
  const display = entries.slice(0, top);
  const nameWidth = Math.max(10, ...display.map((entry) => entry.name.length));
  console.log(`${title} (DIRAM total ${humanBytes(total)})`);
  console.log(`${"#".padStart(3)}  ${"Name".padEnd(nameWidth)}  ${"DIRAM".padStart(8)}  Sections`);
  display.forEach((entry, index) => {
    const row = `${String(index + 1).padStart(3)}.  ${entry.name.padEnd(nameWidth)}  ${humanBytes(entry.size).padStart(8)}  ${describeSections(entry.sections)}`;
    console.log(row);
  });
  if (entries.length > display.length) {
    const remaining = entries.slice(display.length).reduce((sum, entry) => sum + entry.size, 0);
    console.log(`     ... ${humanBytes(remaining)} across ${entries.length - display.length} more entries`);
  }
  console.log();
}

async function main() {
  let args;
  try {
    args = parseArgs(process.argv.slice(2));
  } catch (err) {
    console.error(err.message);
    printHelp();
    process.exit(1);
  }

  const components = await runSize(args.idf, "size-components");
  const files = await runSize(args.idf, "size-files");

  const compData = gatherDiramEntries(components);
  const fileData = gatherDiramEntries(files);

  if (!compData.entries.length && !fileData.entries.length) {
    console.log("No DIRAM usage found in reports.");
    return;
  }

  console.log();
  if (compData.entries.length) {
    renderTable("Components", compData.total, compData.entries, args.top);
  }
  if (fileData.entries.length) {
    renderTable("Object Files", fileData.total, fileData.entries, args.top);
  }
}

main().catch((err) => {
  console.error(err.message ?? err);
  process.exit(1);
});
