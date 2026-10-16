#include "utils.h"
#include "application.h"

#include <DSysInfo>

#include <QDBusInterface>
#include <QGuiApplication>
#include <QPalette>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QUuid>
#include <QJsonDocument>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>

#define UOSDeveloperModeFile "/var/lib/deepin/developer-mode/enabled"

/**
 * @author chatGPT
 * @brief Utils::sendNotification 发送系统通知
 * @param icon 图标名称
 * @param title 通知标题
 * @param body 通知内容
 */
void Utils::sendNotification(const QString &icon, const QString &title, const QString &body)
{
    QDBusInterface interface("org.freedesktop.Notifications",
                             "/org/freedesktop/Notifications",
                             "org.freedesktop.Notifications");

    QVariantList args;
    args << Application::applicationName() // the name of the application
         << static_cast<quint32>(0)        // replaces the previous notification with the same ID
         << icon                           // the application icon of the notification
         << title                          // the title of the notification
         << body                           // the body of the notification
         << QStringList()                  // no actions
         << QVariantMap()                  // no hints
         << -1;                            // no timeout

    interface.callWithArgumentList(QDBus::AutoDetect, "Notify", args);
}

/**
 * @brief Utils::isDDE 判断是否使用 DDE 桌面环境
 * @return true: 当前会话是 DDE；false: 其它桌面（KDE / GNOME / XFCE ...）
 * @note 旧实现在非 DDE 桌面上会把 XDG_CURRENT_DESKTOP 直接改写成 "Deepin"。
 *       这个变量是进程级、全局的，会让 xdg-desktop-portal、GTK 等按桌面环境选择后端的组件
 *       全部误判（典型表现：color-scheme 读不到，主题只能走兜底分支；见 ThemeChecker）。
 *       这里只做判断，不再污染环境变量。
 */
bool Utils::isDDE()
{
    return QString::fromUtf8(qgetenv("XDG_CURRENT_DESKTOP")).toLower().contains(QLatin1String("deepin"));
}

/**
 * @brief 通过 /etc/os-release 判断是否为 deepin 系发行版（deepin / UOS / LinuxDeepin）
 * @note 只读文件与变量，不依赖 QCoreApplication，因此在 QApplication 构造之前调用也是安全的
 *       （setQPAPlatform() 就是在这个时机被调用的）。
 */
