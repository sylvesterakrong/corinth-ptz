#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QHash>
#include <QVector>

class QTimer;

// Reads the joystick and turns it into actions using per-device-type bindings.
// Backends: SDL2 when built with it (gamepads, flight sticks, generic HID
// joysticks), otherwise XInput on Windows (Xbox-compatible gamepads).
//
// Input ids match the UI: pad buttons a b x y lb rb lt rt view menu l3 r3 up
// down left right; axes ls rs trig (pads), stick twist throttle (flight / PTZ
// keyboard), ax01 ax23 (generic); flight/generic buttons trigger thumb b1..b12,
// hat hatU hatD hatL hatR; PTZ keyboard irisO irisC focN focF f1..f6 prev next lock.
class JoystickManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString backend READ backend CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(QString suggestedType READ suggestedType NOTIFY deviceChanged)
    Q_PROPERTY(bool standardMapping READ standardMapping NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceType READ deviceType WRITE setDeviceType NOTIFY configChanged)
    Q_PROPERTY(QVariantList types READ types CONSTANT)
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY configChanged)
    Q_PROPERTY(QVariantMap bindings READ bindings NOTIFY configChanged)
    Q_PROPERTY(int deadband READ deadband WRITE setDeadband NOTIFY configChanged)
    Q_PROPERTY(int expo READ expo WRITE setExpo NOTIFY configChanged)
    Q_PROPERTY(bool invertTilt READ invertTilt WRITE setInvertTilt NOTIFY configChanged)
    Q_PROPERTY(QVariantList axes READ axes NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap pressed READ pressed NOTIFY stateChanged)
    Q_PROPERTY(bool listening READ listening WRITE setListening NOTIFY listeningChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)   // [{ guid, name, axes, buttons, hats, gamepad }]
    Q_PROPERTY(QString selectedGuid READ selectedGuid NOTIFY deviceChanged)
    Q_PROPERTY(int axisCount READ axisCount NOTIFY deviceChanged)
    Q_PROPERTY(int buttonCount READ buttonCount NOTIFY deviceChanged)
    Q_PROPERTY(int hatCount READ hatCount NOTIFY deviceChanged)

public:
    explicit JoystickManager(QObject *parent = nullptr);
    ~JoystickManager() override;

    bool available() const;
    QString backend() const;
    bool connected() const { return m_connected; }
    QString deviceName() const { return m_name; }
    QString suggestedType() const;
    bool standardMapping() const { return m_isPad; }
    QString deviceType() const { return m_type; }
    void setDeviceType(const QString &t);
    QVariantList types() const;
    QVariantList inputs() const;
    QVariantMap bindings() const;
    int deadband() const { return m_deadband; }
    void setDeadband(int v);
    int expo() const { return m_expo; }
    void setExpo(int v);
    bool invertTilt() const { return m_invert; }
    void setInvertTilt(bool v);
    QVariantList axes() const;
    QVariantMap pressed() const;
    bool listening() const { return m_listening; }
    void setListening(bool l);
    QVariantList devices() const { return m_devices; }
    QString selectedGuid() const { return m_openGuid; }
    int axisCount() const { return m_axisCount; }
    int buttonCount() const { return m_buttonCount; }
    int hatCount() const { return m_hatCount; }

    // Choose which attached device drives the app; remembered by GUID, so it is
    // picked again whenever it is plugged back in.
    Q_INVOKABLE void selectDevice(const QString &guid);
    // Switch the device type (and so the diagram and bindings) to the detected one.
    Q_INVOKABLE void useSuggestedType();

    // Returns a notice for the UI. Prefix "?" = rejected (keep listening),
    // "!" = bound with a warning ("Snapshot → A · Record now unassigned"),
    // no prefix = bound.
    Q_INVOKABLE QString setBinding(const QString &action, const QString &input);
    Q_INVOKABLE void clearBinding(const QString &action);
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE QString label(const QString &input) const;
    Q_INVOKABLE QVariantList actions() const;
    Q_INVOKABLE bool isAxisAction(const QString &action) const;
    Q_INVOKABLE void simulateInput(const QString &input);
    Q_INVOKABLE double shape(double v) const;

signals:
    void deviceChanged();
    void configChanged();
    void stateChanged();
    void listeningChanged();
    void devicesChanged();
    void rawInput(const QString &input);
    void actionPressed(const QString &action);
    void actionReleased(const QString &action);
    void axisCommand(double pan, double tilt, double zoom);

private:
    struct Type {
        QString id, label, shortName, desc, diagram;
        QStringList buttons, axisInputs, realButtons;
        QString axis2;
        QHash<QString, QString> defaults;
        QHash<QString, QString> names;
    };
    void buildTypes();
    const Type &type() const;
    void load();
    void save();
    void poll();
    void refreshDevices();
    void openSelected();
    void closeDevice();
    void handleInputs(const QHash<QString, bool> &now);
    QString actionFor(const QString &input) const;

    QHash<QString, Type> m_types;
    QStringList m_typeOrder;
    QString m_type = QStringLiteral("xbox");
    QHash<QString, QHash<QString, QString>> m_bindings;   // type -> action -> input
    int m_deadband = 12;
    int m_expo = 50;
    bool m_invert = false;
    bool m_listening = false;

    QTimer *m_timer = nullptr;
    bool m_connected = false;
    QString m_name;
    bool m_isPad = false;
    float m_a[4] = {0, 0, 0, 0};
    float m_lt = 0, m_rt = 0;
    QHash<QString, bool> m_pressed;
    QString m_simFlash;
    double m_lastPan = 0, m_lastTilt = 0, m_lastZoom = 0;

    void *m_sdlController = nullptr;
    void *m_sdlJoystick = nullptr;
    bool m_sdlReady = false;
    QVariantList m_devices;
    QString m_wantGuid;      // the device the user picked (saved)
    QString m_openGuid;      // the device that is open now
    int m_openInstance = -1;
    int m_axisCount = 0, m_buttonCount = 0, m_hatCount = 0;
};
