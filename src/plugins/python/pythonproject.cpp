// Copyright (C) 2019 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "pythonproject.h"

// OPENMV-DIFF //
// #include "pythonbuildconfiguration.h"
// #include "pythonbuildsystem.h"
// OPENMV-DIFF //
#include "pythonconstants.h"
#include "pythonlanguageclient.h"

#include <coreplugin/icontext.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/target.h>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;

namespace Python::Internal {

PythonProject::PythonProject(const FilePath &fileName)
    : Project(Constants::C_PY_PROJECT_MIME_TYPE_TOML, fileName)
{
    setType(PythonProjectId);
    setProjectLanguages(Context(ProjectExplorer::Constants::PYTHON_LANGUAGE_ID));
    setDisplayName(fileName.completeBaseName());
    // OPENMV-DIFF //
    // OpenMV does not use the Python build system / build configuration, so no build
    // system creator is registered and PySide extra-compilers are not wired up. The
    // "No Python interpreter set" diagnostic that the old PythonProject::projectIssues()
    // produced is now provided by the issuesGenerator passed to registerProjectType()
    // in pythonplugin.cpp, so that method is dropped here.
    // setBuildSystemCreator<PythonBuildSystem>();
    // connect(this, &Project::activeBuildConfigurationChanged, this, [this]() {
    //     for (auto bc : allBuildConfigurations()) {
    //         if (auto pythonBc = qobject_cast<PythonBuildConfiguration *>(bc)) {
    //             if (auto client = PyLSClient::clientForPython(pythonBc->python()))
    //                 client->updateExtraCompilers(this);
    //         }
    //     }
    // });
    // OPENMV-DIFF //
}

PythonProjectNode::PythonProjectNode(const FilePath &path)
    : ProjectNode(path)
{
    setDisplayName(path.completeBaseName());
    setAddFileFilter("*.py");
}

PythonFileNode::PythonFileNode(const FilePath &filePath,
                               const QString &nodeDisplayName,
                               FileType fileType)
    : FileNode(filePath, fileType)
    , m_displayName(nodeDisplayName)
{}

QString PythonFileNode::displayName() const
{
    return m_displayName;
}

} // namespace Python::Internal
