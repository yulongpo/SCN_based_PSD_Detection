#include "DetectionLabWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("HaiAI"));
    QCoreApplication::setApplicationName(QStringLiteral("DetectionLabUI"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    scn::lab::DetectionLabWindow window;
    window.show();
    return application.exec();
}
