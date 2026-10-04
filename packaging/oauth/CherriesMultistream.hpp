#pragma once

class OBSBasic;
class QWidget;
void CherriesInstallMultistream(OBSBasic *main);
void CherriesAttachStreamSettings(QWidget *page);
void CherriesDetachStreamSettings();
bool CherriesStartSelectedStreams();
bool CherriesHasSelectedStreams();
void CherriesManageBroadcast();
