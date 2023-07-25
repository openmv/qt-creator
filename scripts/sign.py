#!/usr/bin/env python

# by: Kwabena W. Agyeman - kwagyeman@openmv.io

import argparse, fnmatch, os, sys

def try_which(program):
    if os.path.dirname(program):
        if os.path.isfile(program) and os.access(program, os.X_OK):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            file_exe = os.path.join(path.strip('\'').strip('\"'), program)
            if os.path.isfile(file_exe) and os.access(file_exe, os.X_OK):
                return file_exe

def which(program):
    exes = []
    if sys.platform.startswith("win") and not program.lower().endswith(".exe"):
        exes.extend(["exe", "exE", "eXe", "eXE", "Exe", "ExE", "EXe", "EXE"])
    if not exes: return try_which(program)
    for exe in exes:
        if try_which(program + '.' + exe): return program
    return None
kSignCMDAvailable = which("kSignCMD")
signtoolAvailable = which("signtool")
codsignAvailable = which("codesign")

def getPFXFile():
    file = os.path.join(os.path.expanduser('~'), "certificate.pfx")
    return None if not os.path.isfile(file) else file
PFXFile = getPFXFile()

def getPFXPass():
    file = os.path.join(os.path.expanduser('~'), "certificate.txt")
    if not os.path.isfile(file): return None
    with open(file, 'r') as file: return file.readline().strip()
PFXPass = getPFXPass()

def getCERFile():
    file = os.path.join(os.path.expanduser('~'), "certificate.cer")
    return None if not os.path.isfile(file) else file
CERFile = getCERFile()

def signFile(file):
    if sys.platform.startswith("win"):
        if kSignCMDAvailable and PFXFile and PFXPass:
            if not os.system("kSignCMD" + \
            " /f " + PFXFile.replace("/", "\\") + \
            " /p " + PFXPass + \
            " " + file.replace("/", "\\")):
                print("Success")
            else:
                print("Failure")
                raise
        elif signtoolAvailable and PFXFile and PFXPass:
            if not os.system("signtool sign" + \
            " /f " + PFXFile.replace("/", "\\") + \
            " /p " + PFXPass + \
            " /fd sha1 /t http://timestamp.comodoca.com /q " + file.replace("/", "\\") + " > nul" + \
            " && signtool sign" + \
            " /f " + PFXFile.replace("/", "\\") + \
            " /p " + PFXPass + \
            " /fd sha256 /tr http://timestamp.comodoca.com/?td=sha256 /td sha256 /as /q " + file.replace("/", "\\") + " > nul"):
                print("Success")
            else:
                print("Failure")
                raise
        elif signtoolAvailable and CERFile and os.getenv("CS_CONTAINER_NAME") and os.getenv("CS_PASSWORD"):
            if not os.system("signtool sign" + \
            " /f " + CERFile.replace("/", "\\") + \
            " /csp \"eToken Base Cryptographic Provider\"" + \
            " /kc \"[{{" + os.getenv("CS_PASSWORD") + "}}]=" + os.getenv("CS_CONTAINER_NAME") + "\"" \
            " /fd sha1 /t http://timestamp.comodoca.com /q " + file.replace("/", "\\") + " > nul" + \
            " && signtool sign" + \
            " /f " + CERFile.replace("/", "\\") + \
            " /csp \"eToken Base Cryptographic Provider\"" + \
            " /kc \"[{{" + os.getenv("CS_PASSWORD") + "}}]=" + os.getenv("CS_CONTAINER_NAME") + "\"" \
            " /fd sha256 /tr http://timestamp.comodoca.com/?td=sha256 /td sha256 /as /q " + file.replace("/", "\\") + " > nul"):
                print("Success")
            else:
                print("Failure")
                raise
        else: print("Skipping")
        return
    elif sys.platform == "darwin":
        if codsignAvailable:
            if not os.system("codesign" + \
            " -s Application --force --options=runtime --timestamp " + file.replace(" ", "\\ ")):
                print("Success")
            else:
                print("Failure")
                raise
        else: print("Skipping")
        return
    print("Success")

def try_signFile(file):
    print("Signing %s... " % file, end='')
    try: signFile(file)
    except:
        print("Trying again... ", end='')
        try: signFile(file)
        except:
            print("Failed to sign %s." % file)
            # Don't die...
            pass

def main():
    __folder__ = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description = "Sign Script")
    parser.add_argument("target", help = "File or Directory")
    args = parser.parse_args()

    target = args.target
    if target.endswith('*'):
        target = target[:-1]
    if not os.path.isabs(target):
        target = os.path.abspath(target)

    if os.path.isfile(target): try_signFile(target)
    else:

        if sys.platform.startswith("win"):
            extensions = ["*.[eE][xX][eE]", "*.[dD][lL][lL]"]

            for dirpath, dirnames, filenames in os.walk(target):
                paths = dirnames + filenames
                for extension in extensions:
                    for path in fnmatch.filter(paths, extension):
                        try_signFile(os.path.join(dirpath, path))

        elif sys.platform == "darwin":
            files = ["ffmpeg", "ffserver", "ffprobe", "ffplay", "bossac",
                     "dfu-util", "dfu-prefix", "dfu-suffix",
                     "elf2uf2", "picotool", "rp2040load", "blhost", "sdphost"]
            for dirpath, dirnames, filenames in os.walk(target):
                for file in files:
                    for filename in fnmatch.filter(filenames, file):
                        try_signFile(os.path.join(dirpath, filename))

if __name__ == "__main__":
    main()
