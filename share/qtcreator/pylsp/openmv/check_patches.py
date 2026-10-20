"""Checks that the OpenMV IDE's language server patches (sitecustomize.py) work.

Run it with a desktop Python after updating a pylsp bundle:

    python share/qtcreator/pylsp/openmv/check_patches.py [bundle ...]

With no arguments it checks every bundle that runs on this machine's Python
(they are pure Python, so any of them can be checked anywhere: name them, e.g.
`windows mac linux-x86_64`). Each check runs in a new Python started the way the
IDE starts the server (this folder, then the bundle, on PYTHONPATH), so it tests
exactly what the IDE gets.
"""

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PYLSP = HERE.parent
STUBS = PYLSP.parent / "html" / "stubs"
BUNDLES = ["windows", "mac", "mac-aarch64", "linux-x86_64", "linux-arm64"]

CHECKS = r'''
import sys, jedi
stubs = sys.argv[1]
project = jedi.Project(path=stubs, sys_path=[stubs], smart_sys_path=False)

def names(source):
    lines = source.split("\n")
    script = jedi.Script(source, project=project)
    return [c.name for c in script.complete(len(lines), len(lines[-1]))]

failures = []
def check(condition, what):
    if not condition:
        failures.append(what)

# 1. no name mangling
check("__read_reg" in names("import csi\nc = csi.CSI()\nc."), "csi.CSI().__read_reg completes")
# 2. stub-only completion
re_names = names("import re\nre.")
check("search" in re_names and "findall" not in re_names, "re: search, no findall")
check("JSONDecoder" not in names("import json\njson."), "json: no JSONDecoder")
check("randint" in names("import random\nrandom."), "random completes")
check(len(names("import tkinter\ntkinter.")) > 50, "unstubbed module (tkinter) falls back")
# 3. stubs first
d = jedi.Script("import re\nre.search", project=project).infer(2, 5)
check(d and str(d[0].module_path).endswith(".pyi"), "hover on re.search lands on the stub")
d = jedi.Script("import re\nre.search", project=project).goto(2, 5, follow_imports=True)
check(d and str(d[0].module_path).endswith(".pyi"), "go to definition lands on the stub")
# 4. stub docstrings
d = jedi.Script("import re\nre.search", project=project).infer(2, 5)
check(d and "Compile regex_str" in d[0].docstring(raw=True), "re.search shows the stub's docs")
# 5. stub submodules
check("_compiler" not in re_names, "re: no _compiler submodule")

print("jedi", jedi.__version__, "OK" if not failures else "FAILED: " + "; ".join(failures))
sys.exit(1 if failures else 0)
'''


def main():
    bundles = sys.argv[1:] or [b for b in BUNDLES if (PYLSP / b / "jedi").is_dir()]
    failed = False
    for bundle in bundles:
        env = dict(os.environ)
        env["PYTHONPATH"] = os.pathsep.join([str(HERE), str(PYLSP / bundle)])
        result = subprocess.run([sys.executable, "-c", CHECKS, str(STUBS)], env=env,
                                capture_output=True, text=True)
        print("%-14s %s" % (bundle, (result.stdout + result.stderr).strip()))
        failed |= result.returncode != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
