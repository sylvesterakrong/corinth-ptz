#include "JoystickManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <QtMath>
#include <cmath>

#if defined(CORINTH_HAS_SDL2)
#  define SDL_MAIN_HANDLED
#  include <SDL.h>
#elif defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <Xinput.h>
#endif

namespace {
const QStringList kPadButtons = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("x"), QStringLiteral("y"),
                                 QStringLiteral("lb"), QStringLiteral("rb"), QStringLiteral("lt"), QStringLiteral("rt"),
                                 QStringLiteral("view"), QStringLiteral("menu"), QStringLiteral("l3"), QStringLiteral("r3"),
                                 QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("left"), QStringLiteral("right")};
const QStringList kActions = {QStringLiteral("pantilt"), QStringLiteral("zoom"), QStringLiteral("stop"),
                              QStringLiteral("focusNear"), QStringLiteral("focusFar"), QStringLiteral("onePush"),
                              QStringLiteral("home"), QStringLiteral("snapshot"), QStringLiteral("record"),
                              QStringLiteral("channel"), QStringLiteral("presetNext"), QStringLiteral("presetPrev"),
                              QStringLiteral("control"), QStringLiteral("night")};

QString actionName(const QString &a)
{
    static const QHash<QString, QString> n = {
        {QStringLiteral("pantilt"), QStringLiteral("Pan / tilt")}, {QStringLiteral("zoom"), QStringLiteral("Zoom")},
        {QStringLiteral("stop"), QStringLiteral("Stop all")}, {QStringLiteral("focusNear"), QStringLiteral("Focus near")},
        {QStringLiteral("focusFar"), QStringLiteral("Focus far")}, {QStringLiteral("onePush"), QStringLiteral("One-push focus")},
        {QStringLiteral("home"), QStringLiteral("Home")}, {QStringLiteral("snapshot"), QStringLiteral("Snapshot")},
        {QStringLiteral("record"), QStringLiteral("Record")}, {QStringLiteral("channel"), QStringLiteral("Visible / thermal")},
        {QStringLiteral("presetNext"), QStringLiteral("Next preset")}, {QStringLiteral("presetPrev"), QStringLiteral("Previous preset")},
        {QStringLiteral("control"), QStringLiteral("Take / release control")}, {QStringLiteral("night"), QStringLiteral("Night mode")}};
    return n.value(a, a);
}
} // namespace

JoystickManager::JoystickManager(QObject *parent)
    : QObject(parent), m_timer(new QTimer(this))
{
    buildTypes();
    load();

#if defined(CORINTH_HAS_SDL2)
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");   // let Ctrl+C / window close reach Qt
    SDL_SetMainReady();
    m_sdlReady = SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) == 0;
#endif

    connect(m_timer, &QTimer::timeout, this, &JoystickManager::poll);
    m_timer->start(50);   // 20 Hz
}

JoystickManager::~JoystickManager()
{
#if defined(CORINTH_HAS_SDL2)
    if (m_sdlController) SDL_GameControllerClose(static_cast<SDL_GameController *>(m_sdlController));
    if (m_sdlJoystick) SDL_JoystickClose(static_cast<SDL_Joystick *>(m_sdlJoystick));
    if (m_sdlReady) SDL_Quit();
#endif
}

