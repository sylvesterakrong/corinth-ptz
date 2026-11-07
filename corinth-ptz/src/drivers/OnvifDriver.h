#pragma once

#include "CameraDriver.h"
#include <QHash>
#include <QStringList>

// ONVIF Profile S driver: PTZ ContinuousMove/Stop/Presets/Home/Status, Imaging
// focus, RelativeMove (click to point) and GetStreamUri, with WS-Security
// UsernameToken (PasswordDigest) auth.
// Works with most PTZ cameras regardless of vendor. Enable ONVIF and create an
// ONVIF user on the camera first (Network > Advanced > Integration Protocol on
// Hikvision-style firmware).
//
// request() answers the Settings pages' ISAPI paths for the subset ONVIF can
// serve (device info, image settings, stream encoder values, reboot) by
// translating to ONVIF calls and back to ISAPI-shaped XML.
// One media profile as the camera lists it.
struct OnvifProfile
{
    QString token;
    QString source;          // VideoSourceConfiguration/SourceToken: one per sensor
    QString encoderToken;    // VideoEncoderConfiguration token
    int width = 0, height = 0;
    bool ptz = false;        // carries a PTZ configuration
};

// For one sensor: which profile is the main stream and which the sub stream.
struct OnvifStreamPlan
{
    QString source;
    int mainIndex = -1;      // index into the profile list
    int subIndex = -1;
    int thirdIndex = -1;     // the next one down; the sub stream again if there are only two
};

class OnvifDriver : public CameraDriver
{
    Q_OBJECT
public:
    explicit OnvifDriver(const CameraConfig &cfg, QObject *parent = nullptr);

    // Dual-spectrum cameras list one video source per sensor. In order of appearance the first
    // source is the visible camera and the second the thermal one; within a source the highest
    // resolution profile is the main stream and the next one the sub stream (the same one if
    // it has only a single profile).
    static QList<OnvifStreamPlan> planStreams(const QList<OnvifProfile> &profiles);
    // The profile PTZ commands go to: the first one with a PTZ configuration, else the first.
    static int ptzProfileIndex(const QList<OnvifProfile> &profiles);

    QString kind() const override { return QStringLiteral("ONVIF"); }

    void continuousMove(int pan, int tilt, int zoom, const QString &label) override;
    void gotoPreset(int id, const QString &label) override;
    void setPreset(int id, const QString &name, const QString &label) override;
    void gotoHome(const QString &label) override;
    void setHome(const QString &label) override;
    void focus(int dir, const QString &label) override;
    void iris(int dir, const QString &label) override;
    void onePushFocus(const QString &label) override;
    void setAux(const QString &name, bool on, const QString &label) override;
    void queryStatus() override;
    void ping() override;
    QString streamUrl(bool mainStream, int channel) const override;
    void request(const QString &method, const QString &path, const QByteArray &body,
                 HttpCallback cb) override;

    bool supportsSettingsEndpoint(const QString &path) const override;
    bool hasChannel2() const override { return m_plan.size() >= 2 || CameraDriver::hasChannel2(); }
    bool supportsZoomLevel() const override { return true; }
    void setZoomLevel(double times, const QString &label) override;
    bool supportsPoint() const override { return true; }
    void pointAt(double nx, double ny, const QString &label) override;
    void queryPresets() override;
    QVariantList auxItems() const override;

private:
    void init();
    void soap(const QString &endpoint, const QString &body, bool auth, HttpCallback cb);
    void ptzCommand(const QString &body, const QString &label, bool changesPresets = false);
    void imagingCommand(const QString &body, const QString &label);
    void imagingGet(int channel, const QString &kind, HttpCallback cb);
    void imagingPut(int channel, const QString &kind, const QByteArray &body, HttpCallback cb);
    void streamGet(int channelId, HttpCallback cb);
    void streamPut(int channelId, const QByteArray &body, HttpCallback cb);
    void streamCaps(int channelId, HttpCallback cb);
    void imagingCaps(int channel, const QString &kind, HttpCallback cb);
    void timeGet(bool ntp, HttpCallback cb);
    void timePut(bool ntp, const QByteArray &body, HttpCallback cb);
    QString encoderTokenFor(int channelId) const;
    QString securityHeader() const;
    QString deviceUrl() const;
    QString presetToken(int id) const;
    QString ptzToken() const;

    QString m_mediaUrl;
    QString m_ptzUrl;
    QString m_imagingUrl;
    QList<OnvifProfile> m_plist;
    QList<OnvifStreamPlan> m_plan;
    QHash<QString, QString> m_uris;   // profile token -> stream address
    QStringList m_sourceTokens;       // one per sensor, in order: for the Imaging service
    QHash<int, QString> m_presetTokens;
    bool m_initRunning = false;
    bool m_statusInFlight = false;
    double m_ptX = 0, m_ptY = 0; bool m_ptKnown = false;   // last reported pan/tilt (ONVIF -1..1), re-sent with an absolute zoom
};
