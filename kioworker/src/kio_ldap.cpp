/*
  SPDX-FileCopyrightText: 2004-2007 Szombathelyi György <gyurco@freemail.hu>

  SPDX-License-Identifier: MIT

*/

#include "kio_ldap.h"
#include "kldap_debug.h"

#include <kldapcore/ldif.h>

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDebug>

#ifdef Q_OS_WIN
#include <Winsock2.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#endif
#include <sys/stat.h>

using namespace KIO;
using namespace KLDAPCore;

// Pseudo plugin class to embed meta data
class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.worker.ldap" FILE "ldap.json")
};

extern "C" {
int Q_DECL_EXPORT kdemain(int argc, char **argv);
}

/**
 * The main program.
 */
int kdemain(int argc, char **argv)
{
    QCoreApplication app(argc, argv); // needed for QSocketNotifier
    app.setApplicationName(QStringLiteral("kio_ldap"));

    qCDebug(KLDAP_LOG) << "Starting kio_ldap instance";

    if (argc != 4) {
        qCritical() << "Usage kio_ldap protocol pool app";
        return -1;
    }

    // let the protocol class do its work
    LDAPProtocol worker(argv[1], argv[2], argv[3]);
    worker.dispatchLoop();

    qCDebug(KLDAP_LOG) << "Done";
    return 0;
}

/**
 * Initialize the ldap worker
 */
LDAPProtocol::LDAPProtocol(const QByteArray &protocol, const QByteArray &pool, const QByteArray &app)
    : WorkerBase(protocol, pool, app)
    , mProtocol(protocol)
{
    mOp.setConnection(mConn);
    qCDebug(KLDAP_LOG) << "LDAPProtocol::LDAPProtocol (" << protocol << ")";
}

LDAPProtocol::~LDAPProtocol()
{
    closeConnection();
}

KIO::WorkerResult LDAPProtocol::LDAPErr(int err)
{
    QString extramsg;
    if (mConnected) {
        if (err == KLDAP_SUCCESS) {
            err = mConn.ldapErrorCode();
        }
        if (err != KLDAP_SUCCESS) {
            extramsg = i18n("\nAdditional info: ") + mConn.ldapErrorString();
        }
    }
    if (err == KLDAP_SUCCESS) {
        return KIO::WorkerResult::pass();
    }

    qCDebug(KLDAP_LOG) << "error code: " << err << " msg: " << LdapConnection::errorString(err) << extramsg << "'";
    QString msg;
    msg = mServer.url().toDisplayString();
    if (!extramsg.isEmpty()) {
        msg += extramsg;
    }

    /* FIXME: No need to close on all errors */
    closeConnection();

    switch (err) {
    /* FIXME: is it worth mapping the following error codes to kio errors?

      LDAP_OPERATIONS_ERROR
      LDAP_STRONG_AUTH_REQUIRED
      LDAP_PROTOCOL_ERROR
      LDAP_TIMELIMIT_EXCEEDED
      LDAP_SIZELIMIT_EXCEEDED
      LDAP_COMPARE_FALSE
      LDAP_COMPARE_TRUE
      LDAP_PARTIAL_RESULTS
      LDAP_NO_SUCH_ATTRIBUTE
      LDAP_UNDEFINED_TYPE
      LDAP_INAPPROPRIATE_MATCHING
      LDAP_CONSTRAINT_VIOLATION
      LDAP_INVALID_SYNTAX
      LDAP_NO_SUCH_OBJECT
      LDAP_ALIAS_PROBLEM
      LDAP_INVALID_DN_SYNTAX
      LDAP_IS_LEAF
      LDAP_ALIAS_DEREF_PROBLEM
      LDAP_INAPPROPRIATE_AUTH
      LDAP_BUSY
      LDAP_UNAVAILABLE
      LDAP_UNWILLING_TO_PERFORM
      LDAP_LOOP_DETECT
      LDAP_NAMING_VIOLATION
      LDAP_OBJECT_CLASS_VIOLATION
      LDAP_NOT_ALLOWED_ON_NONLEAF
      LDAP_NOT_ALLOWED_ON_RDN
      LDAP_NO_OBJECT_CLASS_MODS
      LDAP_OTHER
      LDAP_LOCAL_ERROR
      LDAP_ENCODING_ERROR
      LDAP_DECODING_ERROR
      LDAP_FILTER_ERROR
    */
    case KLDAP_AUTH_UNKNOWN:
    case KLDAP_INVALID_CREDENTIALS:
    case KLDAP_STRONG_AUTH_NOT_SUPPORTED:
        return KIO::WorkerResult::fail(ERR_CANNOT_AUTHENTICATE, msg);
    case KLDAP_ALREADY_EXISTS:
        return KIO::WorkerResult::fail(ERR_FILE_ALREADY_EXIST, msg);
    case KLDAP_INSUFFICIENT_ACCESS:
        return KIO::WorkerResult::fail(ERR_ACCESS_DENIED, msg);
    case KLDAP_CONNECT_ERROR:
    case KLDAP_SERVER_DOWN:
        return KIO::WorkerResult::fail(ERR_CANNOT_CONNECT, msg);
    case KLDAP_TIMEOUT:
        return KIO::WorkerResult::fail(ERR_SERVER_TIMEOUT, msg);
    case KLDAP_PARAM_ERROR:
        return KIO::WorkerResult::fail(ERR_INTERNAL, msg);
    case KLDAP_NO_MEMORY:
        return KIO::WorkerResult::fail(ERR_OUT_OF_MEMORY, msg);

    default:
        return KIO::WorkerResult::fail(
            KIO::ERR_WORKER_DEFINED,
            i18n("LDAP server returned the error: %1 %2\nThe LDAP URL was: %3", LdapConnection::errorString(err), extramsg, mServer.url().toDisplayString()));
    }
}