bool JoystickManager::available() const
{
#if defined(CORINTH_HAS_SDL2)
    return m_sdlReady;
#elif defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

QString JoystickManager::backend() const
{
#if defined(CORINTH_HAS_SDL2)
    return QStringLiteral("SDL2");
#elif defined(Q_OS_WIN)
    return QStringLiteral("XInput");
#else
    return QStringLiteral("None (build with SDL2 for joystick support)");
#endif
}

void JoystickManager::buildTypes()
{
    QHash<QString, QString> padDefaults = {
        {QStringLiteral("pantilt"), QStringLiteral("ls")}, {QStringLiteral("zoom"), QStringLiteral("trig")},
        {QStringLiteral("stop"), QStringLiteral("b")}, {QStringLiteral("focusNear"), QStringLiteral("lb")},
        {QStringLiteral("focusFar"), QStringLiteral("rb")}, {QStringLiteral("onePush"), QStringLiteral("r3")},
        {QStringLiteral("home"), QStringLiteral("l3")}, {QStringLiteral("snapshot"), QStringLiteral("a")},
        {QStringLiteral("record"), QStringLiteral("x")}, {QStringLiteral("channel"), QStringLiteral("y")},
        {QStringLiteral("presetNext"), QStringLiteral("right")}, {QStringLiteral("presetPrev"), QStringLiteral("left")},
        {QStringLiteral("control"), QStringLiteral("menu")}, {QStringLiteral("night"), QStringLiteral("view")}};

    Type xbox;
    xbox.id = QStringLiteral("xbox"); xbox.label = QStringLiteral("Gamepad · Xbox"); xbox.shortName = QStringLiteral("Xbox gamepad");
    xbox.desc = QStringLiteral("Two sticks, triggers, ABXY. Cheapest and most common."); xbox.diagram = QStringLiteral("pad");
    xbox.buttons = kPadButtons; xbox.axisInputs = {QStringLiteral("ls"), QStringLiteral("rs"), QStringLiteral("trig")};
    xbox.realButtons = kPadButtons; xbox.axis2 = QStringLiteral("rs"); xbox.defaults = padDefaults;
    xbox.names = {{QStringLiteral("a"), QStringLiteral("A")}, {QStringLiteral("b"), QStringLiteral("B")}, {QStringLiteral("x"), QStringLiteral("X")},
                  {QStringLiteral("y"), QStringLiteral("Y")}, {QStringLiteral("lb"), QStringLiteral("LB")}, {QStringLiteral("rb"), QStringLiteral("RB")},
                  {QStringLiteral("lt"), QStringLiteral("LT")}, {QStringLiteral("rt"), QStringLiteral("RT")}, {QStringLiteral("view"), QStringLiteral("View")},
                  {QStringLiteral("menu"), QStringLiteral("Menu")}, {QStringLiteral("l3"), QStringLiteral("L3")}, {QStringLiteral("r3"), QStringLiteral("R3")}};

    Type ps = xbox;
    ps.id = QStringLiteral("ps"); ps.label = QStringLiteral("Gamepad · PlayStation"); ps.shortName = QStringLiteral("PlayStation gamepad");
    ps.desc = QStringLiteral("Same layout as Xbox with PlayStation button names.");
    ps.names = {{QStringLiteral("a"), QStringLiteral("Cross")}, {QStringLiteral("b"), QStringLiteral("Circle")}, {QStringLiteral("x"), QStringLiteral("Square")},
                {QStringLiteral("y"), QStringLiteral("Triangle")}, {QStringLiteral("lb"), QStringLiteral("L1")}, {QStringLiteral("rb"), QStringLiteral("R1")},
                {QStringLiteral("lt"), QStringLiteral("L2")}, {QStringLiteral("rt"), QStringLiteral("R2")}, {QStringLiteral("view"), QStringLiteral("Share")},
                {QStringLiteral("menu"), QStringLiteral("Options")}, {QStringLiteral("l3"), QStringLiteral("L3")}, {QStringLiteral("r3"), QStringLiteral("R3")}};

    Type flight;
    flight.id = QStringLiteral("flight"); flight.label = QStringLiteral("Flight stick"); flight.shortName = QStringLiteral("Flight stick");
    flight.desc = QStringLiteral("Single stick with twist and throttle. Twist zoom feels natural."); flight.diagram = QStringLiteral("flight");
    flight.realButtons = {QStringLiteral("trigger"), QStringLiteral("thumb"), QStringLiteral("b3"), QStringLiteral("b4"), QStringLiteral("b5"), QStringLiteral("b6"),
                          QStringLiteral("b7"), QStringLiteral("b8"), QStringLiteral("b9"), QStringLiteral("b10"), QStringLiteral("b11"), QStringLiteral("b12")};
    flight.buttons = flight.realButtons + QStringList{QStringLiteral("hatU"), QStringLiteral("hatD"), QStringLiteral("hatL"), QStringLiteral("hatR")};
    flight.axisInputs = {QStringLiteral("stick"), QStringLiteral("twist"), QStringLiteral("throttle")}; flight.axis2 = QStringLiteral("twist");
    flight.defaults = {{QStringLiteral("pantilt"), QStringLiteral("stick")}, {QStringLiteral("zoom"), QStringLiteral("twist")},
                       {QStringLiteral("stop"), QStringLiteral("thumb")}, {QStringLiteral("focusNear"), QStringLiteral("b3")},
                       {QStringLiteral("focusFar"), QStringLiteral("b4")}, {QStringLiteral("onePush"), QStringLiteral("b5")},
                       {QStringLiteral("home"), QStringLiteral("b6")}, {QStringLiteral("snapshot"), QStringLiteral("trigger")},
                       {QStringLiteral("record"), QStringLiteral("b7")}, {QStringLiteral("channel"), QStringLiteral("b8")},
                       {QStringLiteral("presetNext"), QStringLiteral("hatR")}, {QStringLiteral("presetPrev"), QStringLiteral("hatL")},
                       {QStringLiteral("control"), QStringLiteral("b11")}, {QStringLiteral("night"), QStringLiteral("b12")}};
    flight.names = {{QStringLiteral("trigger"), QStringLiteral("Trigger")}, {QStringLiteral("thumb"), QStringLiteral("Thumb")},
                    {QStringLiteral("stick"), QStringLiteral("Stick X/Y")}, {QStringLiteral("twist"), QStringLiteral("Twist")},
                    {QStringLiteral("throttle"), QStringLiteral("Throttle")}};

    Type kb;
    kb.id = QStringLiteral("kb"); kb.label = QStringLiteral("PTZ keyboard joystick"); kb.shortName = QStringLiteral("PTZ keyboard");
    kb.desc = QStringLiteral("CCTV desk unit: 3-axis stick plus iris, focus and function keys."); kb.diagram = QStringLiteral("kb");
    kb.realButtons = {QStringLiteral("f1"), QStringLiteral("f2"), QStringLiteral("f3"), QStringLiteral("f4"), QStringLiteral("f5"), QStringLiteral("f6"),
                      QStringLiteral("irisO"), QStringLiteral("irisC"), QStringLiteral("focN"), QStringLiteral("focF"),
                      QStringLiteral("prev"), QStringLiteral("next"), QStringLiteral("lock")};
    kb.buttons = kb.realButtons;
    kb.axisInputs = {QStringLiteral("stick"), QStringLiteral("twist")}; kb.axis2 = QStringLiteral("twist");
    kb.defaults = {{QStringLiteral("pantilt"), QStringLiteral("stick")}, {QStringLiteral("zoom"), QStringLiteral("twist")},
                   {QStringLiteral("stop"), QStringLiteral("f1")}, {QStringLiteral("focusNear"), QStringLiteral("focN")},
                   {QStringLiteral("focusFar"), QStringLiteral("focF")}, {QStringLiteral("onePush"), QStringLiteral("f2")},
                   {QStringLiteral("home"), QStringLiteral("f3")}, {QStringLiteral("snapshot"), QStringLiteral("f4")},
                   {QStringLiteral("record"), QStringLiteral("f5")}, {QStringLiteral("channel"), QStringLiteral("f6")},
                   {QStringLiteral("presetNext"), QStringLiteral("next")}, {QStringLiteral("presetPrev"), QStringLiteral("prev")},
                   {QStringLiteral("control"), QStringLiteral("lock")}, {QStringLiteral("night"), QString()}};
    kb.names = {{QStringLiteral("irisO"), QStringLiteral("Iris +")}, {QStringLiteral("irisC"), QStringLiteral("Iris −")},
                {QStringLiteral("focN"), QStringLiteral("Focus near")}, {QStringLiteral("focF"), QStringLiteral("Focus far")},
                {QStringLiteral("prev"), QStringLiteral("Prev")}, {QStringLiteral("next"), QStringLiteral("Next")},
                {QStringLiteral("lock"), QStringLiteral("Lock")}, {QStringLiteral("stick"), QStringLiteral("Joystick X/Y")},
                {QStringLiteral("twist"), QStringLiteral("Twist")}};
    for (int i = 1; i <= 6; ++i) kb.names.insert(QStringLiteral("f%1").arg(i), QStringLiteral("F%1").arg(i));

    Type gen;
    gen.id = QStringLiteral("generic"); gen.label = QStringLiteral("Generic HID joystick"); gen.shortName = QStringLiteral("Generic joystick");
    gen.desc = QStringLiteral("Any USB device the system sees as a joystick. Map by number."); gen.diagram = QStringLiteral("generic");
    for (int i = 1; i <= 12; ++i) gen.realButtons << QStringLiteral("b%1").arg(i);
    gen.buttons = gen.realButtons + QStringList{QStringLiteral("hatU"), QStringLiteral("hatD"), QStringLiteral("hatL"), QStringLiteral("hatR")};
    gen.axisInputs = {QStringLiteral("ax01"), QStringLiteral("ax23")}; gen.axis2 = QStringLiteral("ax23");
    gen.defaults = {{QStringLiteral("pantilt"), QStringLiteral("ax01")}, {QStringLiteral("zoom"), QStringLiteral("ax23")},
                    {QStringLiteral("stop"), QStringLiteral("b2")}, {QStringLiteral("focusNear"), QStringLiteral("b5")},
                    {QStringLiteral("focusFar"), QStringLiteral("b6")}, {QStringLiteral("onePush"), QStringLiteral("b3")},
                    {QStringLiteral("home"), QStringLiteral("b4")}, {QStringLiteral("snapshot"), QStringLiteral("b1")},
                    {QStringLiteral("record"), QStringLiteral("b7")}, {QStringLiteral("channel"), QStringLiteral("b8")},
                    {QStringLiteral("presetNext"), QStringLiteral("hatR")}, {QStringLiteral("presetPrev"), QStringLiteral("hatL")},
                    {QStringLiteral("control"), QStringLiteral("b10")}, {QStringLiteral("night"), QStringLiteral("b9")}};
    gen.names = {{QStringLiteral("ax01"), QStringLiteral("Axes 0/1")}, {QStringLiteral("ax23"), QStringLiteral("Axes 2/3")}};

    for (const Type &t : {xbox, ps, flight, kb, gen}) {
        m_types.insert(t.id, t);
        m_typeOrder << t.id;
    }
}

const JoystickManager::Type &JoystickManager::type() const
{
    auto it = m_types.constFind(m_type);
    return it != m_types.constEnd() ? it.value() : m_types.constFind(QStringLiteral("xbox")).value();
}

void JoystickManager::load()
{
    QSettings s;
    m_type = s.value(QStringLiteral("joystick/type"), QStringLiteral("xbox")).toString();
    if (!m_types.contains(m_type)) m_type = QStringLiteral("xbox");
    m_deadband = s.value(QStringLiteral("joystick/deadband"), 12).toInt();
    m_expo = s.value(QStringLiteral("joystick/expo"), 50).toInt();
    m_invert = s.value(QStringLiteral("joystick/invertTilt"), false).toBool();
    for (const QString &id : m_typeOrder) {
        QHash<QString, QString> b = m_types[id].defaults;
        const QByteArray json = s.value(QStringLiteral("joystick/bindings/") + id).toByteArray();
        const QJsonObject o = QJsonDocument::fromJson(json).object();
        for (auto it = o.begin(); it != o.end(); ++it)
            if (kActions.contains(it.key())) b.insert(it.key(), it.value().toString());
        m_bindings.insert(id, b);
    }
}

void JoystickManager::save()
{
    QSettings s;
    s.setValue(QStringLiteral("joystick/type"), m_type);
    s.setValue(QStringLiteral("joystick/deadband"), m_deadband);
    s.setValue(QStringLiteral("joystick/expo"), m_expo);
    s.setValue(QStringLiteral("joystick/invertTilt"), m_invert);
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        QJsonObject o;
        for (auto b = it.value().constBegin(); b != it.value().constEnd(); ++b) o.insert(b.key(), b.value());
        s.setValue(QStringLiteral("joystick/bindings/") + it.key(), QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
}

QString JoystickManager::suggestedType() const
{
    if (!m_connected) return {};
    const QString n = m_name.toLower();
    if (n.contains(QLatin1String("playstation")) || n.contains(QLatin1String("dualsense")) || n.contains(QLatin1String("dualshock")) || n.contains(QLatin1String("ps4")) || n.contains(QLatin1String("ps5")))
        return QStringLiteral("ps");
    if (m_isPad) return QStringLiteral("xbox");
    if (n.contains(QLatin1String("extreme 3d")) || n.contains(QLatin1String("hotas")) || n.contains(QLatin1String("t.16000")) || n.contains(QLatin1String("flight")))
        return QStringLiteral("flight");
    return QStringLiteral("generic");
}

void JoystickManager::setDeviceType(const QString &t)
{
    if (!m_types.contains(t) || t == m_type) return;
    m_type = t;
    m_pressed.clear();
    save();
    emit configChanged();
    emit stateChanged();
}

QVariantList JoystickManager::types() const
{
    QVariantList l;
    for (const QString &id : m_typeOrder) {
        const Type &t = m_types[id];
        l << QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("label"), t.label}, {QStringLiteral("shortName"), t.shortName},
                         {QStringLiteral("desc"), t.desc}, {QStringLiteral("diagram"), t.diagram}};
    }
    return l;
}

