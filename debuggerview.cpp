#include "debuggerview.h"
#include "./ui_debuggerview.h"

#include <QDebug>

#include "userdb.h"

#include <QStandardItemModel>
#include <QXmlSimpleReader>
#include <QStandardPaths>
#include <driverinfo.h>
#define ERRMSG_SIZE    1024

DebuggerView::DebuggerView(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::DebuggerView)
{

    ui->setupUi(this);
    ui->statusbar->showMessage("Welcome to Ekos Debugger!");
    m_MountModel = new QStandardItemModel(this);

    QSettings settings;
    bool restartINDI = settings.value("indi/restart", true).toBool();
    ui->restartINDICB->setChecked(restartINDI);
    connect(ui->restartINDICB, &QCheckBox::toggled, [](bool checked)
    {
        QSettings settings;
        settings.setValue("indi/restart", checked);
    });

    bool restartKStars = settings.value("kstars/restart", true).toBool();
    ui->restartKStarsCB->setChecked(restartKStars);
    connect(ui->restartKStarsCB, &QCheckBox::toggled, [](bool checked)
    {
        QSettings settings;
        settings.setValue("kstars/restart", checked);
    });

    connect(ui->startKStarsB, &QPushButton::clicked, this, &DebuggerView::startKStars);
    connect(ui->stopKStarsB, &QPushButton::clicked, this, &DebuggerView::stopKStars);
    connect(ui->copyKStarsDebugLogB, &QPushButton::clicked, this, &DebuggerView::copyKStarsDebugLog);
    connect(ui->copyKStarsAppLogB, &QPushButton::clicked, this, &DebuggerView::copyKStarsAppLog);
    connect(ui->startINDIB, &QPushButton::clicked, this, &DebuggerView::startINDI);
    connect(ui->stopINDIB, &QPushButton::clicked, this, &DebuggerView::stopINDI);
    connect(ui->copyINDIDebugLogB, &QPushButton::clicked, this, &DebuggerView::copyINDIDebugLog);
    connect(ui->copyINDIAppLogB, &QPushButton::clicked, this, &DebuggerView::copyINDIAppLog);
    connect(ui->profileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DebuggerView::loadDriverCombo);
    connect(ui->driverCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DebuggerView::createINDIArgs);
    connect(ui->saveKStarsLogsB, &QPushButton::clicked, this, &DebuggerView::saveKStarsLogs);
    connect(ui->saveINDILogsB, &QPushButton::clicked, this, &DebuggerView::saveINDILogs);

    readXMLDrivers();
    loadProfiles();
}

DebuggerView::~DebuggerView()
{
    delete ui;
    if(m_KStarsProcess != nullptr)
        stopKStars();
    if(m_INDIProcess != nullptr)
        stopINDI();
}

void DebuggerView::startKStars()
{
    m_KStarsProcess = new QProcess();
    QStringList args;
    args << "-batch" << "-ex" << "run" << "-ex" << "bt" << "kstars";
    ui->startKStarsB->setDisabled(true);
    ui->stopKStarsB->setDisabled(false);
    ui->statusbar->showMessage("Started KStars.");

    //reads output and error process logs and connects them to their corresponding proccesing functions.
    connect(m_KStarsProcess, &QProcess::readyReadStandardOutput, this, &DebuggerView::processKStarsOutput);
    connect(m_KStarsProcess, &QProcess::readyReadStandardError, this, &DebuggerView::processKStarsError);
    //catches if KStars process has exited or crashed.
    connect(m_KStarsProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [ = ](int exitCode, QProcess::ExitStatus exitStatus)
    {
        Q_UNUSED(exitCode);
        if (ui->stopKStarsB->isEnabled())
        {
            ui->startKStarsB->setDisabled(false);
            ui->stopKStarsB->setDisabled(true);
            if(exitStatus == QProcess::CrashExit)
            {
                ui->statusbar->showMessage("KStars crashed.");
                if(ui->restartKStarsCB->isChecked())
                    startKStars();
            }
            else
                ui->statusbar->showMessage("KStars exited normally.");

        }
    });

    m_KStarsProcess->start("gdb", args);
}

