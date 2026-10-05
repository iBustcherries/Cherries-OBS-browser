#pragma once
#include <obs.h>
#include <QString>
class QWidget;
void CherriesInstallPortrait(QWidget *main, obs_data_t *collectionData);
void CherriesShowPortrait();
void CherriesRestorePortraitVideo(obs_data_t *collectionData);
bool CherriesPreparePortrait(QString &error);
video_t *CherriesPortraitVideo();
QString CherriesPortraitEncoder();
int CherriesPortraitBitrate();