QVariantList JoystickManager::inputs() const
{
    QVariantList l;
    const Type &t = type();
    for (const QString &id : t.buttons) l << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label(id)}, {QStringLiteral("axis"), false}};
    for (const QString &id : t.axisInputs)
        if (id != QLatin1String("trig")) l << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label(id)}, {QStringLiteral("axis"), true}};
    return l;
}

QVariantMap JoystickManager::bindings() const
{
    QVariantMap m;
    const auto b = m_bindings.value(m_type);
    for (const QString &a : kActions) m.insert(a, b.value(a));
    return m;
}

void JoystickManager::setDeadband(int v) { v = qBound(0, v, 30); if (v == m_deadband) return; m_deadband = v; save(); emit configChanged(); }
void JoystickManager::setExpo(int v) { v = qBound(0, v, 100); if (v == m_expo) return; m_expo = v; save(); emit configChanged(); }
void JoystickManager::setInvertTilt(bool v) { if (v == m_invert) return; m_invert = v; save(); emit configChanged(); }

void JoystickManager::setListening(bool l)
{
    if (l == m_listening) return;
    m_listening = l;
    emit listeningChanged();
}

QVariantList JoystickManager::axes() const
{
    return {m_a[0], m_a[1], m_a[2], m_a[3], m_lt, m_rt};
}

