#pragma once

#include <QString>
#include <cstdint>
#include <string>

class QWidget;
struct QCefCookieManager;

std::string CherriesTwitchClient(QWidget *parent, bool setup, bool forceSetup = false);
bool CherriesTwitchLogin(QWidget *parent, const std::string &client, std::string &token, std::string &refresh,
			 uint64_t &expiry, QCefCookieManager *cookies);
bool CherriesTwitchEnsureToken(const std::string &client, std::string &token, std::string &refresh, uint64_t &expiry);
