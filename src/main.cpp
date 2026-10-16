#include "application.h"
#include "mainwindow-dtk.h"
#include "utils/utils.h"

#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fstream>
#include <execinfo.h>

#include <DSysInfo>

#include <QDate>
#include <QProcessEnvironment>
#include <QSettings>
#include <QFile>
#include <QStandardPaths>
#include <QScreen>
#include <QSurfaceFormat>

#include <backend/DataCollectorAndUploader.h>
#include <backend/ThemeChecker.h>

DCORE_USE_NAMESPACE
DWIDGET_USE_NAMESPACE

static QString buildDateTime;


void gatherInfo(FILE *fp, std::ofstream& logFile, const char* description) {
    if (fp) {
        char buffer[512];
        logFile << description << ":\n";
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            buffer[strcspn(buffer, "\n")] = 0;
            logFile << buffer << "\n";
        }
        pclose(fp);
    } else {
        logFile << "Failed to gather " << description << " info.\n";
    }
}


void crashHandler(int sig) {
    void *array[50];
    size_t size = backtrace(array, 50);
    if (size == 0) {
        perror("backtrace");
        exit(1);
    }

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char filename[128];
    snprintf(filename, sizeof(filename), "/tmp/spark_store_crash_log_%04d%02d%02d_%02d%02d%02d.txt",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

    std::ofstream logFile(filename, std::ios::out);

    if (!logFile.is_open()) {
        perror("ofstream");
        exit(1);
    }

    logFile << "Please send this log to the developer. QQ Group: 872690351\n";
    logFile << "Gitee: https://gitee.com/spark-store-project/spark-store/issues\n";
    logFile << "Gihub: https://github.com/spark-store-project/spark-store/issues\n";
    logFile << "Build Date and Time: " << buildDateTime.toStdString() << "\n";
    gatherInfo(popen("cat ~/.config/spark-union/spark-store/config.ini", "r"), logFile, "User Config File");

    // Collecting System Information
    gatherInfo(popen("LANG=en_US.UTF-8 uname -m", "r"), logFile, "CPU Architecture");
    gatherInfo(popen("LANG=en_US.UTF-8 lsb_release -a", "r"), logFile, "Distribution info");
    gatherInfo(popen("LANG=en_US.UTF-8 lscpu", "r"), logFile, "All CPU Info");
    gatherInfo(popen("LANG=en_US.UTF-8 free -h | grep Mem | awk '{print $2}'", "r"), logFile, "Memory Size");



    logFile << "Error: signal " << sig << ":\n";
    for (size_t i = 0; i < size; i++) {
        char **strings = backtrace_symbols(&array[i], 1);
        if (strings != NULL && strings[0] != NULL) {
            logFile << strings[0] << "\n";
        } else {
            logFile << "Failed to get symbol.\n";
        }
        free(strings);
    }

    logFile.close();

    char openCmd[256];
    snprintf(openCmd, sizeof(openCmd), "xdg-open %s", filename);
    if (system(openCmd) == -1) {
        perror("system");
    }

    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);

    exit(1);
}

/**
 * @brief 打印一份 UI 环境快照：平台插件、默认字体、DPI、主题类型以及相关环境变量
 * @note 非 deepin 桌面（Kubuntu 等）上出现「字体不显示 / 主题错乱」时，
 *       先把这段日志贴出来，可以直接区分问题出在平台插件、平台主题、字体还是主题判断上。
 */
static void logUiEnvironment(const QGuiApplication &app)
{
    qDebug().noquote() << QStringLiteral(
                              "UI environment:\n"
                              "  platform plugin      : %1\n"
                              "  default font         : %2 (%3pt)\n"
                              "  logical dpi          : %4\n"
                              "  dark theme           : %5\n"
                              "  XDG_CURRENT_DESKTOP  : %6\n"
                              "  QT_QPA_PLATFORM      : %7\n"
                              "  QT_QPA_PLATFORMTHEME : %8")
                              .arg(app.platformName())
                              .arg(app.font().family())
                              .arg(app.font().pointSizeF())
                              .arg(app.primaryScreen() ? app.primaryScreen()->logicalDotsPerInch() : -1.0)
                              .arg(ThemeChecker::instance()->useDarkTheme() ? QStringLiteral("yes") : QStringLiteral("no"))
                              .arg(QString::fromUtf8(qgetenv("XDG_CURRENT_DESKTOP")))
                              .arg(QString::fromUtf8(qgetenv("QT_QPA_PLATFORM")))
                              .arg(QString::fromUtf8(qgetenv("QT_QPA_PLATFORMTHEME")));
}

