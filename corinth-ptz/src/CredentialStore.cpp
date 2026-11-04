#include "CredentialStore.h"

#include <QSettings>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <wincred.h>
#endif

namespace CredentialStore {

#if defined(Q_OS_WIN)

bool isSecure() { return true; }

bool save(const QString &target, const QString &user, const QString &secret)
{
    const std::wstring t = target.toStdWString();
    const std::wstring u = user.toStdWString();
    const QByteArray blob(reinterpret_cast<const char *>(secret.utf16()), int(secret.size() * 2));
    CREDENTIALW cred;
    ZeroMemory(&cred, sizeof(cred));
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(t.c_str());
    cred.UserName = const_cast<LPWSTR>(u.c_str());
    cred.CredentialBlobSize = DWORD(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;   // this PC only, for this Windows user
    return CredWriteW(&cred, 0) == TRUE;
}

QString load(const QString &target)
{
    const std::wstring t = target.toStdWString();
    PCREDENTIALW p = nullptr;
    if (!CredReadW(t.c_str(), CRED_TYPE_GENERIC, 0, &p) || !p)
        return {};
    const QString s = QString::fromUtf16(reinterpret_cast<const char16_t *>(p->CredentialBlob), int(p->CredentialBlobSize / 2));
    CredFree(p);
    return s;
}

void remove(const QString &target)
{
    const std::wstring t = target.toStdWString();
    CredDeleteW(t.c_str(), CRED_TYPE_GENERIC, 0);
}

#else

bool isSecure() { return false; }

bool save(const QString &target, const QString &, const QString &secret)
{
    QSettings().setValue(QStringLiteral("credentials/") + target, secret);
    return true;
}

QString load(const QString &target)
{
    return QSettings().value(QStringLiteral("credentials/") + target).toString();
}

void remove(const QString &target)
{
    QSettings().remove(QStringLiteral("credentials/") + target);
}

#endif

}
