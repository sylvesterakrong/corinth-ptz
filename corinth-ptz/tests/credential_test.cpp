// Camera passwords in the operating system's credential store: write, read back
// (including non-ASCII and long secrets), replace, remove. Uses its own target
// names and deletes them afterwards.
#include "CredentialStore.h"

#include <QCoreApplication>
#include <QDebug>

static int g_fail = 0;

static void check(bool ok, const QString &what)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << what;
    if (!ok) ++g_fail;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CorinthTest"));
    QCoreApplication::setApplicationName(QStringLiteral("credential_test"));

    const QString a = QStringLiteral("CorinthPTZ-unittest/camera/a");
    const QString b = QStringLiteral("CorinthPTZ-unittest/camera/b");
    CredentialStore::remove(a);
    CredentialStore::remove(b);

    check(CredentialStore::load(a).isEmpty(), QStringLiteral("nothing stored yet reads back empty"));
    check(CredentialStore::save(a, QStringLiteral("admin"), QStringLiteral("p@ss word")), QStringLiteral("a password can be stored"));
    check(CredentialStore::load(a) == QLatin1String("p@ss word"), QStringLiteral("and read back unchanged"));

    const QString odd = QStringLiteral("Zażółć gęślą jaźń · 密码 · \"quotes\" & <tags>");
    check(CredentialStore::save(b, QStringLiteral("user"), odd) && CredentialStore::load(b) == odd, QStringLiteral("non-ASCII and punctuation survive"));
    check(CredentialStore::load(a) == QLatin1String("p@ss word"), QStringLiteral("each camera has its own entry"));

    check(CredentialStore::save(a, QStringLiteral("admin"), QStringLiteral("changed")) && CredentialStore::load(a) == QLatin1String("changed"),
          QStringLiteral("saving again replaces the old password"));

    const QString longSecret(400, QLatin1Char('x'));
    check(CredentialStore::save(a, QStringLiteral("admin"), longSecret) && CredentialStore::load(a) == longSecret, QStringLiteral("a 400-character password fits"));

    CredentialStore::remove(a);
    CredentialStore::remove(b);
    check(CredentialStore::load(a).isEmpty() && CredentialStore::load(b).isEmpty(), QStringLiteral("removing deletes the entry"));
    check(CredentialStore::isSecure() || true, QStringLiteral("backend: %1").arg(CredentialStore::isSecure() ? QStringLiteral("Windows Credential Manager") : QStringLiteral("QSettings fallback (not secure)")));

    qInfo().noquote() << "CREDENTIAL TEST failures:" << g_fail;
    return g_fail;
}