void LDAPProtocol::controlsFromMetaData(LdapControls &serverctrls, LdapControls &clientctrls)
{
    QString oid;
    bool critical;
    QByteArray value;
    int i = 0;
    while (hasMetaData(QStringLiteral("SERVER_CTRL%1").arg(i))) {
        const QByteArray val = metaData(QStringLiteral("SERVER_CTRL%1").arg(i)).toUtf8();
        Ldif::splitControl(val, oid, critical, value);
        qCDebug(KLDAP_LOG) << "server ctrl #" << i << " value: " << val << " oid: " << oid << " critical: " << critical
                           << " value: " << QString::fromUtf8(value.constData(), value.size());
        LdapControl ctrl(oid, val, critical);
        serverctrls.append(ctrl);
        i++;
    }
    i = 0;
    while (hasMetaData(QStringLiteral("CLIENT_CTRL%1").arg(i))) {
        const QByteArray val = metaData(QStringLiteral("CLIENT_CTRL%1").arg(i)).toUtf8();
        Ldif::splitControl(val, oid, critical, value);
        qCDebug(KLDAP_LOG) << "client ctrl #" << i << " value: " << val << " oid: " << oid << " critical: " << critical
                           << " value: " << QString::fromUtf8(value.constData(), value.size());
        LdapControl ctrl(oid, val, critical);
        clientctrls.append(ctrl);
        i++;
    }
}

