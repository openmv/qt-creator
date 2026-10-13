#!/usr/bin/env python

# Copyright (C) 2023-2024 OpenMV, LLC.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Any redistribution, use, or modification in source or binary form
#    is done solely for personal benefit and not for any commercial
#    purpose or for monetary gain. For commercial licensing options,
#    please contact openmv@openmv.io
#
# THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
# OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse, fnmatch, os, sys, subprocess

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

def checkMach(file):
    result = subprocess.run(["otool", "-hv", file], capture_output=True, text=True, errors="replace")
    return ("is not an object file" not in result.stdout) and ("is not an object file" not in result.stderr)

def signFile(file, args):
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
        elif os.getenv("SM_HOST") and os.getenv("SM_API_KEY") and os.getenv("SM_CLIENT_CERT_PASSWORD") and os.getenv("SM_CODE_SIGNING_CERT_SHA1_HASH"):
            if not os.system("signtool sign" + \
            " /sha1 " + os.getenv("SM_CODE_SIGNING_CERT_SHA1_HASH") + \
            " /tr http://timestamp.digicert.com /td SHA256 /fd SHA256 /q " + file.replace("/", "\\") + " > nul"):
                print("Success")
            else:
                print("Failure")
                raise
        else: print("Skipping")
        return
    elif sys.platform == "darwin":
        if codsignAvailable:
            if not os.system("codesign -d --verbose=4 " + file.replace(" ", "\\ ") + " 2>&1 | grep Timestamp > /dev/null") and \
            not os.system("codesign -d --verbose=4 " + file.replace(" ", "\\ ") + " 2>&1 | grep Authority > /dev/null") and \
            not os.system("codesign -d --verbose=4 " + file.replace(" ", "\\ ") + " 2>&1 | grep Runtime > /dev/null") and \
            not os.system("codesign --verify " + file.replace(" ", "\\ ") + " > /dev/null 2>&1"):
                print("Already Signed")
            else:
                if not os.system("codesign" + \
                " -s Application --force --options=runtime --timestamp " + args + file.replace(" ", "\\ ")):
                    print("Success")
                else:
                    print("Failure")
                    raise
        else: print("Skipping")
        return
    print("Success")

def try_signFile(file, args=""):
    print("Signing %s... " % file, end='')
    try: signFile(file, args)
    except:
        print("Trying again... ", end='')
        try: signFile(file, args)
        except:
            print("Failed to sign %s." % file)
            # Don't die...
            pass

def get_latest_commit():
    result = subprocess.run(['git', 'rev-parse', 'HEAD'], stdout=subprocess.PIPE)
    return result.stdout.decode('utf-8').strip()

def get_latest_tag_commit():
    result = subprocess.run(['git', 'rev-list', '-n', '1', '--tags', '--max-count=1'], stdout=subprocess.PIPE)
    return result.stdout.decode('utf-8').strip()

def is_latest_commit_equal_to_tag():
    latest_commit = get_latest_commit()
    latest_tag_commit = get_latest_tag_commit()
    return latest_commit == latest_tag_commit

def main():
    parser = argparse.ArgumentParser(description = "Sign Script")
    parser.add_argument("target", help = "File or Directory")
    args = parser.parse_args()

    target = args.target
    if target.endswith('*'):
        target = target[:-1]
    if not os.path.isabs(target):
        target = os.path.abspath(target)

    if os.path.isfile(target):
        plist_name = os.path.splitext(os.path.basename(target))[0]
        plist_path = os.path.join(os.path.dirname(target), plist_name + ".plist")
        if sys.platform == "darwin" and os.path.isfile(plist_path) and checkMach(target):
            try_signFile(target, args=("--entitlements " + plist_path.replace(" ", "\\ ") + " "))
        elif sys.platform != "darwin" or checkMach(target):
            try_signFile(target)
    else:

        # Only digitally sign all files on windows during a release...
        if is_latest_commit_equal_to_tag() and sys.platform.startswith("win"):
            extensions = ["*.[eE][xX][eE]"] # "*.[dD][lL][lL]"
            excludeNames = ["dpinst_x86.exe", "dpinst_amd64.exe",
                            "dpinst-x86.exe", "dpinst-amd64.exe",
                            "vcredist_x86.exe", "vcredist_x64.exe"]

            def isExcludedName(path):
                for n in excludeNames:
                    if n == path:
                        return True
                return False

            for dirpath, dirnames, filenames in os.walk(target):
                paths = dirnames + filenames
                for extension in extensions:
                    for path in fnmatch.filter(paths, extension):
                        if not isExcludedName(path):
                            try_signFile(os.path.join(dirpath, path))

        elif sys.platform == "darwin":
            for dirpath, dirnames, filenames in os.walk(target):
                for filename in filenames:
                    path = os.path.join(dirpath, filename)
                    plist_name = os.path.splitext(filename)[0]
                    plist_path = os.path.join(dirpath, plist_name + ".plist")
                    if os.path.isfile(plist_path) and checkMach(path):
                        try_signFile(path, args=("--entitlements " + plist_path.replace(" ", "\\ ") + " "))
                    elif checkMach(path):
                        try_signFile(path)

if __name__ == "__main__":
    main()
