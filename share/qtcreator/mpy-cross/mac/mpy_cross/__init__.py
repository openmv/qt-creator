import os
import re
import sys
import stat
import subprocess
from glob import glob
from os.path import join, dirname, abspath
from . import versions

__all__ = ['mpy_cross', 'run']
__pkg_dir = abspath(dirname(__file__))
try:
    mpy_cross = glob(os.path.join(__pkg_dir, 'mpy-cross*'))[0]
except IndexError:
    raise SystemExit("Error: No mpy-cross binary found in: %s" % __pkg_dir)


def set_version(micropython, bytecode):
    global mpy_cross
    vers = versions.mpy_version(micropython, bytecode)
    path = join(__pkg_dir, 'archive', vers, 'mpy-cross*')
    try:
        mpy_cross = glob(path)[0]
    except IndexError:
        raise SystemExit("Error: No mpy-cross binary found in: %s" % dirname(path))
    


def fix_perms():
    try:
        st = os.stat(mpy_cross)
        os.chmod(mpy_cross, st.st_mode | stat.S_IEXEC)
    except OSError:
        pass


def usage():
    fix_perms()
    p = run("-h", stdout=subprocess.PIPE)
    while True:
        line = p.stdout.readline().decode("utf8")
        if not line:
            break
        print(line, end="")
        if line.strip() == "Options:":
            print("-c <version> : --compat <version> : Run mpy-cross in compatibility mode for given micropython version.")
            print("-b <version> : --bytecode <version> : Output specific bytecode version for use with older micropython versions.")


def run(*args, **kwargs):
    fix_perms()
    return subprocess.Popen([mpy_cross] + list(args), **kwargs)


def main():
    compat = None
    bytever = None 
    pop = []
    argv = sys.argv[1:]
    for i, arg in enumerate(argv):
        a = arg.split("=")
        if a[0] in ("-c", "--compat", "-b", "--bytecode"):
            if compat or bytever:
                raise SystemExit("Error: -b and -c are mutually exclusive.")
            pop.append(i)
            if len(a) > 1:
                val = a[1]
            else:
                val = argv[i + 1]
                pop.append(i + 1)
            if "-c" in a[0]:
                compat = val
            else:
                bytever = val
    for i in reversed(pop):
        argv.pop(i)
    if compat or bytever:
        set_version(compat, bytever)
    
    if "-h" in argv or "--help" in argv:
        usage()
    else:
        sys.exit(run(*argv).wait())