void LDAPProtocol::LDAPEntry2UDSEntry(const LdapDN &dn, UDSEntry &entry, const LdapUrl &usrc, bool dir)
{
    int pos;
    entry.clear();
    QString name = dn.toString();
    if ((pos = name.indexOf(QLatin1Char(','))) > 0) {
        name.truncate(pos);
    }
    if ((pos = name.indexOf(QLatin1Char('='))) > 0) {
        name.remove(0, pos + 1);
    }
    name.replace(QLatin1Char(' '), QLatin1String("_"));
    if (!dir) {
        name += QLatin1String(".ldif");
    }
    entry.fastInsert(KIO::UDSEntry::UDS_NAME, name);

    // the file type
    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, dir ? S_IFDIR : S_IFREG);

    // the mimetype
    if (!dir) {
        entry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("text/plain"));
    }

    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, dir ? 0500 : 0400);

    // the url
    LdapUrl url = usrc;
    url.setPath(QLatin1Char('/') + dn.toString());
    url.setScope(dir ? LdapUrl::One : LdapUrl::Base);
    entry.fastInsert(KIO::UDSEntry::UDS_URL, url.toDisplayString());
}

KIO::WorkerResult LDAPProtocol::changeCheck(const LdapUrl &url)
{
    LdapServer server;
    server.setUrl(url);

    if (mConnected) {
        if (server.host() != mServer.host() || server.port() != mServer.port() || server.baseDn() != mServer.baseDn() || server.user() != mServer.user()
            || server.bindDn() != mServer.bindDn() || server.realm() != mServer.realm() || server.password() != mServer.password()
            || server.timeLimit() != mServer.timeLimit() || server.sizeLimit() != mServer.sizeLimit() || server.version() != mServer.version()
            || server.security() != mServer.security() || server.auth() != mServer.auth() || server.mech() != mServer.mech()) {
            closeConnection();
            mServer = server;
            return openConnection();
        }
        return KIO::WorkerResult::pass();
    }

    mServer = server;
    return openConnection();
}

void LDAPProtocol::setHost(const QString &host, quint16 port, const QString &user, const QString &password)
{
    if (mServer.host() != host || mServer.port() != port || mServer.user() != user || mServer.password() != password) {
        closeConnection();
    }

    mServer.host() = host;
    if (port > 0) {
        mServer.setPort(port);
    } else {
        struct servent *pse;
        if ((pse = getservbyname(mProtocol.constData(), "tcp")) == nullptr) {
            if (mProtocol == "ldaps") {
                mServer.setPort(636);
            } else {
                mServer.setPort(389);
            }
        } else {
            mServer.setPort(ntohs(pse->s_port));
        }
    }
    mServer.setUser(user);
    mServer.setPassword(password);

    qCDebug(KLDAP_LOG) << "setHost: " << host << " port: " << port << " user: " << user << " pass: [protected]";
}

KIO::WorkerResult LDAPProtocol::openConnection()
{
    if (mConnected) {
        return KIO::WorkerResult::pass();
    }

    mConn.setServer(mServer);
    if (mConn.connect() != 0) {
        return KIO::WorkerResult::fail(ERR_CANNOT_CONNECT, mConn.connectionError());
    }

    mConnected = true;

    AuthInfo info;
    info.url.setScheme(QLatin1String(mProtocol));
    info.url.setHost(mServer.host());
    info.url.setPort(mServer.port());
    info.url.setUserName(mServer.user());
    info.caption = i18n("LDAP Login");
    info.comment = QString::fromLatin1(mProtocol) + QLatin1String("://") + mServer.host() + QLatin1Char(':') + QString::number(mServer.port());
    info.commentLabel = i18n("site:");
    info.username = mServer.auth() == LdapServer::SASL ? mServer.user() : mServer.bindDn();
    info.password = mServer.password();
    info.keepPassword = true;
    bool cached = checkCachedAuthentication(info);

    bool firstauth = true;

    while (true) {
        int retval = mOp.bind_s();
        if (retval == 0) {
            break;
        }
        if (retval == KLDAP_INVALID_CREDENTIALS || retval == KLDAP_INSUFFICIENT_ACCESS || retval == KLDAP_INAPPROPRIATE_AUTH
            || retval == KLDAP_UNWILLING_TO_PERFORM) {
            if (firstauth && cached) {
                if (mServer.auth() == LdapServer::SASL) {
                    mServer.setUser(info.username);
                } else {
                    mServer.setBindDn(info.username);
                }
                mServer.setPassword(info.password);
                mConn.setServer(mServer);
                cached = false;
            } else {
                const int errorCode = firstauth ? openPasswordDialog(info) : openPasswordDialog(info, i18n("Invalid authorization information."));
                if (!errorCode) {
                    if (info.keepPassword) { // user asked password be save/remembered
                        cacheAuthentication(info);
                    }
                } else {
                    closeConnection();
                    if (errorCode == ERR_USER_CANCELED) {
                        return KIO::WorkerResult::fail(ERR_USER_CANCELED, i18n("LDAP connection canceled."));
                    }
                    return KIO::WorkerResult::fail(errorCode, QString());
                }
                if (mServer.auth() == LdapServer::SASL) {
                    mServer.setUser(info.username);
                } else {
                    mServer.setBindDn(info.username);
                }
                mServer.setPassword(info.password);
                firstauth = false;
                mConn.setServer(mServer);
            }
        } else {
            const KIO::WorkerResult errorResult = LDAPErr(retval);
            // TODO: ensure that LDAPErr(retval) always closes the connection, avoiding the explicit call
            closeConnection();
            return errorResult;
        }
    }

    qCDebug(KLDAP_LOG) << "connected!";
    return KIO::WorkerResult::pass();
}

