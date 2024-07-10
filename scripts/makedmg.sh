#!/bin/bash

############################################################################
#
# Copyright (C) 2016 The Qt Company Ltd.
# Contact: https://www.qt.io/licensing/
#
# This file is part of Qt Creator.
#
# Commercial License Usage
# Licensees holding valid commercial Qt licenses may use this file in
# accordance with the commercial license agreement provided with the
# Software or, alternatively, in accordance with the terms contained in
# a written agreement between you and The Qt Company. For licensing terms
# and conditions see https://www.qt.io/terms-conditions. For further
# information use the contact form at https://www.qt.io/contact-us.
#
# GNU General Public License Usage
# Alternatively, this file may be used under the terms of the GNU
# General Public License version 3 as published by the Free Software
# Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
# included in the packaging of this file. Please review the following
# information to ensure the GNU General Public License requirements will
# be met: https://www.gnu.org/licenses/gpl-3.0.html.
#
############################################################################

[ $# -lt 2 ] && echo "Usage: $(basename $0) <name.app> <name.dmg>" && exit 2
[ $(uname -s) != "Darwin" ] && echo "Run this script on Mac OS X" && exit 2;
sourceApp="$1"
intermediateFolder=$(mktemp -d "/tmp/packagedir.XXXXX")
finalDMGName="$2"
#OPENMV-DIFF#
#title="Qt Creator"
#OPENMV-DIFF#
title="OpenMV IDE"
#OPENMV-DIFF#

echo Preparing image artifacts...
cp -a "${sourceApp}" "${intermediateFolder}"
ln -s /Applications "${intermediateFolder}"
cp "$(dirname "${BASH_SOURCE[0]}")/../LICENSE.GPL3-EXCEPT" "${intermediateFolder}/LICENSE.GPL3-EXCEPT.txt"
echo Creating image...

# When building on github runner, 'hdiutil create' occasionally fails (resource busy)
# so we make several retries
try_count=0
hdiutil_success=0

while [ $hdiutil_success -ne 1 -a $try_count -lt 8 ]; do
    # Create temporary rw image
    if hdiutil create -srcfolder "${intermediateFolder}" -volname "${title}" -format UDBZ "${finalDMGName}" -ov -scrub -size 1g -verbose
    then
        hdiutil_success=1
        break
    fi
    try_count=$(( $try_count + 1 ))
    echo "'hdiutil create' failed (attempt ${try_count}). Retrying..."
    sleep 1
done

if [ $hdiutil_success -ne 1 -a -n "${GITHUB_RUN_ID}" ]; then
    # Still no success after 8 attempts.
    # If we are on github runner, kill the Xprotect service and make one
    # final attempt.
    # see https://github.com/actions/runner-images/issues/7522
    echo "Killing XProtect..."
    sudo pkill -9 XProtect >/dev/null || true;
    sleep 3

    if hdiutil create -srcfolder "${intermediateFolder}" -volname "${title}" -format UDBZ "${finalDMGName}" -ov -scrub -size 1g -verbose
    then
        hdiutil_success=1
    fi
fi

# make sure that the image is umounted etc
sleep 4

# clean up
rm -rf "${intermediateFolder}"

if [ $hdiutil_success -ne 1 ]; then
    echo "FATAL: 'hdiutil create' FAILED!"
    exit 1
fi
