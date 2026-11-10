/* Copyright (C) 2026 OpenMV, LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Any redistribution, use, or modification in source or binary form
 *    is done solely for personal benefit and not for any commercial
 *    purpose or for monetary gain. For commercial licensing options,
 *    please contact openmv@openmv.io
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
 * OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "openmvsharedaccess.h"

#if defined(Q_OS_WIN)

#include <QDir>

#include <windows.h>
#include <aclapi.h>

#include <string>

// What icacls calls "M" (Modify).
#define MODIFY_ACCESS (FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE)
#define INHERIT_FLAGS (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)

static std::wstring nativePath(const QString &path)
{
    return QDir::toNativeSeparators(path).toStdWString();
}

static bool hasUsersModifyAccess(const QString &path, PSID usersSid)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;

    if (GetNamedSecurityInfoW(nativePath(path).c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, &dacl, nullptr, &sd) != ERROR_SUCCESS)
    {
        return false;
    }

    bool found = false;

    for (DWORD i = 0; dacl && (i < dacl->AceCount) && (!found); i++)
    {
        ACE_HEADER *header = nullptr;

        if ((!GetAce(dacl, i, reinterpret_cast<LPVOID *>(&header)))
        || (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
        || (header->AceFlags & INHERIT_ONLY_ACE))
        {
            continue;
        }

        ACCESS_ALLOWED_ACE *ace = reinterpret_cast<ACCESS_ALLOWED_ACE *>(header);

        found = EqualSid(&ace->SidStart, usersSid)
             && ((ace->Mask & MODIFY_ACCESS) == MODIFY_ACCESS)
             && ((header->AceFlags & INHERIT_FLAGS) == INHERIT_FLAGS);
    }

    LocalFree(sd);
    return found;
}

void grantUsersModifyAccess(const QString &dir, const QStringList &children)
{
    BYTE sidBuffer[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(sidBuffer);
    PSID usersSid = sidBuffer;

    if (!CreateWellKnownSid(WinBuiltinUsersSid, nullptr, usersSid, &sidSize))
    {
        return;
    }

    bool done = hasUsersModifyAccess(dir, usersSid);

    for (int i = 0; done && (i < children.size()); i++)
    {
        // A child that existed before the grant only picks it up through propagation,
        // which fails partway if this account couldn't change that child.
        if (QDir(children.at(i)).exists())
        {
            done = hasUsersModifyAccess(children.at(i), usersSid);
        }
    }

    if (done)
    {
        return;
    }

    std::wstring path = nativePath(dir);
    PACL oldDacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;

    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, &oldDacl, nullptr, &sd) != ERROR_SUCCESS)
    {
        return;
    }

    EXPLICIT_ACCESSW access = {};
    access.grfAccessPermissions = MODIFY_ACCESS;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(usersSid);

    PACL newDacl = nullptr;

    if (SetEntriesInAclW(1, &access, oldDacl, &newDacl) == ERROR_SUCCESS)
    {
        // Also propagates the inheritable entry to everything already inside dir.
        SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                              nullptr, nullptr, newDacl, nullptr);
        LocalFree(newDacl);
    }

    LocalFree(sd);
}

#else

void grantUsersModifyAccess(const QString &dir, const QStringList &children)
{
    Q_UNUSED(dir)
    Q_UNUSED(children)
}

#endif
