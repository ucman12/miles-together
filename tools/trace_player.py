"""Research-only observer. No multiplayer, camera edits, input, save edits or actor spawning."""
from __future__ import annotations

import argparse
import hashlib
import json
import threading
from pathlib import Path

import frida
import pefile
from inspect_assets import path_id

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--game", type=Path, required=True)
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--components", action="store_true", help="Observe bounded component table once per actor")
    parser.add_argument("--lookup-asset", help="Research-only lookup of an already loaded actor asset on the game callback thread")
    args = parser.parse_args()
    compatibility = json.loads((ROOT / "sheets/compatibility.json").read_text())["rows"][0]
    game_exe = args.game / compatibility["executable"]
    if hashlib.sha256(game_exe.read_bytes()).hexdigest() != compatibility["sha256"]:
        raise SystemExit("Unsupported executable: no instrumentation was attached")
    hook = next(row for row in json.loads((ROOT / "sheets/native_hooks.json").read_text())["rows"] if row["id"] == "follow_camera_commit")
    pe = pefile.PE(str(game_exe), fast_load=True)
    rva = int(hook["rva"], 16)
    signature = bytes.fromhex(hook["signature"])
    if pe.get_data(rva, len(signature)) != signature:
        raise SystemExit("Static hook signature mismatch: no instrumentation was attached")
    source = r"""
const cfg = __CONFIG__;
const module = Process.getModuleByName(cfg.exe);
const hook = module.base.add(cfg.rva);
const bytes = Array.from(new Uint8Array(hook.readByteArray(cfg.signature.length / 2)))
  .map(v => v.toString(16).padStart(2, '0')).join('');
if (bytes !== cfg.signature) throw new Error('Live hook signature mismatch');
let last = 0;
let calls = 0;
let errors = 0;
let actors = new Set();
let enumerated = new Set();
function cstring(address, maxLength) {
  if (address.isNull()) return '';
  const bytes = new Uint8Array(address.readByteArray(maxLength));
  const end = bytes.indexOf(0);
  if (end < 0) throw new Error('Unterminated bounded string');
  return address.readUtf8String(end);
}
function components(owner) {
  const dense = owner.add(cfg.layout.owner_dense_capacity).readU16();
  const count = dense || owner.add(cfg.layout.owner_sparse_count).readU16();
  if (count < 1 || count > 1024) return {dense, count, rejected:true};
  const table = owner.add(dense ? cfg.layout.owner_dense_entries : cfg.layout.owner_sparse_entries).readPointer();
  if (table.isNull()) return {dense, count, rejected:true};
  const entries = [];
  for (let index=0; index<count; index++) {
    const descriptor = table.add(index*16).readPointer();
    const instance = table.add(index*16+8).readPointer();
    if (descriptor.isNull() || instance.isNull()) continue;
    const entry = {descriptor:descriptor.toString(), instance:instance.toString()};
    try {
      const namePointer = descriptor.add(cfg.layout.component_descriptor_name).readPointer();
      entry.name = cstring(namePointer, 120);
    } catch (error) { entry.nameError = String(error); }
    try {
      const vt = instance.readPointer();
      const relative = vt.sub(module.base);
      let rtti = '';
      if (relative.compare(ptr(0)) >= 0 && relative.compare(ptr(module.size)) < 0) {
        const locator = vt.sub(8).readPointer();
        const typeRva = locator.add(12).readU32();
        rtti = cstring(module.base.add(typeRva+16), 160);
      }
      entry.rtti = rtti;
      entry.vtable = relative.toString();
    } catch (error) { entry.rttiError = String(error); }
    entries.push(entry);
  }
  return {dense, count, entries};
}
const listener = Interceptor.attach(hook, {
  onEnter(args) { this.target = args[1]; calls++; },
  onLeave() {
    const now = Date.now();
    if (now - last < 300) return;
    last = now;
    try {
      const target = this.target;
      if (target.isNull()) return;
      const vt = target.readPointer().sub(module.base).toString();
      if (!cfg.layout.target_vtables.includes(vt)) return;
      const handle = target.add(cfg.layout.target_handle).readPointer();
      if (handle.isNull()) return;
      const actor = handle.readPointer();
      if (actor.isNull() || actor.add(cfg.layout.actor_invalid_flag).readU8() !== 0) return;
      const skeleton = actor.add(cfg.layout.actor_skeleton).readPointer();
      if (skeleton.isNull()) return;
      const xyz = cfg.layout.actor_translation.map(offset => actor.add(offset).readFloat());
      if (!xyz.every(Number.isFinite) || xyz.some(v => Math.abs(v) > 100000)) return;
      actors.add(actor.toString());
      if (cfg.lookup && !cfg.lookup.done) {
        cfg.lookup.done = true;
        const research = cfg.lookup.hook;
        const functionAddress = module.base.add(research.rva);
        const live = Array.from(new Uint8Array(functionAddress.readByteArray(research.signature.length/2)))
          .map(v => v.toString(16).padStart(2,'0')).join('');
        if (live !== research.signature) throw new Error('Research getter signature mismatch');
        const lookup = new NativeFunction(functionAddress, research.return_type, research.arguments);
        const result = lookup(module.base.add(research.context_rva), new UInt64(cfg.lookup.id));
        send({kind:'asset_lookup', path:cfg.lookup.path, id:cfg.lookup.id, result:result.toString(), thread:Process.getCurrentThreadId()});
      }
      if (cfg.components && !enumerated.has(actor.toString())) {
        enumerated.add(actor.toString());
        send({kind:'components', owner:handle.toString(), actor:actor.toString(), data:components(handle)});
      }
      send({kind:'local_actor', thread:Process.getCurrentThreadId(), target:target.toString(),
        actor:actor.toString(), xyz, skeleton:skeleton.toString(), at:now});
    } catch (_) { errors++; }
  }
});
send({kind:'attached', pid:Process.id, base:module.base.toString(), rva:cfg.rva});
rpc.exports = {
  finish() {
    listener.detach();
    Interceptor.flush();
    return {calls, errors, actorCount:actors.size};
  }
};
"""
    lookup = None
    if args.lookup_asset:
        research_hook = json.loads((ROOT / "sheets/research_hooks.json").read_text())["rows"][0]
        research_rva = int(research_hook["rva"], 16)
        research_signature = bytes.fromhex(research_hook["signature"])
        if pe.get_data(research_rva, len(research_signature)) != research_signature:
            raise SystemExit("Research getter static signature mismatch: no instrumentation was attached")
        research_hook = {**research_hook, "rva": research_rva, "context_rva": int(research_hook["context_rva"], 16)}
        lookup = {"path": args.lookup_asset, "id": f"0x{path_id(args.lookup_asset):016x}", "hook": research_hook}
    source = source.replace("__CONFIG__", json.dumps({"exe": compatibility["executable"], "rva": rva, "signature": hook["signature"], "components": args.components, "lookup": lookup, "layout": {row["id"]: row["value"] for row in json.loads((ROOT / "sheets/actor_layout.json").read_text())["rows"]}}))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    records = []
    errors = []
    session = frida.attach(args.pid)
    script = session.create_script(source)
    def on_message(message, data):
        if message["type"] == "send":
            records.append(message["payload"])
        else:
            errors.append(message)
    script.on("message", on_message)
    summary = {}
    try:
        script.load()
        threading.Event().wait(args.seconds)
        summary = script.exports_sync.finish()
    finally:
        try:
            script.unload()
        finally:
            session.detach()
        args.out.write_text(json.dumps({"summary": summary, "records": records, "errors": errors}, indent=2) + "\n", encoding="utf-8")
    samples = [r for r in records if r.get("kind") == "local_actor"]
    component_records = [r for r in records if r.get("kind") == "components"]
    component_summary = [{"owner": r["owner"], "dense": r["data"].get("dense"), "entryCount": len(r["data"].get("entries", [])), "firstEntries": r["data"].get("entries", [])[:4], "heroEntries": [e for e in r["data"].get("entries", []) if "HeroLocal" in e.get("rtti", "") or "HeroRemote" in e.get("rtti", "")]} for r in component_records]
    print(json.dumps({"summary": summary, "samples": len(samples), "errors": errors, "lookups": [r for r in records if r.get("kind") == "asset_lookup"], "components": component_summary, "first": samples[:1], "last": samples[-1:]}, indent=2))
    return 0 if samples and not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