QVariantMap JoystickManager::pressed() const
{
    QVariantMap m;
    for (auto it = m_pressed.constBegin(); it != m_pressed.constEnd(); ++it)
        if (it.value()) m.insert(it.key(), true);
    if (!m_simFlash.isEmpty()) m.insert(m_simFlash, true);
    return m;
}

QString JoystickManager::label(const QString &input) const
{
    if (input.isEmpty()) return QStringLiteral("Unassigned");
    const Type &t = type();
    if (t.names.contains(input)) return t.names.value(input);
    if (input == QLatin1String("trig")) return t.names.value(QStringLiteral("lt"), QStringLiteral("LT")) + QStringLiteral(" / ") + t.names.value(QStringLiteral("rt"), QStringLiteral("RT"));
    static const QHash<QString, QString> common = {
        {QStringLiteral("up"), QStringLiteral("D-pad ↑")}, {QStringLiteral("down"), QStringLiteral("D-pad ↓")},
        {QStringLiteral("left"), QStringLiteral("D-pad ←")}, {QStringLiteral("right"), QStringLiteral("D-pad →")},
        {QStringLiteral("ls"), QStringLiteral("Left stick")}, {QStringLiteral("rs"), QStringLiteral("Right stick")},
        {QStringLiteral("hatU"), QStringLiteral("Hat ↑")}, {QStringLiteral("hatD"), QStringLiteral("Hat ↓")},
        {QStringLiteral("hatL"), QStringLiteral("Hat ←")}, {QStringLiteral("hatR"), QStringLiteral("Hat →")}};
    if (common.contains(input)) return common.value(input);
    if (input.size() > 1 && input.at(0) == QLatin1Char('b') && input.mid(1).toInt() > 0) return QStringLiteral("Button ") + input.mid(1);
    return input;
}