static bool osReleaseIsDeepinFamily()
{
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QStringList lines = QString::fromUtf8(file.readAll()).toLower().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (!line.startsWith(QLatin1String("id=")) && !line.startsWith(QLatin1String("id_like="))) {
            continue;
        }

        const QString value = line.mid(line.indexOf(QLatin1Char('=')) + 1).remove(QLatin1Char('"'));
        const QStringList tokens = value.split(QLatin1Char(' '));
        for (const QString &token : tokens) {
            if (token == QLatin1String("deepin") || token == QLatin1String("linuxdeepin") || token == QLatin1String("uos")) {
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Utils::isDeepinFamily 判断是否运行在 deepin 系发行版 / DDE 会话上
 * @note deepin 专有的集成（dxcb 平台插件、deepin 平台主题）只应该在这些环境里启用。
 */
bool Utils::isDeepinFamily()
{
    if (isDDE()) {
        return true;
    }

    return osReleaseIsDeepinFamily();
}

/**
 * @brief Utils::isWayland 判断是否使用 wayland 显示协议
 * @return bool true: 使用 wayland 显示协议 false: 使用 x11 显示协议
 */
bool Utils::isWayland()
{
    bool isWayland = false;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString XDG_SESSION_TYPE = env.value(QStringLiteral("XDG_SESSION_TYPE"));
    QString WAYLAND_DISPLAY = env.value(QStringLiteral("WAYLAND_DISPLAY"));
    if (XDG_SESSION_TYPE == QLatin1String("wayland") || WAYLAND_DISPLAY.contains(QLatin1String("wayland"), Qt::CaseInsensitive))
    {
        isWayland = true;
    }

    return isWayland;
}

/**
 * @brief 判断是否使用 TreeLand 混合器
 * @return bool true: 使用 TreeLand 混合器 false: 非 TreeLand 混合器
 */
bool Utils::isTreeLand()
{
    bool isTreeLand = false;
    if (qgetenv("DDE_CURRENT_COMPOSITOR").toLower() == "treeland") {
        isTreeLand = true;
    }

    return isTreeLand;
}

bool Utils::isPhytium()
{
    bool isPhytium = false;
    QProcess process;
    process.start("lscpu");
    process.waitForFinished();


    QString output = process.readAllStandardOutput();
    if (output.contains(QLatin1String("Phytium")))
    {
        isPhytium = true;
    }

    return isPhytium;
}
/**
 * @brief Utils::initConfig 初始化 config.ini 配置文件，去除废弃字段
 */
void Utils::initConfig()
{
    // WARNING: 请在 组织名称 和 应用程序名称 初始化完成后调用
    bool useWayland = true;

    QSettings config(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/config.ini", QSettings::IniFormat);
    if (config.contains("build/useWayland"))
    {
        useWayland = config.value("build/useWayland").toBool();
    }
    config.remove("build/isDeepinOS");
    config.remove("build/isWayland");
    config.remove("build/useWayland");

    config.setValue("runtime/isDDE", isDDE());
    config.setValue("runtime/isWayland", isWayland());
    // Check config file, if there is no wayland config, then set it to default, which means use wayland if possible.
    if (!config.contains("runtime/useWayland"))
    {
        config.setValue("runtime/useWayland", useWayland);
    }

    // check uuid
    if (!config.contains("info/uuid")){
        config.setValue("info/uuid", initUUID());
    }
    config.sync(); // 写入更改至 config.ini，并同步最新内容

    checkUOSDeveloperMode(); // Check UOS developer mode
}

/**
 * @brief Utils::isUOS 判断是否为 UOS 操作系统
 * @return bool true: UOS 操作系统 false: 非 UOS 操作系统
 */
bool Utils::isUOS()
{
    // WARNING: 请在 组织名称 和 应用程序名称 初始化完成后调用
    bool isUOS = false;

    QSettings config(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/config.ini", QSettings::IniFormat);
    if (Dtk::Core::DSysInfo::productType() == Dtk::Core::DSysInfo::Uos)
    {
        isUOS = true;
        config.setValue("UOS/isUOS", true);
    }
    else
    {
        isUOS = false;
        config.remove("UOS");
    }
    config.sync(); // 写入更改至 config.ini，并同步最新内容

    return isUOS;
}

/**
 * @brief Utils::initUUID 生成 UUID
*/
QString Utils::initUUID(){
    QUuid uuid = QUuid::createUuid();
    return uuid.toString();
}

/**
 * @brief Utils::setQPAPlatform Set display backend
 * @note 本函数在 QApplication 构造之前调用，因此只能用环境变量 + /etc/os-release 判断发行版。
 *
 *       改动说明：旧实现无条件把 QT_QPA_PLATFORM 覆盖成 "dxcb;xcb"（X11）/ "wayland"。
 *       dxcb 是 deepin 私有的平台插件，在 Kubuntu 这类非 deepin 桌面上强行加载它会
 *       绕过桌面自身的窗口装饰与合成协议（KWin/Breeze），是窗口渲染异常（含文字不显示）的常见来源。
 *       现在只在 deepin 系环境里启用 dxcb，其它发行版交给 Qt / 桌面环境自己决定。
 */
void Utils::setQPAPlatform()
{
    // WARNING: 请在 initConfig() 执行后调用
    const bool isWayland = Utils::isWayland();
    const bool isDeepinFamilyOS = Utils::isDeepinFamily();

    QSettings config(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/config.ini", QSettings::IniFormat);
    const bool useWayland = config.value("runtime/useWayland").toBool();

    qDebug() << "System Wayland enabled:" << isWayland
             << "Spark Wayland enabled:" << useWayland
             << "deepin family:" << isDeepinFamilyOS;

    // 用户或桌面环境已经显式指定了平台插件，就不要抢（便于排查与自定义）
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qDebug() << "Keep the QT_QPA_PLATFORM from environment:" << qgetenv("QT_QPA_PLATFORM");
        return;
    }

    /**
     * NOTE: https://github.com/linuxdeepin/developer-center/issues/7217#issuecomment-1922653903
     * DDE Wayland has been deprecated, so using wayland plugin only
     */
    if (isWayland && useWayland)
    {
        qputenv("QT_QPA_PLATFORM", "wayland");
    }
    else if (isDeepinFamilyOS)
    {
        // dxcb 缺失时回退到 xcb
        qputenv("QT_QPA_PLATFORM", "dxcb;xcb");
    }
    else
    {
        // 非 deepin 桌面：不注入 deepin 专有插件，保持 Qt 默认（X11 下即 xcb）
        qDebug() << "Non-deepin desktop detected, keep the default platform plugin";
    }
}

/**
 * @brief Utils::paletteWindowColor 取当前调色板的窗口底色
 * @param fallbackColor 调色板不可用时使用的兜底色
 * @note 背景色必须与控件文字色来自同一套调色板：旧实现把背景色写死成
 *       #2a2b2b / #f3f7f8，而文字色来自 DTK 调色板，两者在非 DDE 桌面上可能不一致，
 *       出现「白字画在浅色底上」——用户看到的就是"字体不显示"。
 */
QColor Utils::paletteWindowColor(const QColor &fallbackColor)
{
    const QColor color = QGuiApplication::palette().color(QPalette::Window);
    return color.isValid() ? color : fallbackColor;
}

/**
 * @brief Utils::paletteCardColor 卡片/分组框底色：由窗口底色派生，保留与窗口的层次差
 */
QColor Utils::paletteCardColor(bool dark)
{
    const QColor base = paletteWindowColor(dark ? QColor("#2a2b2b") : QColor("#f3f7f8"));
    return dark ? base.darker(112) : base.lighter(103);
}

/**
 * @brief Utils::paletteCardBorderColor 卡片/分组框描边色
 */
QColor Utils::paletteCardBorderColor(bool dark)
{
    const QColor base = paletteWindowColor(dark ? QColor("#2a2b2b") : QColor("#f3f7f8"));
    return dark ? base.lighter(150) : base.darker(108);
}

/**
 * @brief Utils::checkUOSDeveloperMode Check UOS developer mode
 */
void Utils::checkUOSDeveloperMode()
{
    QSettings config(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/config.ini", QSettings::IniFormat);

    QFile file(UOSDeveloperModeFile);
    if (Utils::isUOS() && file.exists() && file.open(QFile::ReadOnly | QFile::Text))
    {
        QString lineData = QString::fromUtf8(file.readLine());
        bool devmode = lineData.trimmed().toInt();
        qDebug() << "UOS Developer Mode Status:" << devmode;
        config.setValue("UOS/EnableDeveloperMode", devmode);
    }
    else
    {
        /**
         * NOTE: Utils::isUOS() 中，判断为非 UOS 时已经从 config 中删除 UOS 组，
         * 此处若包含该字段则一定是 UOS，直接写入开发者模式开关状态即可
         */
        if (config.contains("UOS/EnableDeveloperMode"))
        {
            config.setValue("UOS/EnableDeveloperMode", false);
        }
    }
    file.close();
    config.sync(); // 写入更改至 config.ini，并同步最新内容
}

/**
 * @brief Utils::parseFeatureJsonFile 解析版本特性 json 文件
 * @return 返回 QJsonObject
 */
QJsonObject Utils::parseFeatureJsonFile()
{
    QFile file(":/json/features.json");
    if (!file.open(QFile::ReadOnly))
    {
        qWarning() << Q_FUNC_INFO << "features.json open failed";
        return QJsonObject();
    }

    QJsonParseError error;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || jsonDoc.isNull())
    {
        qWarning() << Q_FUNC_INFO << "features.json validate failed:" << error.errorString();
        return QJsonObject();
    }

    if (jsonDoc.isEmpty() || !jsonDoc.isObject())
    {
        qWarning() << Q_FUNC_INFO << "features jsonDoc parse failed:" << jsonDoc;
        return QJsonObject();
    }

    return jsonDoc.object();
}

/**
 * @brief Utils::shouldDisableWebEngineSandbox 检查是否应关闭webengine沙箱
 * @return bool true: 配置文件中设置了关闭沙箱 false: 未设置或设置为false
 */
bool Utils::shouldDisableWebEngineSandbox()
{
    // WARNING: 请在 组织名称 和 应用程序名称 初始化完成后调用
    QSettings config(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/config.ini", QSettings::IniFormat);
    
    // 检查配置文件中[webengine]部分的noSandbox配置项
    // 如果配置存在且值为true，则返回true；否则返回false
    qDebug()<<"sandbox is set to"<<config.value("webengine/noSandbox", false).toBool();
    return config.value("webengine/noSandbox", false).toBool();
}

// 日志相关静态变量
static QFile *logFile = nullptr;
static QString logFilePath;

// 自定义消息处理器，捕获所有Qt日志输出
void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    QString level;
    
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        fprintf(stderr, "DEBUG: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtInfoMsg:
        level = "INFO";
        fprintf(stderr, "INFO: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtWarningMsg:
        level = "WARNING";
        fprintf(stderr, "WARNING: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtCriticalMsg:
        level = "ERROR";
        fprintf(stderr, "ERROR: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        break;
    case QtFatalMsg:
        level = "FATAL";
        fprintf(stderr, "FATAL: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
        abort();
    }
    
    // 写入到日志文件
    Utils::writeLog(level, msg);
}

// 初始化日志系统
void Utils::initLogger()
{
    // 确保日志目录存在
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir;
    if (!dir.exists(logDir)) {
        dir.mkpath(logDir);
    }
    
    // 设置日志文件路径
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    logFilePath = logDir + QString("/spark-store_%1.log").arg(timestamp);
    
    // 打开日志文件
    logFile = new QFile(logFilePath);
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Failed to open log file:" << logFilePath;
        delete logFile;
        logFile = nullptr;
        return;
    }
    
    // 安装自定义消息处理器，捕获所有Qt日志输出
    qInstallMessageHandler(customMessageHandler);
    
    // 写入日志头信息
    writeLog("INFO", "Logger initialized");
    writeLog("INFO", QString("Application started at %1").arg(
             QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")));
}

// 写入日志
void Utils::writeLog(const QString &level, const QString &message)
{
    if (!logFile || !logFile->isOpen()) {
        return;
    }
    
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString logEntry = QString("[%1] [%2] %3\n").arg(timestamp).arg(level).arg(message);
    
    QTextStream out(logFile);
    out << logEntry;
    logFile->flush();
    
    // 同时输出到控制台，便于调试
    if (level == "ERROR") {
        qCritical() << logEntry.trimmed();
    } else if (level == "WARNING") {
        qWarning() << logEntry.trimmed();
    } else {
        qDebug() << logEntry.trimmed();
    }
}

// 导出日志
bool Utils::exportLogs(const QString &targetPath)
{
    QString exportPath = targetPath;
    if (exportPath.isEmpty()) {
        exportPath = "/tmp/spark-store";
    }
    
    // 确保目标目录存在
    QDir dir;
    if (!dir.exists(exportPath)) {
        if (!dir.mkpath(exportPath)) {
            writeLog("ERROR", QString("Failed to create target directory: %1").arg(exportPath));
            return false;
        }
    }
    
    // 关闭当前日志文件，便于复制
    if (logFile && logFile->isOpen()) {
        logFile->close();
    }
    
    // 复制日志文件到目标位置
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString targetLogPath = exportPath + QString("/spark-store_full_log_%1.log").arg(timestamp);
    
    bool success = QFile::copy(logFilePath, targetLogPath);
    
    // 额外检查：即使QFile::copy返回false，也要检查目标文件是否实际存在且大小合理
    if (!success) {
        QFileInfo targetFileInfo(targetLogPath);
        if (targetFileInfo.exists() && targetFileInfo.size() > 0) {
            success = true;
        }
    }
    
    if (success) {
        writeLog("INFO", QString("All logs (INFO, DEBUG, WARNING, ERROR) exported to: %1").arg(targetLogPath));
        
        // 同时创建一个简单的导出报告
        QString reportPath = exportPath + QString("/export_report_%1.txt").arg(timestamp);
        QFile reportFile(reportPath);
        if (reportFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&reportFile);
            out << "Spark Store Log Export Report\n";
            out << "================================\n";
            out << "Export Time: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
            out << "Target Directory: " << exportPath << "\n";
            out << "Log File: " << targetLogPath << "\n";
            out << "Original Log: " << logFilePath << "\n";
            out << "Log Levels: INFO, DEBUG, WARNING, ERROR, FATAL\n";
            out << "Status: SUCCESS\n";
            reportFile.close();
        }
    } else {
        writeLog("ERROR", QString("Failed to export logs to: %1").arg(targetLogPath));
    }
    
    // 重新打开日志文件继续记录
    if (logFile && !logFile->isOpen()) {
        logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    
    return success;
}

// 获取日志文件路径
QString Utils::getLogFilePath()
{
    return logFilePath;
}
