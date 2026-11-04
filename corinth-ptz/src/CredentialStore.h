#pragma once

#include <QString>

// Camera passwords live in the Windows Credential Manager (generic credentials, kept
// per Windows user, never written to the registry or to the settings file). On other
// platforms there is no equivalent in Qt, so they fall back to QSettings: that is
// only a development convenience; the Windows build is the one used in the field.
namespace CredentialStore {

// True when secrets are kept in the operating system's own store.
bool isSecure();
bool save(const QString &target, const QString &user, const QString &secret);
// Empty when nothing is stored under `target`.
QString load(const QString &target);
void remove(const QString &target);

}