void LDAPProtocol::closeConnection()
{
    if (mConnected) {
        mConn.close();
    }
    mConnected = false;

    qCDebug(KLDAP_LOG) << "connection closed!";
}

/**
 * Get the information contained in the URL.
 */
KIO::WorkerResult LDAPProtocol::get(const QUrl &_url)
{
    qCDebug(KLDAP_LOG) << "get(" << _url << ")";

    LdapUrl usrc(_url);

    const KIO::WorkerResult checkResult = changeCheck(usrc);
    if (!checkResult.success()) {
        return checkResult;
    }

    LdapControls serverctrls;
    LdapControls clientctrls;
    controlsFromMetaData(serverctrls, clientctrls);
    if (mServer.pageSize()) {
        LdapControls ctrls = serverctrls;
        ctrls.append(LdapControl::createPageControl(mServer.pageSize()));
        qCDebug(KLDAP_LOG) << "page size: " << mServer.pageSize();
        mOp.setServerControls(ctrls);
    } else {
        mOp.setServerControls(serverctrls);
    }
    mOp.setClientControls(clientctrls);
    int id;
    if ((id = mOp.search(usrc.dn(), usrc.scope(), usrc.filter(), usrc.attributes())) == -1) {
        return LDAPErr();
    }

    // tell the mimetype
    mimeType(QStringLiteral("text/plain"));
    // collect the result
    // QByteArray result;
    filesize_t processed_size = 0;

    int ret;
    while (true) {
        ret = mOp.waitForResult(id, -1);
        if (ret == -1 || mConn.ldapErrorCode() != KLDAP_SUCCESS) {
            return LDAPErr();
        }
        qCDebug(KLDAP_LOG) << " ldap_result: " << ret;
        if (ret == LdapOperation::RES_SEARCH_RESULT) {
            if (mServer.pageSize()) {
                QByteArray cookie;
                int estsize = -1;
                for (int i = 0; i < mOp.controls().count(); ++i) {
                    qCDebug(KLDAP_LOG) << " control oid: " << mOp.controls().at(i).oid();
                    estsize = mOp.controls().at(i).parsePageControl(cookie);
                    if (estsize != -1) {
                        break;
                    }
                }
                qCDebug(KLDAP_LOG) << " estimated size: " << estsize;
                if (estsize != -1 && !cookie.isEmpty()) {
                    LdapControls ctrls{serverctrls}; // clazy:exclude=container-inside-loop
                    qCDebug(KLDAP_LOG) << "page size: " << mServer.pageSize() << " estimated size: " << estsize;
                    ctrls.append(LdapControl::createPageControl(mServer.pageSize(), cookie));
                    mOp.setServerControls(ctrls);
                    if ((id = mOp.search(usrc.dn(), usrc.scope(), usrc.filter(), usrc.attributes())) == -1) {
                        return LDAPErr();
                    }
                    continue;
                }
            }
            break;
        }
        if (ret != LdapOperation::RES_SEARCH_ENTRY) {
            continue;
        }

        QByteArray entry = mOp.object().toString().toUtf8() + '\n';
        processed_size += entry.size();
        data(entry);
        processedSize(processed_size);
    }

    totalSize(processed_size);

    // tell we are finished
    data(QByteArray());
    return KIO::WorkerResult::pass();
}