int main(int argc, char *argv[])
{
    // 崩溃处理
    signal(SIGSEGV, crashHandler);  // 注册SIGSEGV处理函数
    
    // 初始化日志系统
    Utils::initLogger();
    Utils::writeLog("INFO", "Application starting...");

     // Get build time
     static const QDate buildDate = QLocale(QLocale::English).toDate(QString(__DATE__).replace("  ", " 0"), "MMM dd yyyy");
     static const QTime buildTime = QTime::fromString(__TIME__, "hh:mm:ss");
     buildDateTime = buildDate.toString("yyyy.MM.dd") + "-" + buildTime.toString("hh:mm:ss");
    
    //在cmakelist.txt中设置 buildDateTime
//    QString buildDateTime = QString("%1-%2").arg(QString(BUILD_DATE)).arg(QString(BUILD_TIME));
    Application::checkAppConfigLocation(); // 检查 ~/.config/spark-union/spark-store 文件夹是否存在

    // 初始化 config.ini 配置文件
    Utils::initConfig();

    // 回传版本信息，不涉及个人隐私
    DataCollectorAndUploader uploader;
    QObject::connect(&uploader, &DataCollectorAndUploader::uploadSuccessful, [](){
        qDebug() << "Data uploaded successfully";
        Utils::writeLog("INFO", "Data uploaded successfully");
    });
    QObject::connect(&uploader, &DataCollectorAndUploader::uploadFailed, [](QString error){
        qDebug() << "Upload failed with error: " << error;
        Utils::writeLog("ERROR", QString("Upload failed with error: %1").arg(error));
    });

    uploader.collectAndUploadData();

    // Set display backend
    Utils::setQPAPlatform();

    // 龙芯机器配置,使得 DApplication 能正确加载 QtWebEngine
    qputenv("DTK_FORCE_RASTER_WIDGETS", "FALSE");

    // 设置 QtWebEngine 环境变量
    QStringList chromium_flags;
    // 浏览器开启 GPU 支持
    // chromium_flags.append("--disable-features=UseModernMediaControls");
    // chromium_flags.append("--disable-web-security");
    // 全平台软件渲染Webkit
    chromium_flags.append("--disable-gpu");
#if defined __sw_64__ || __loongarch__
    chromium_flags.append("--no-sandbox");
#endif
    // 如果配置文件中设置了关闭沙箱，则添加no-sandbox标志
    if (Utils::shouldDisableWebEngineSandbox()) {
        chromium_flags.append("--no-sandbox");
    }
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", chromium_flags.join(" ").toUtf8());

    /**
     * NOTE: https://zhuanlan.zhihu.com/p/550285855
     * 避免 wayland 环境下从 QtWebEngine 后退回到 QWidget 时黑屏闪烁
     */
    if (Utils::isWayland()) {
        qputenv("QMLSCENE_DEVICE", "softwarecontext");
        DApplication::setAttribute(Qt::AA_UseSoftwareOpenGL);
    }

    /**
     * 平台主题：只在 deepin 系环境里强制使用 DTK 平台主题。
     *
     * 在 Kubuntu 等桌面上注入 "-platformtheme deepin" 会绕过桌面自己的 Qt 平台主题
     * （KDE 的 plasma-integration 正是通过它把系统字体、配色、图标主题交给 Qt 应用），
     * 结果就是：系统里设置的字体对商店失效、配色跟着 DTK 走，
     * 非 DDE 环境下主题判断还会退化成兜底值（进而出现深色/浅色错配、文字看不见）。
     */
    const bool useDeepinPlatformTheme = Utils::isDeepinFamily();
    const int extraArgs = useDeepinPlatformTheme ? 2 : 0;

    QVector<char *> fakeArgs(argc + extraArgs + 1); // 末尾再留一个 nullptr 作为 argv 终止符
    fakeArgs[0] = argv[0];
    if (useDeepinPlatformTheme)
    {
        fakeArgs[1] = const_cast<char *>("-platformtheme");
        fakeArgs[2] = const_cast<char *>("deepin");
    }
    for (int i = 1; i < argc; i++)
    {
        fakeArgs[i + extraArgs] = argv[i];
    }
    fakeArgs[argc + extraArgs] = nullptr;
    int fakeArgc = argc + extraArgs; // QCoreApplication 的 argc 要用引用，避免 c++ 编译器优化
    Application a(fakeArgc, fakeArgs.data());
    // 设置版本和构建时间
    a.setBuildDateTime(buildDateTime);

    // 打印一份 UI 环境快照：排查「非 deepin 桌面下字体/主题异常」时先看这段日志
    logUiEnvironment(a);

    // 限制单实例运行
    if (!a.setSingleInstance("spark-store"))
    {
        qWarning() << "Another instance has already started, activating...";
        return -1;
    }

    MainWindow w;
    a.setMainWindow(&w); // 设置应用程序主窗口，用于初始化关于对话框

    if (argc > 1)
    {
        QString arg1 = argv[1];
        if (arg1.trimmed().startsWith("spk://"))
        {
            w.openUrl(arg1);
        }
    }
    w.show();

    // 在程序结束前关闭日志文件 - 修复变量名并移到return前
    QObject::connect(&a, &QApplication::aboutToQuit, []() {
        Utils::writeLog("INFO", "Application shutting down");
    });
    
    return a.exec();
}