QVariantList JoystickManager::actions() const
{
    QVariantList l;
    for (const QString &a : kActions)
        l << QVariantMap{{QStringLiteral("id"), a}, {QStringLiteral("name"), actionName(a)}, {QStringLiteral("axis"), isAxisAction(a)},
                         {QStringLiteral("locked"), a == QLatin1String("stop")}, {QStringLiteral("hold"), a == QLatin1String("control")}};
    return l;
}

bool JoystickManager::isAxisAction(const QString &action) const
{
    return action == QLatin1String("pantilt") || action == QLatin1String("zoom");
}

QString JoystickManager::setBinding(const QString &action, const QString &inputIn)
{
    const Type &t = type();
    QString input = inputIn;
    const bool wantsAxis = isAxisAction(action);
    if (wantsAxis && (input == QLatin1String("lt") || input == QLatin1String("rt")) && t.axisInputs.contains(QStringLiteral("trig")))
        input = QStringLiteral("trig");
    const bool isAxis = t.axisInputs.contains(input);
    if (wantsAxis != isAxis)
        return wantsAxis ? QStringLiteral("?Move a stick, twist or trigger for this action")
                         : QStringLiteral("?Press a button for this action");

    auto &b = m_bindings[m_type];
    QString owner;
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        if (it.value() == input && it.key() != action) { owner = it.key(); break; }
    if (owner == QLatin1String("stop"))
        return QStringLiteral("?") + label(input) + QStringLiteral(" is reserved for Stop all");

    QString notice = actionName(action) + QStringLiteral(" → ") + label(input);
    if (!owner.isEmpty()) {
        b.insert(owner, QString());
        notice = QStringLiteral("!") + notice + QStringLiteral(" · ") + actionName(owner) + QStringLiteral(" now unassigned");
    }
    b.insert(action, input);
    save();
    emit configChanged();
    return notice;
}

