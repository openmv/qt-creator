#ifndef EDGEIMPULSE_H
#define EDGEIMPULSE_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>

#include "../openmvpluginio.h"

#define EDGE_IMPULSE_SETTINGS_GROUP "OpenMVEdgeImpulse"
#define LAST_JWT_TOKEN "LastJWTToken"
#define LAST_JWT_TOKEN_EMAIL "LastJWTTokenEmail"
#define LAST_API_KEY "LastAPIKey"

QString loggedIntoEdgeImpulse();
void loginToEdgeImpulse();
void logoutFromEdgeImpulse();

void uploadToSelectedProject(const QString &path);
void uploadProjectByAPIKey(const QString &path);

#endif // EDGEIMPULSE_H
