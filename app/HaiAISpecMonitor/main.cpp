#include "ui/MainWindow.h"
#include "ui/StartupWindow.h"

#include "../../application/MonitoringSession.h"
#include "../../application/PresentationModel.h"

#include <QApplication>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("SCN"));
    QApplication::setOrganizationDomain(QStringLiteral("scn.local"));
    QApplication::setApplicationName(QStringLiteral("HaiAISpecMonitor"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    scn::application::MonitoringSession session;
    scn::application::PresentationModel presentationModel;
    scn::app::MainWindow window(session, presentationModel);

    scn::app::StartupWindow startup;
    QObject::connect(&startup, &scn::app::StartupWindow::startupFinished,
                     &window, [&startup, &window] {
        startup.close();
        window.show();
        window.raise();
        window.activateWindow();
    });
    startup.show();
    return application.exec();
}