void JoystickManager::clearBinding(const QString &action)
{
    if (action == QLatin1String("stop")) return;
    m_bindings[m_type].insert(action, QString());
    save();
    emit configChanged();
}

void JoystickManager::restoreDefaults()
{
    m_bindings[m_type] = type().defaults;
    m_deadband = 12; m_expo = 50; m_invert = false;
    save();
    emit configChanged();
}

double JoystickManager::shape(double v) const
{
    const double dead = m_deadband / 100.0, expo = m_expo / 100.0;
    const double a = qAbs(v);
    if (a < dead) return 0.0;
    double n = (a - dead) / (1.0 - dead);
    n = expo * n * n * n + (1.0 - expo) * n;
    return v < 0 ? -n : n;
}

QString JoystickManager::actionFor(const QString &input) const
{
    const auto b = m_bindings.value(m_type);
    for (auto it = b.constBegin(); it != b.constEnd(); ++it)
        if (it.value() == input && !isAxisAction(it.key())) return it.key();
    return {};
}

void JoystickManager::simulateInput(const QString &input)
{
    m_simFlash = input;
    emit stateChanged();
    QTimer::singleShot(300, this, [this, input]() { if (m_simFlash == input) { m_simFlash.clear(); emit stateChanged(); } });
    if (m_listening) { emit rawInput(input); return; }
    const QString a = actionFor(input);
    if (!a.isEmpty()) {
        emit actionPressed(a);
        QTimer::singleShot(150, this, [this, a]() { emit actionReleased(a); });
    }
}