/**
 * Test if the url contains a directory or a file.
 */
KIO::WorkerResult LDAPProtocol::stat(const QUrl &_url)
{
    qCDebug(KLDAP_LOG) << "stat(" << _url << ")";

    LdapUrl usrc(_url);

    const KIO::WorkerResult checkResult = changeCheck(usrc);
    if (!checkResult.success()) {
        return checkResult;
    }

    int ret;
    int id;
    // look how many entries match
    const QStringList saveatt = usrc.attributes();
    QStringList att{QStringLiteral("dn")};

    if ((id = mOp.search(usrc.dn(), usrc.scope(), usrc.filter(), att)) == -1) {
        return LDAPErr();
    }

    qCDebug(KLDAP_LOG) << "stat() getting result";
    do {
        ret = mOp.waitForResult(id, -1);
        if (ret == -1 || mConn.ldapErrorCode() != KLDAP_SUCCESS) {
            return LDAPErr();
        }
        if (ret == LdapOperation::RES_SEARCH_RESULT) {
            return KIO::WorkerResult::fail(ERR_DOES_NOT_EXIST, _url.toDisplayString());
        }
    } while (ret != LdapOperation::RES_SEARCH_ENTRY);

    mOp.abandon(id);

    usrc.setAttributes(saveatt);

    UDSEntry uds;
    bool critical;
    LDAPEntry2UDSEntry(usrc.dn(), uds, usrc, usrc.extension(QStringLiteral("x-dir"), critical) != QLatin1String("base"));

    statEntry(uds);
    // we are done
    return KIO::WorkerResult::pass();
}

/**
 * Deletes one entry;
 */
