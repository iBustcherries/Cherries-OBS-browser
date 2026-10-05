#pragma once

#include <QString>
#include <functional>
#include <string>

class QWidget;
QWidget *CherriesCreateTwitchBroadcast(QWidget *parent, const QString &account, const std::string &client,
				       std::function<std::string()> token);