void JoystickManager::handleInputs(const QHash<QString, bool> &now)
{
    // Rising/falling edges -> actions (or raw input while assigning).
    for (auto it = now.constBegin(); it != now.constEnd(); ++it) {
        const bool was = m_pressed.value(it.key(), false);
        if (it.value() == was) continue;
        if (it.value()) {
            if (m_listening) { emit rawInput(it.key()); continue; }
            const QString a = actionFor(it.key());
            if (!a.isEmpty()) emit actionPressed(a);
        } else if (!m_listening) {
            const QString a = actionFor(it.key());
            if (!a.isEmpty()) emit actionReleased(a);
        }
    }
    m_pressed = now;

    const Type &t = type();
    if (m_listening) {
        if (std::hypot(m_a[0], m_a[1]) > 0.6) emit rawInput(t.axisInputs.value(0));
        else if (qAbs(m_a[2]) > 0.6 || qAbs(m_a[3]) > 0.6) emit rawInput(t.axis2);
        return;
    }

    // Axes -> pan/tilt/zoom. Normalised convention: stick up = negative Y.
    const auto b = m_bindings.value(m_type);
    auto pair = [this](const QString &in, double &x, double &y) {
        if (in == QLatin1String("ls") || in == QLatin1String("stick") || in == QLatin1String("ax01")) { x = m_a[0]; y = m_a[1]; return true; }
        if (in == QLatin1String("rs") || in == QLatin1String("ax23")) { x = m_a[2]; y = m_a[3]; return true; }
        return false;
    };
    double px = 0, py = 0;
    pair(b.value(QStringLiteral("pantilt")), px, py);
    double pan = shape(px);
    double tilt = shape(-py);
    if (m_invert) tilt = -tilt;

    double z = 0;
    const QString zb = b.value(QStringLiteral("zoom"));
    if (zb == QLatin1String("trig")) z = m_rt - m_lt;
    else if (zb == QLatin1String("twist")) z = m_a[2];
    else if (zb == QLatin1String("throttle")) z = -m_a[3];
    else { double zx = 0, zy = 0; if (pair(zb, zx, zy)) z = -zy; }
    double zoom = shape(z);

    if (qAbs(pan - m_lastPan) > 0.01 || qAbs(tilt - m_lastTilt) > 0.01 || qAbs(zoom - m_lastZoom) > 0.01) {
        m_lastPan = pan; m_lastTilt = tilt; m_lastZoom = zoom;
        emit axisCommand(pan, tilt, zoom);
    }
}

void JoystickManager::poll()
{
    bool connected = false;
    QString name;
    bool isPad = false;
    QVector<bool> btns;
    float a[4] = {0, 0, 0, 0};
    float lt = 0, rt = 0;
    int hat = 0;   // bit 1 up, 2 right, 4 down, 8 left

#if defined(CORINTH_HAS_SDL2)
    if (m_sdlReady) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED || ev.type == SDL_JOYDEVICEREMOVED) {
                if (m_sdlController) { SDL_GameControllerClose(static_cast<SDL_GameController *>(m_sdlController)); m_sdlController = nullptr; }
                if (m_sdlJoystick) { SDL_JoystickClose(static_cast<SDL_Joystick *>(m_sdlJoystick)); m_sdlJoystick = nullptr; }
            }
        }
        if (!m_sdlController && !m_sdlJoystick && SDL_NumJoysticks() > 0) {
            if (SDL_IsGameController(0)) m_sdlController = SDL_GameControllerOpen(0);
            else m_sdlJoystick = SDL_JoystickOpen(0);
        }
        if (auto *gc = static_cast<SDL_GameController *>(m_sdlController)) {
            if (SDL_GameControllerGetAttached(gc)) {
                connected = true; isPad = true;
                name = QString::fromUtf8(SDL_GameControllerName(gc));
                const SDL_GameControllerButton map[16] = {
                    SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
                    SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_INVALID,
                    SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
                    SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT};
                lt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
                rt = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f;
                for (int i = 0; i < 16; ++i) {
                    if (i == 6) btns << (lt > 0.5f);
                    else if (i == 7) btns << (rt > 0.5f);
                    else btns << (SDL_GameControllerGetButton(gc, map[i]) != 0);
                }
                a[0] = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
                a[1] = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
                a[2] = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX) / 32767.0f;
                a[3] = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) / 32767.0f;
            }
        } else if (auto *js = static_cast<SDL_Joystick *>(m_sdlJoystick)) {
            if (SDL_JoystickGetAttached(js)) {
                connected = true;
                name = QString::fromUtf8(SDL_JoystickName(js));
                const int nb = SDL_JoystickNumButtons(js);
                for (int i = 0; i < nb && i < 16; ++i) btns << (SDL_JoystickGetButton(js, i) != 0);
                const int na = SDL_JoystickNumAxes(js);
                for (int i = 0; i < 4 && i < na; ++i) a[i] = SDL_JoystickGetAxis(js, i) / 32767.0f;
                if (SDL_JoystickNumHats(js) > 0) {
                    const Uint8 h = SDL_JoystickGetHat(js, 0);
                    hat = (h & SDL_HAT_UP ? 1 : 0) | (h & SDL_HAT_RIGHT ? 2 : 0) | (h & SDL_HAT_DOWN ? 4 : 0) | (h & SDL_HAT_LEFT ? 8 : 0);
                }
            }
        }
    }
