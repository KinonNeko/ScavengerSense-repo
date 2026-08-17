#!/usr/bin/env python3
"""Validate the FOMOD tree before packaging."""
import os, sys, re
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "fomod-build")
errors, warnings = [], []

def err(m): errors.append(m)
def warn(m): warnings.append(m)

# --- 1. well-formedness -------------------------------------------------
for f in ("fomod/info.xml", "fomod/ModuleConfig.xml"):
    p = os.path.join(BUILD, f)
    if not os.path.exists(p):
        err(f"missing {f}")
        continue
    try:
        ET.parse(p)
    except ET.ParseError as e:
        err(f"{f}: not well-formed: {e}")
if errors:
    print("\n".join(errors)); sys.exit(1)

cfg = ET.parse(os.path.join(BUILD, "fomod/ModuleConfig.xml")).getroot()
info = ET.parse(os.path.join(BUILD, "fomod/info.xml")).getroot()

# --- 2. ModConfig 5.0 child order --------------------------------------
ORDER = ["moduleName", "moduleImage", "moduleDependencies",
         "requiredInstallFiles", "installSteps", "conditionalFileInstalls"]
seen = [c.tag for c in cfg]
for t in seen:
    if t not in ORDER:
        err(f"<config> has unknown child <{t}>")
idx = [ORDER.index(t) for t in seen if t in ORDER]
if idx != sorted(idx):
    err(f"<config> children out of schema order: {seen}")
if cfg.tag != "config":
    err("root element is not <config>")
if not cfg.findtext("moduleName"):
    err("empty moduleName")

# --- 3. every referenced source folder exists, and is referenced once ----
refs = {}
for folder in cfg.iter("folder"):
    src = folder.get("source")
    if src is None:
        err("<folder> without source")
        continue
    refs.setdefault(src, 0)
    refs[src] += 1
    if not os.path.isdir(os.path.join(BUILD, src)):
        err(f"source folder does not exist: {src!r}")
    if folder.get("destination") is None:
        err(f"{src}: folder has no destination attribute")
for f in cfg.iter("file"):
    src = f.get("source")
    if src and not os.path.isfile(os.path.join(BUILD, src)):
        err(f"source file does not exist: {src!r}")

# every payload dir on disk is referenced exactly once
payload = sorted(d for d in os.listdir(BUILD)
                 if os.path.isdir(os.path.join(BUILD, d)) and d != "fomod")
for d in payload:
    n = refs.get(d, 0)
    if n == 0:
        err(f"payload folder {d!r} exists on disk but nothing installs it")
    elif n > 1:
        err(f"payload folder {d!r} referenced {n} times")
for src in refs:
    if src not in payload:
        err(f"reference to {src!r} which is not a payload folder")

# no payload folder is empty
for d in payload:
    if not any(os.path.isfile(os.path.join(r, f))
               for r, _, fs in os.walk(os.path.join(BUILD, d)) for f in fs):
        err(f"payload folder {d!r} contains no files")

# --- 4. no two SIMULTANEOUSLY SELECTABLE folders write the same path ----
# Two folders in one SelectExactlyOne / SelectAtMostOne group can never both be
# installed, so sharing a filename there is deliberate: it guarantees exactly
# one answer's fragment ends up on disk.
exclusive = []          # list of sets of folders that are mutually exclusive
for g in cfg.iter("group"):
    if g.get("type") in ("SelectExactlyOne", "SelectAtMostOne"):
        fs_ = {f.get("source") for p in g.iter("plugin") for f in p.iter("folder")}
        fs_.discard(None)
        if fs_:
            exclusive.append(fs_)

def mutually_exclusive(a, b):
    return any(a in s and b in s for s in exclusive)

dest = {}
for d in payload:
    base = os.path.join(BUILD, d)
    for r, _, fs in os.walk(base):
        for f in fs:
            rel = os.path.relpath(os.path.join(r, f), base).replace("\\", "/").lower()
            if rel in dest and not mutually_exclusive(d, dest[rel]):
                err(f"collision: {d}/{rel} also provided by {dest[rel]}, "
                    f"and both can be selected at once")
            dest.setdefault(rel, d)

# --- 4b. every key in an installer fragment is a key the plugin reads ----
CONFIG = os.path.join(ROOT, "plugin/src/Config.cpp")
known = set()
if os.path.isfile(CONFIG):
    src = open(CONFIG, encoding="utf-8").read()
    for sec, key in re.findall(r'Get\(\s*table,\s*"([^"]+)"\s*,\s*"([^"]+)"', src):
        known.add((sec.lower(), key.lower()))
    # per-category keys are built at runtime: name, nameColor, nameOutlineOnly
    for cat in re.findall(r'^\s*"(\w+)",?\s*$', src, re.M):
        pass
    for cat in ["container", "door", "flora", "gear", "alchemy",
                "book", "valuable", "activator", "furniture", "actor"]:
        for suffix in ("", "color", "outlineonly"):
            known.add(("categories", cat + suffix))
    frag_files = [os.path.join(r, f)
                  for d in payload
                  for r, _, fs in os.walk(os.path.join(BUILD, d))
                  for f in fs
                  if "scavengersense_setup" in r.replace("\\", "/").lower()]
    if not frag_files:
        warn("no installer fragments found - the 'How it should look' page installs nothing")
    for p in frag_files:
        section = None
        for lineno, line in enumerate(open(p, encoding="utf-8"), 1):
            s = line.split(";")[0].strip()
            if not s:
                continue
            if s.startswith("["):
                section = s[1:s.find("]")].lower()
                continue
            if "=" not in s:
                err(f"{os.path.basename(p)}:{lineno}: not a key = value line")
                continue
            key = s.split("=", 1)[0].strip().lower()
            if section == "markers":       # markers.<rule>.<field>, matched at runtime
                continue
            if (section, key) not in known:
                err(f"{os.path.relpath(p, BUILD)}:{lineno}: "
                    f"[{section}] {key} is not a setting the plugin reads")