void DebuggerView::stopKStars()
{
    m_KStarsProcess->terminate();
    ui->startKStarsB->setDisabled(false);
    ui->stopKStarsB->setDisabled(true);
    ui->statusbar->showMessage("Stopped KStars.");
}

void DebuggerView::processKStarsOutput()
{
    QString buffer = m_KStarsProcess->readAllStandardOutput();
    buffer = buffer.trimmed();
    ui->KStarsDebugLog->append(buffer);
}

void DebuggerView::processKStarsError()
{
    QString buffer = m_KStarsProcess->readAllStandardError();
    buffer = buffer.trimmed();
    ui->KStarsAppLog->append(buffer);
}

void DebuggerView::copyKStarsDebugLog()
{
    ui->KStarsDebugLog->selectAll();
    ui->KStarsDebugLog->copy();
    ui->statusbar->showMessage("Copied KStars Debug log.");
}

void DebuggerView::copyKStarsAppLog()
{
    ui->KStarsAppLog->selectAll();
    ui->KStarsAppLog->copy();
    ui->statusbar->showMessage("Copied KStars Application log.");
}

void DebuggerView::startINDI()
{
    m_INDIProcess = new QProcess();
    QStringList args;
    args << "-batch" << "-ex" << "set follow-fork-mode child" << "-ex" << "bt" << "--args"
         << "indiserver" << "-r" << "0" << "-v" << INDIArgs;
    //    gdb -batch -ex "set follow-fork-mode child" -ex "run" -ex "bt" --args indiserver -r 0 -v
    ui->startINDIB->setDisabled(true);
    ui->stopINDIB->setDisabled(false);
    ui->statusbar->showMessage("Started INDI.");

    connect(m_INDIProcess, &QProcess::readyReadStandardOutput, this, &DebuggerView::processINDIOutput);
    connect(m_INDIProcess, &QProcess::readyReadStandardError, this, &DebuggerView::processINDIError);
    connect(m_INDIProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            [ = ](int exitCode, QProcess::ExitStatus exitStatus)
    {
        Q_UNUSED(exitCode);
        if (ui->stopINDIB->isEnabled())
        {
            ui->startINDIB->setDisabled(false);
            ui->stopINDIB->setDisabled(true);
            if(exitStatus == QProcess::CrashExit)
            {
                ui->statusbar->showMessage("INDI crashed.");
                if(ui->restartINDICB->isChecked())
                    startINDI();
            }
            else
                ui->statusbar->showMessage("INDI exited normally.");
        }
    });

    m_INDIProcess->start("gdb", args);
}

void DebuggerView::stopINDI()
{
    m_INDIProcess->terminate();
    ui->startINDIB->setDisabled(false);
    ui->stopINDIB->setDisabled(true);
    ui->statusbar->showMessage("Stopped INDI.");
}

void DebuggerView::processINDIOutput()
{
    QString buffer = m_INDIProcess->readAllStandardOutput();
    buffer = buffer.trimmed();
    ui->INDIDebugLog->append(buffer);
}

void DebuggerView::processINDIError()
{
    QString buffer = m_INDIProcess->readAllStandardError();
    buffer = buffer.trimmed();
    ui->INDIAppLog->append(buffer);
}

void DebuggerView::copyINDIDebugLog()
{
    ui->INDIDebugLog->selectAll();
    ui->INDIDebugLog->copy();
    ui->statusbar->showMessage("Copied INDI Debug log.");
}

void DebuggerView::copyINDIAppLog()
{
    ui->INDIAppLog->selectAll();
    ui->INDIAppLog->copy();
    ui->statusbar->showMessage("Copied INDI Application log.");
}

//from manager.cpp
void DebuggerView::loadProfiles()
{
    profiles.clear();
    ProfileDB::GetAllProfiles(profiles);

    for (auto &pi : profiles)
    {
        qDebug() << pi->name;
        ui->profileCombo->addItem(pi->name);
    }
}

