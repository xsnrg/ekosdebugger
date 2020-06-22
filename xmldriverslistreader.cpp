#include "xmldriverslistreader.h"

XmlDriversListReader::XmlDriversListReader(DriversList* driverslist)
{
    m_driverslist = driverslist;
}

bool XmlDriversListReader::read(QIODevice *device)
{
    reader.setDevice(device);

    if (reader.readNextStartElement())
    {
        if (reader.name() == "driversList")
            readDriversList();
        else
            reader.raiseError(QObject::tr("Not a drivers file"));
    }
    return !reader.error();
}

void XmlDriversListReader::readDriversList()
{
    while(reader.readNextStartElement())
    {
        if(reader.name() == "devGroup")
            readGroup();
        else
            reader.skipCurrentElement();
    }
}

void XmlDriversListReader::readGroup()
{
    Q_ASSERT(reader.isStartElement() &&
             reader.name() == "devGroup");

    Group* grp = new Group;

    readGroupName(grp);
    readDeviceGroup(grp);

    m_driverslist->addGroup(grp);
}

void XmlDriversListReader::readGroupName(Group *group)
{
    Q_ASSERT(reader.name() == "devGroup" &&
             reader.attributes().hasAttribute("group"));

    QString groupName =
        reader.attributes().value("group").toString();
    group->setGroup(groupName);
}

void XmlDriversListReader::readDeviceGroup(Group *group)
{
    Q_ASSERT(reader.isStartElement() &&
             reader.name() == "devGroup");

    QList<QStringList> devicesList;
    while(reader.readNextStartElement())
    {
        if(reader.name() == "device")
        {
            QString devLabel =
                reader.attributes().value("label").toString();
            QString devExec = readDeviceExec();
            devicesList.append(QList<QString> {devLabel, devExec});
        }
        else
            reader.skipCurrentElement();
    }
    group->setDevices(devicesList);
}

QString XmlDriversListReader::readDeviceExec()
{
    Q_ASSERT(reader.isStartElement() &&
             reader.name() == "device");

    QString devExec = "";

    while(reader.readNextStartElement())
    {
        if(reader.name() == "driver")
        {
            devExec = reader.readElementText();
        }
        else
            reader.skipCurrentElement();
    }
    return devExec;
}