KIO::WorkerResult LDAPProtocol::del(const QUrl &_url, bool)
{
    qCDebug(KLDAP_LOG) << "del(" << _url << ")";

    LdapUrl usrc(_url);

    const KIO::WorkerResult checkResult = changeCheck(usrc);
    if (!checkResult.success()) {
        return checkResult;
    }

    LdapControls serverctrls;
    LdapControls clientctrls;
    controlsFromMetaData(serverctrls, clientctrls);
    mOp.setServerControls(serverctrls);
    mOp.setClientControls(clientctrls);

    qCDebug(KLDAP_LOG) << " del: " << usrc.dn().toString().toUtf8();
    int id;
    int ret;

    if ((id = mOp.del(usrc.dn())) == -1) {
        return LDAPErr();
    }
    ret = mOp.waitForResult(id, -1);
    if (ret == -1 || mConn.ldapErrorCode() != KLDAP_SUCCESS) {
        return LDAPErr();
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult LDAPProtocol::put(const QUrl &_url, int, KIO::JobFlags flags)
{
    qCDebug(KLDAP_LOG) << "put(" << _url << ")";

    LdapUrl usrc(_url);

    const KIO::WorkerResult checkResult = changeCheck(usrc);
    if (!checkResult.success()) {
        return checkResult;
    }

    LdapControls serverctrls;
    LdapControls clientctrls;
    controlsFromMetaData(serverctrls, clientctrls);
    mOp.setServerControls(serverctrls);
    mOp.setClientControls(clientctrls);

    LdapObject addObject;
    LdapOperation::ModOps modops;
    QByteArray buffer;
    int result = 0;
    Ldif::ParseValue ret;
    Ldif ldif;
    ret = Ldif::MoreData;
    int ldaperr;

    do {
        if (ret == Ldif::MoreData) {
            dataReq(); // Request for data
            result = readData(buffer);
            ldif.setLdif(buffer);
        }
        if (result < 0) {
            // error
            return KIO::WorkerResult::fail();
        }
        if (result == 0) {
            qCDebug(KLDAP_LOG) << "EOF!";
            ldif.endLdif();
        }
        do {
            ret = ldif.nextItem();
            qCDebug(KLDAP_LOG) << "nextitem: " << ret;

            switch (ret) {
            case Ldif::None:
            case Ldif::NewEntry:
            case Ldif::MoreData:
                break;
            case Ldif::EndEntry:
                ldaperr = KLDAP_SUCCESS;
                switch (ldif.entryType()) {
                case Ldif::Entry_None:
                    return KIO::WorkerResult::fail(ERR_INTERNAL, i18n("The Ldif parser failed."));
                case Ldif::Entry_Del:
                    qCDebug(KLDAP_LOG) << "kio_ldap_del";
                    ldaperr = mOp.del_s(ldif.dn());
                    break;
                case Ldif::Entry_Modrdn:
                    qCDebug(KLDAP_LOG) << "kio_ldap_modrdn olddn:" << ldif.dn().toString() << " newRdn: " << ldif.newRdn()
                                       << " newSuperior: " << ldif.newSuperior() << " deloldrdn: " << ldif.delOldRdn();
                    ldaperr = mOp.rename_s(ldif.dn(), ldif.newRdn(), ldif.newSuperior(), ldif.delOldRdn());
                    break;
                case Ldif::Entry_Mod:
                    qCDebug(KLDAP_LOG) << "kio_ldap_mod";
                    ldaperr = mOp.modify_s(ldif.dn(), modops);
                    modops.clear();
                    break;
                case Ldif::Entry_Add:
                    qCDebug(KLDAP_LOG) << "kio_ldap_add " << ldif.dn().toString();
                    addObject.setDn(ldif.dn());
                    ldaperr = mOp.add_s(addObject);
                    if (ldaperr == KLDAP_ALREADY_EXISTS && (flags & KIO::Overwrite)) {
                        qCDebug(KLDAP_LOG) << ldif.dn().toString() << " already exists, delete first";
                        ldaperr = mOp.del_s(ldif.dn());
                        if (ldaperr == KLDAP_SUCCESS) {
                            ldaperr = mOp.add_s(addObject);
                        }
                    }
                    addObject.clear();
                    break;
                }
                if (ldaperr != KLDAP_SUCCESS) {
                    qCDebug(KLDAP_LOG) << "put ldap error: " << ldaperr;
                    return LDAPErr(ldaperr);
                }
                break;
            case Ldif::Item:
                switch (ldif.entryType()) {
                case Ldif::Entry_Mod: {
                    LdapOperation::ModOp op;
                    op.type = LdapOperation::Mod_None;
                    switch (ldif.modType()) {
                    case Ldif::Mod_None:
                        op.type = LdapOperation::Mod_None;
                        break;
                    case Ldif::Mod_Add:
                        op.type = LdapOperation::Mod_Add;
                        break;
                    case Ldif::Mod_Replace:
                        op.type = LdapOperation::Mod_Replace;
                        break;
                    case Ldif::Mod_Del:
                        op.type = LdapOperation::Mod_Del;
                        break;
                    }
                    op.attr = ldif.attr();
                    if (!ldif.value().isNull()) {
                        op.values.append(ldif.value());
                    }
                    modops.append(op);
                    break;
                }
                case Ldif::Entry_Add:
                    if (!ldif.value().isEmpty()) {
                        addObject.addValue(ldif.attr(), ldif.value());
                    }
                    break;
                default:
                    return KIO::WorkerResult::fail(ERR_INTERNAL, i18n("The Ldif parser failed."));
                }
                break;
            case Ldif::Control: {
                LdapControl control;
                control.setControl(ldif.oid(), ldif.value(), ldif.isCritical());
                serverctrls.append(control);
                mOp.setServerControls(serverctrls);
                break;
            }
            case Ldif::Err:
                return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("Invalid Ldif file in line %1.", ldif.lineNumber()));
            }
        } while (ret != Ldif::MoreData);
    } while (result > 0);

    return KIO::WorkerResult::pass();
}

/**
 * List the contents of a directory.
 */
KIO::WorkerResult LDAPProtocol::listDir(const QUrl &_url)
{
    QStringList att;
    LdapUrl usrc(_url);

    qCDebug(KLDAP_LOG) << "listDir(" << _url << ")";

    const KIO::WorkerResult checkResult = changeCheck(usrc);
    if (!checkResult.success()) {
        return checkResult;
    }

    LdapUrl usrc2 = usrc;

    const QStringList saveatt = usrc.attributes();
    bool critical = true;
    bool isSub = (usrc.extension(QStringLiteral("x-dir"), critical) == QLatin1String("sub"));
    // look up the entries
    if (isSub) {
        att.append(QStringLiteral("dn"));
        usrc.setAttributes(att);
    }
    if (_url.query().isEmpty()) {
        usrc.setScope(LdapUrl::One);
    }
    int id;

    if ((id = mOp.search(usrc.dn(), usrc.scope(), usrc.filter(), usrc.attributes())) == -1) {
        return LDAPErr();
    }

    usrc.setAttributes(QStringList() << QLatin1String(""));
    usrc.setExtension(QStringLiteral("x-dir"), QStringLiteral("base"));
    // publish the results
    UDSEntry uds;

    int ret2;
    int id2;
    unsigned long total = 0;

    int ret;
    while (true) {
        ret = mOp.waitForResult(id, -1);
        if (ret == -1 || mConn.ldapErrorCode() != KLDAP_SUCCESS) {
            return LDAPErr();
        }
        if (ret == LdapOperation::RES_SEARCH_RESULT) {
            break;
        }
        if (ret != LdapOperation::RES_SEARCH_ENTRY) {
            continue;
        }
        qCDebug(KLDAP_LOG) << " ldap_result: " << ret;

        total++;
        uds.clear();

        LDAPEntry2UDSEntry(mOp.object().dn(), uds, usrc);
        listEntry(uds);
        //      processedSize( total );
        qCDebug(KLDAP_LOG) << " total: " << total << " " << usrc.toDisplayString();

        // publish the sub-directories (if dirmode==sub)
        if (isSub) {
            LdapDN dn = mOp.object().dn();
            usrc2.setDn(dn);
            usrc2.setScope(LdapUrl::One);
            usrc2.setAttributes(saveatt);
            usrc2.setFilter(usrc.filter());
            qCDebug(KLDAP_LOG) << "search2 " << dn.toString();
            if ((id2 = mOp.search(dn, LdapUrl::One, QString(), att)) != -1) {
                while (true) {
                    qCDebug(KLDAP_LOG) << " next result ";
                    ret2 = mOp.waitForResult(id2, -1);
                    if (ret2 == -1 || ret2 == LdapOperation::RES_SEARCH_RESULT) {
                        break;
                    }
                    if (ret2 == LdapOperation::RES_SEARCH_ENTRY) {
                        LDAPEntry2UDSEntry(dn, uds, usrc2, true);
                        listEntry(uds);
                        total++;
                        mOp.abandon(id2);
                        break;
                    }
                }
            }
        }
    }

    //  totalSize( total );

    uds.clear();
    // we are done
    return KIO::WorkerResult::pass();
}

#include "kio_ldap.moc"