else:
    warn("Config.cpp not found - fragment keys not checked")

# --- 5. groups / plugins sanity ----------------------------------------
VALID_TYPES = {"Required", "Recommended", "Optional", "CouldBeUsable", "NotUsable"}
VALID_GROUP = {"SelectAtLeastOne", "SelectAtMostOne", "SelectExactlyOne",
               "SelectAll", "SelectAny"}
set_flags, used_flags = set(), set()
step_names = []
for step in cfg.iter("installStep"):
    step_names.append(step.get("name"))
    if not step.get("name"):
        err("installStep without a name")
    if step.find("optionalFileGroups") is None:
        err(f"installStep {step.get('name')!r} has no optionalFileGroups")
for g in cfg.iter("group"):
    if g.get("type") not in VALID_GROUP:
        err(f"group {g.get('name')!r}: bad type {g.get('type')!r}")
    plugs = g.find("plugins")
    if plugs is None or not list(plugs):
        err(f"group {g.get('name')!r} has no plugins")
        continue
    if g.get("type") == "SelectExactlyOne":
        kinds = [t.get("name") for p in plugs
                 for t in p.iter("type")]
        if all(k == "NotUsable" for k in kinds):
            err(f"group {g.get('name')!r} is SelectExactlyOne but every option is NotUsable")
for p in cfg.iter("plugin"):
    name = p.get("name")
    if not name:
        err("plugin without a name")
    # child order inside plugin: description, image?, files?, conditionFlags?, typeDescriptor
    PORDER = ["description", "image", "files", "conditionFlags", "typeDescriptor"]
    ch = [c.tag for c in p]
    for t in ch:
        if t not in PORDER:
            err(f"plugin {name!r}: unknown child <{t}>")
    i = [PORDER.index(t) for t in ch if t in PORDER]
    if i != sorted(i):
        err(f"plugin {name!r}: children out of schema order: {ch}")
    if p.find("description") is None:
        err(f"plugin {name!r}: no description")
    td = p.find("typeDescriptor")
    if td is None:
        err(f"plugin {name!r}: no typeDescriptor")
    else:
        kids = [c.tag for c in td]
        if kids not in (["type"], ["dependencyType"]):
            err(f"plugin {name!r}: typeDescriptor must hold exactly one of "
                f"type/dependencyType, got {kids}")
        for t in td.iter("type"):
            if t.get("name") not in VALID_TYPES:
                err(f"plugin {name!r}: bad type {t.get('name')!r}")
        for t in td.iter("defaultType"):
            if t.get("name") not in VALID_TYPES:
                err(f"plugin {name!r}: bad defaultType {t.get('name')!r}")
    # NotUsable options must install nothing
    types = [t.get("name") for t in p.iter("type")] + \
            [t.get("name") for t in p.iter("defaultType")]
    if types and all(t == "NotUsable" for t in types) and p.find("files") is not None:
        err(f"plugin {name!r}: NotUsable but has <files> that can never install")
    for fl in p.iter("flag"):
        if fl.get("name"):
            set_flags.add(fl.get("name"))
for d in cfg.iter("flagDependency"):
    used_flags.add(d.get("flag"))
    if d.get("value") is None:
        err(f"flagDependency on {d.get('flag')!r} has no value")
for f in used_flags - set_flags:
    err(f"flagDependency reads flag {f!r} that nothing ever sets")
for f in set_flags - used_flags:
    warn(f"flag {f!r} is set but never read")

# --- 6. names ----------------------------------------------------------
blob = open(os.path.join(BUILD, "fomod/ModuleConfig.xml"), encoding="utf-8").read() + \
       open(os.path.join(BUILD, "fomod/info.xml"), encoding="utf-8").read()
if re.search(r"[Ss]urvivor ?[Ss]ense", blob):
    err("old mod name still appears in the FOMOD XML")
for r, _, fs in os.walk(BUILD):
    for f in fs:
        if re.search(r"[Ss]urvivor ?[Ss]ense", f):
            err(f"old name in filename: {os.path.relpath(os.path.join(r, f), BUILD)}")
if info.findtext("Name") != cfg.findtext("moduleName"):
    warn(f"info.xml Name {info.findtext('Name')!r} != moduleName "
         f"{cfg.findtext('moduleName')!r}")

# --- 7. core payload has what the mod needs ----------------------------
core = os.path.join(BUILD, "00 Core")
for need in ("ScavengerSense.esp", "SKSE/Plugins/ScavengerSense.dll",
             "SKSE/Plugins/ScavengerSense_marks.ini",
             "SKSE/Plugins/ScavengerSense_titles.ini"):
    if not os.path.isfile(os.path.join(core, need)):
        err(f"core install is missing {need}")
if os.path.isfile(os.path.join(core, "SKSE/Plugins/ScavengerSense.ini")):
    err("core ships a ScavengerSense.ini - it must not, defaults are built in")

# --- report ------------------------------------------------------------
print(f"payload folders : {len(payload)}  ({', '.join(payload)})")
print(f"install steps   : {len(step_names)}  ({'; '.join(step_names)})")
print(f"options         : {len(list(cfg.iter('plugin')))}")
print(f"flags           : set={sorted(set_flags)} read={sorted(used_flags)}")
print(f"destination files: {len(dest)}")
for w in warnings:
    print("WARN ", w)
for e in errors:
    print("ERROR", e)
print("\n" + ("FAILED" if errors else "OK"))
sys.exit(1 if errors else 0)
