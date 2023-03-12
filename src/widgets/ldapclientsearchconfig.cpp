/*
 * SPDX-FileCopyrightText: 2013-2023 Laurent Montel <montel@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ldapclientsearchconfig.h"
#include <kldap/ldapserver.h>

#include <KConfig>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <qt5keychain/keychain.h>
#else
#include <qt6keychain/keychain.h>
#endif
using namespace QKeychain;
using namespace KLDAP;

class Q_DECL_HIDDEN LdapClientSearchConfig::LdapClientSearchConfigPrivate
{
public:
    LdapClientSearchConfigPrivate() = default;

    ~LdapClientSearchConfigPrivate() = default;
};

Q_GLOBAL_STATIC_WITH_ARGS(KConfig, s_config, (QLatin1String("kabldaprc"), KConfig::NoGlobals))

KConfig *LdapClientSearchConfig::config()
{
    return s_config;
}

LdapClientSearchConfig::LdapClientSearchConfig(QObject *parent)
    : QObject(parent)
    , d(new LdapClientSearchConfig::LdapClientSearchConfigPrivate())
{
}

LdapClientSearchConfig::~LdapClientSearchConfig() = default;

#if 0 // Port it
void LdapClientSearchConfig::clearWalletPassword()
{
    if (!d->wallet) {
        d->wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0);
    }
    if (d->wallet) {
        d->useWallet = true;
        if (d->wallet->hasFolder(QStringLiteral("ldapclient"))) {
            //Recreate it.
            d->wallet->removeFolder(QStringLiteral("ldapclient"));
            d->wallet->createFolder(QStringLiteral("ldapclient"));
            d->wallet->setFolder(QStringLiteral("ldapclient"));
        }
    }
}
#endif
