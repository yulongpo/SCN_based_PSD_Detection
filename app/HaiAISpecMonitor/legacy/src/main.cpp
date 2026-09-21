#include <QApplication>
#include <QIcon>
#include <QDebug>
#include <QDir>
#include <QTextCodec>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "customwindow.h"
#include "monitor/SystemMonitor.h"
#include "controller/Controller.h"
#include "initialization.h"

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // 设置控制台代码页为 UTF-8（65001），保证从外部控制台（如 cmd）启动时中文输出不乱码
    SetConsoleOutputCP(CP_UTF8);                   // 控制台输出代码页设为 UTF-8（65001）
    SetConsoleCP(CP_UTF8);                         // 控制台输入代码页设为 UTF-8（65001）
#endif

    // Qt5 下 qDebug()/QString 输出按 UTF-8 编码，与上方控制台代码页匹配，保证中文不乱码
    //QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));

    // Keep Qt5's legacy local-8-bit conversions aligned with the UTF-8
    // encoding used by the source files, configuration and runtime messages.
    if (QTextCodec *utf8Codec = QTextCodec::codecForName("UTF-8")) {
        QTextCodec::setCodecForLocale(utf8Codec);
    }

    // These attributes must be set before QApplication creates QCoreApplication.
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!QDir::setCurrent(appDir)) {
        qCritical() << "Failed to set working directory:" << appDir;
        return -1;
    }

    qRegisterMetaType<radioai::core::EComState>("radioai::core::EComState");
    // 启动系统资源监控后台线程（CPU / GPU / RAM）
    SystemMonitor::instance().start();

    Controller controller;
    InitThread initThread(&controller);
    ProcessController processController(&controller);
    QObject::connect(&initThread, &InitThread::sendCurrentProcess, &processController, &ProcessController::currentProcessChanged, Qt::QueuedConnection);
    QObject::connect(&controller, &Controller::sendCurrentProcess, &processController, &ProcessController::currentProcessChanged, Qt::QueuedConnection);
    initThread.start();

    int ret = controller.exec(app);

    QVector<radioai::core::IComFlow*>& comflows = initThread.getComFlows();
    for (radioai::core::IComFlow* comFlow : comflows)
    {
        comFlow->stop();
        destoryComFlow(comFlow);
    }
    return ret;
}
