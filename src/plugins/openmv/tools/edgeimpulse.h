#ifndef EDGEIMPULSE_H
#define EDGEIMPULSE_H

#include <QtNetwork>
#include <QHostAddress>
#include <QHostInfo>

#include <extensionsystem/pluginmanager.h>
#include <utils/environment.h>

#include "../openmvdataseteditor.h"

#define EDGE_IMPULSE_SETTINGS_GROUP "OpenMVEdgeImpulse"
#define LAST_TRAIN_TEST_SPLIT "LastTrainTestSplit"
#define LAST_JWT_TOKEN "LastJWTToken"
#define LAST_JWT_TOKEN_EMAIL "LastJWTTokenEmail"
#define LAST_PROJECT_ID "LastProjectId"
#define LAST_API_KEY "LastAPIKey"

QString loggedIntoEdgeImpulse();
void loginToEdgeImpulse(OpenMVDatasetEditor *editor);
void logoutFromEdgeImpulse();

void uploadToSelectedProject(OpenMVDatasetEditor *editor);
void uploadProjectByAPIKey(OpenMVDatasetEditor *editor);

#endif // EDGEIMPULSE_H