#elif defined(Q_OS_WIN)
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE st;
        ZeroMemory(&st, sizeof(st));
        if (XInputGetState(i, &st) != ERROR_SUCCESS) continue;
        connected = true; isPad = true;
        name = QStringLiteral("XInput controller %1").arg(i + 1);
        const WORD w = st.Gamepad.wButtons;
        lt = st.Gamepad.bLeftTrigger / 255.0f;
        rt = st.Gamepad.bRightTrigger / 255.0f;
        btns = {bool(w & XINPUT_GAMEPAD_A), bool(w & XINPUT_GAMEPAD_B), bool(w & XINPUT_GAMEPAD_X), bool(w & XINPUT_GAMEPAD_Y),
                bool(w & XINPUT_GAMEPAD_LEFT_SHOULDER), bool(w & XINPUT_GAMEPAD_RIGHT_SHOULDER), lt > 0.5f, rt > 0.5f,
                bool(w & XINPUT_GAMEPAD_BACK), bool(w & XINPUT_GAMEPAD_START), bool(w & XINPUT_GAMEPAD_LEFT_THUMB), bool(w & XINPUT_GAMEPAD_RIGHT_THUMB),
                bool(w & XINPUT_GAMEPAD_DPAD_UP), bool(w & XINPUT_GAMEPAD_DPAD_DOWN), bool(w & XINPUT_GAMEPAD_DPAD_LEFT), bool(w & XINPUT_GAMEPAD_DPAD_RIGHT)};
        // XInput: Y up is positive. Normalise to "up = negative" like SDL.
        a[0] = qBound(-1.0f, st.Gamepad.sThumbLX / 32767.0f, 1.0f);
        a[1] = qBound(-1.0f, -st.Gamepad.sThumbLY / 32767.0f, 1.0f);
        a[2] = qBound(-1.0f, st.Gamepad.sThumbRX / 32767.0f, 1.0f);
        a[3] = qBound(-1.0f, -st.Gamepad.sThumbRY / 32767.0f, 1.0f);
        break;
    }
#endif

    if (connected != m_connected || name != m_name || isPad != m_isPad) {
        const bool lost = m_connected && !connected;
        m_connected = connected; m_name = name; m_isPad = isPad;
        emit deviceChanged();
        if (lost) {
            // Safety: a controller that disconnects mid-move must stop the camera.
            for (auto it = m_pressed.constBegin(); it != m_pressed.constEnd(); ++it)
                if (it.value()) { const QString act = actionFor(it.key()); if (!act.isEmpty()) emit actionReleased(act); }
            m_pressed.clear();
            m_lastPan = m_lastTilt = m_lastZoom = 0;
            emit axisCommand(0, 0, 0);
            emit stateChanged();
        }
    }
    if (!connected) return;

    // Map raw buttons onto the selected device type's input ids.
    QHash<QString, bool> now;
    const Type &t = type();
    if (t.diagram == QLatin1String("pad")) {
        for (int i = 0; i < kPadButtons.size(); ++i) now.insert(kPadButtons.at(i), i < btns.size() && btns.at(i));
    } else {
        for (int i = 0; i < t.realButtons.size(); ++i) now.insert(t.realButtons.at(i), i < btns.size() && btns.at(i));
        if (t.buttons.contains(QStringLiteral("hatU"))) {
            now.insert(QStringLiteral("hatU"), hat & 1);
            now.insert(QStringLiteral("hatR"), hat & 2);
            now.insert(QStringLiteral("hatD"), hat & 4);
            now.insert(QStringLiteral("hatL"), hat & 8);
        }
    }

    bool axesChanged = false;
    for (int i = 0; i < 4; ++i) if (qAbs(m_a[i] - a[i]) > 0.005f) { axesChanged = true; m_a[i] = a[i]; }
    if (qAbs(m_lt - lt) > 0.005f || qAbs(m_rt - rt) > 0.005f) { axesChanged = true; m_lt = lt; m_rt = rt; }
    const bool buttonsChanged = now != m_pressed;

    handleInputs(now);
    if (axesChanged || buttonsChanged) emit stateChanged();
}
