#pragma once
#include <obs.h>

class OBSBasic;
class QWidget;
void CherriesInstallMultistream(OBSBasic *main);
void CherriesAttachStreamSettings(QWidget *page);
void CherriesDetachStreamSettings();
bool CherriesStartSelectedStreams();
bool CherriesHasSelectedStreams();
void CherriesManageBroadcast(int tab = -1);
bool CherriesConfigureAudio(obs_output_t *output);
