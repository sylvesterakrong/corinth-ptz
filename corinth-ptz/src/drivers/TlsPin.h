#pragma once

#include <QCryptographicHash>
#include <QSettings>
#include <QSslCertificate>
#include <QString>

// A camera with HTTPS usually has a self-signed certificate, which a normal TLS check
// rejects. Instead of turning the check off, the app asks once, shows the certificate's
// SHA-256 fingerprint, and remembers it (pins it) for that camera. From then on only that
// exact certificate is accepted; a different one is refused and reported.
namespace TlsPin {

enum class Decision
{
    Trusted,         // the certificate is the pinned one
    NeedsApproval,   // nothing pinned yet: show the fingerprint and ask
    Changed          // something is pinned and this is different: refuse and warn
};

inline Decision decide(const QString &pinned, const QString &seen)
{
    if (pinned.isEmpty()) return Decision::NeedsApproval;
    return pinned.compare(seen, Qt::CaseInsensitive) == 0 ? Decision::Trusted : Decision::Changed;
}

// "AB:CD:..." as the camera's own page or a browser shows it.
inline QString fingerprint(const QSslCertificate &c)
{
    if (c.isNull()) return {};
    return QString::fromLatin1(c.digest(QCryptographicHash::Sha256).toHex(':')).toUpper();
}

inline QString pinned(const QString &cameraKey)
{
    return QSettings().value(QStringLiteral("tls/pin/") + cameraKey).toString();
}

inline void pin(const QString &cameraKey, const QString &fp)
{
    QSettings().setValue(QStringLiteral("tls/pin/") + cameraKey, fp);
}

inline void forget(const QString &cameraKey)
{
    QSettings().remove(QStringLiteral("tls/pin/") + cameraKey);
}

}