void DebuggerView::loadDriverCombo()
{
    currProfile = nullptr;

    ui->driverCombo->clear();

    // Get current profile
    for (auto &pi : profiles)
    {
        if (ui->profileCombo->currentText() == pi->name)
        {
            currProfile = pi.get();
            break;
        }
    }
    setPi(currProfile);
}

void DebuggerView::createINDIArgs()
{
    if (ui->driverCombo->currentText() == nullptr)
        return;

    INDIArgs.clear();

    for (auto &grp : driverslist->m_groups)
    {
        for(auto &dev : grp->m_devices)
        {
            if (ui->driverCombo->currentText() == dev[0])
            {
                INDIArgs.append(dev[1]);
                break;
            }
        }
    }
    for(auto &driver : currProfile->drivers)
    {
        if (ui->driverCombo->currentText() == driver)
            continue;
        for (auto &grp : driverslist->m_groups)
        {
            for(auto &dev : grp->m_devices)
            {
                if (driver == dev[0])
                {
                    INDIArgs.append(dev[1]);
                    break;
                }
            }
        }

    }
}

void DebuggerView::setPi(ProfileInfo * value)
{
    pi = value;

    QMapIterator<QString, QString> i(pi->drivers);

    while (i.hasNext())
    {
        i.next();

        QString key   = i.key();
        QString value = i.value();
        ui->driverCombo->addItem(value);

    }

}

bool DebuggerView::readXMLDrivers()
{
    // TODO fix it for MacOS
    QDir indiDir("/usr/share/indi");
    QString driverName;

    indiDir.setNameFilters(QStringList() << "indi_*.xml" << "drivers.xml");
    indiDir.setFilter(QDir::Files | QDir::NoSymLinks);
    QFileInfoList list = indiDir.entryInfoList();

    for (auto &fileInfo : list)
    {
        if (fileInfo.fileName().endsWith(QLatin1String("_sk.xml")))
            continue;
        readXMLDriverList(fileInfo.absoluteFilePath());
    }

    return true;
}

void DebuggerView::readXMLDriverList(const QString &driversFile)
{
    QFile file(driversFile);
    if(!file.open(QFile::ReadOnly | QFile::Text))
    {
        qDebug() << "Cannot read file" << file.errorString();
        return;
    }


    XmlDriversListReader xmlReader(driverslist);
    //    xmlReader.read(&file);

    if (!xmlReader.read(&file))
        qDebug() << "Parse error in file " << xmlReader.errorString();
    else
        driverslist->print();
}

void DebuggerView::saveKStarsLogs()
{
    QSettings settings;
    QString homePath = settings.value("kstars/savePath", QDir::homePath()).toString();

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-ddThh-mm-ss");
    QString filepath;
    filepath = QFileDialog::getExistingDirectory(this, "Save KStars logs", homePath, QFileDialog::ShowDirsOnly);
    qDebug() << filepath;
    if(!filepath.isEmpty() && !filepath.isNull())
    {
        settings.setValue("kstars/savePath", filepath);
        QString debugLog = ui->KStarsDebugLog->toPlainText();
        QString appLog = ui->KStarsAppLog->toPlainText();

        QString debuglogtxt = filepath + "/kstars_debug_log_" + timestamp + ".txt";
        QFile debugfile( debuglogtxt );
        if ( debugfile.open(QIODevice::ReadWrite) )
        {
            QTextStream stream( &debugfile );
            stream << debugLog << endl;
        }
        debugfile.close();

        QString applogtxt = filepath + "/kstars_app_log_" + timestamp + ".txt";
        QFile appfile( applogtxt );
        if ( appfile.open(QIODevice::ReadWrite) )
        {
            QTextStream stream( &appfile );
            stream << appLog << endl;
        }
        appfile.close();

        ui->statusbar->showMessage("Saved KStars logs.");
    }

    //    settings.setValue("kstars/savePath", filepath);
    //    settings.setValue("indi/savePath", 68);


}

void DebuggerView::saveINDILogs()
{
    QSettings settings;
    QString homePath = settings.value("indi/savePath", QDir::homePath()).toString();
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-ddThh-mm-ss");
    QString filepath;
    filepath = QFileDialog::getExistingDirectory(this, "Save INDI logs", homePath, QFileDialog::ShowDirsOnly);
    qDebug() << filepath;
    if(!filepath.isEmpty() && !filepath.isNull())
    {
        settings.setValue("indi/savePath", filepath);

        QString debugLog = ui->INDIDebugLog->toPlainText();
        QString appLog = ui->INDIAppLog->toPlainText();

        QString debuglogtxt = filepath + "/indi_debug_log_" + timestamp + ".txt";
        QFile debugfile( debuglogtxt );
        if ( debugfile.open(QIODevice::ReadWrite) )
        {
            QTextStream stream( &debugfile );
            stream << debugLog << endl;
        }
        debugfile.close();

        QString applogtxt = filepath + "/indi_app_log_" + timestamp + ".txt";
        QFile appfile( applogtxt );
        if ( appfile.open(QIODevice::ReadWrite) )
        {
            QTextStream stream( &appfile );
            stream << appLog << endl;
        }
        appfile.close();

        ui->statusbar->showMessage("Saved INDI logs.");
    }

    //    settings.setValue("indi/savePath", filepath);


}
//bool DebuggerView::readINDIHosts()
//{
//    QString indiFile("indihosts.xml");
//    //QFile localeFile;
//    QFile file;
//    char errmsg[1024];
//    char c;
//    LilXML *xmlParser = newLilXML();
//    XMLEle *root      = nullptr;
//    XMLAtt *ap        = nullptr;
//    QString hName, hHost, hPort;

//    lastGroup = nullptr;
//    file.setFileName(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + indiFile);
//    //    file.setFileName(KSPaths::locate(QStandardPaths::GenericDataLocation, indiFile));
//    if (file.fileName().isEmpty() || !file.open(QIODevice::ReadOnly))
//    {
//        delLilXML(xmlParser);
//        return false;
//    }

//    while (file.getChar(&c))
//    {
//        root = readXMLEle(xmlParser, c, errmsg);

//        if (root)
//        {
//            // Get host name
//            ap = findXMLAtt(root, "name");
//            if (!ap)
//            {
//                delLilXML(xmlParser);
//                return false;
//            }

//            hName = QString(valuXMLAtt(ap));

//            // Get host name
//            ap = findXMLAtt(root, "hostname");

//            if (!ap)
//            {
//                delLilXML(xmlParser);
//                return false;
//            }

//            hHost = QString(valuXMLAtt(ap));

//            ap = findXMLAtt(root, "port");

//            if (!ap)
//            {
//                delLilXML(xmlParser);
//                return false;
//            }

//            hPort = QString(valuXMLAtt(ap));

//            DriverInfo *dv = new DriverInfo(hName);
//            dv->setHostParameters(hHost, hPort);
//            dv->setDriverSource(HOST_SOURCE);

//            connect(dv, SIGNAL(deviceStateChanged(DriverInfo*)), this, SLOT(processDeviceStatus(DriverInfo*)));

//            driversList.append(dv);

//            //            QTreeWidgetItem *item = new QTreeWidgetItem(ui->clientTreeWidget, lastGroup);
//            //            lastGroup             = item;
//            //            item->setIcon(HOST_STATUS_COLUMN, ui->disconnected);
//            //            item->setText(HOST_NAME_COLUMN, hName);
//            //            item->setText(HOST_PORT_COLUMN, hPort);

//            delXMLEle(root);
//        }
//        else if (errmsg[0])
//        {
//            qDebug() << errmsg;
//            delLilXML(xmlParser);
//            return false;
//        }
//    }

//    delLilXML(xmlParser);

//    return true;
//}
