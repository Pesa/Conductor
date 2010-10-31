#include <QxtSignalGroup>

#include "probemanager.h"
#include "probeinterface.h"


ProbeManager::ProbeManager(QObject *parent) :
    QObject(parent),
    group(new QxtSignalGroup(this))
{
    addrs["Main room"] = "127.0.0.1"; // FIXME: for testing only!

    QHashIterator<QString, QString> i(addrs);
    while (i.hasNext()) {
        i.next();
        ProbeInterface *probe = new ProbeInterface(i.key(), i.value());
        probes[i.key()] = probe;
        group->addSignal(probe, SIGNAL(connected(QString)));
        connect(probe, SIGNAL(rssiChanged(QString,QString,int)), SLOT(rssiChanged(QString,QString,int)));
    }

    connect(group, SIGNAL(allSignalsReceived()), SIGNAL(ready()));
}

ProbeManager::~ProbeManager()
{
    qDeleteAll(probes);
    probes.clear();
}

void ProbeManager::connectToProbes()
{
    group->reset();
    foreach (ProbeInterface *i, probes)
        i->connect();
}

void ProbeManager::disconnectFromProbes()
{
    foreach (ProbeInterface *i, probes)
        i->disconnect();
}

void ProbeManager::startMonitoring(const QString &device)
{
    foreach (ProbeInterface *i, probes) {
        i->addDevice(device);
        i->startDiscovery();
    }
}

void ProbeManager::stopMonitoring(const QString &device)
{
    foreach (ProbeInterface *i, probes) {
        i->stopDiscovery();
        i->removeDevice(device);
    }
}

void ProbeManager::rssiChanged(const QString &probeName, const QString &device, int newRssi)
{
    qDebug("%s [%s] => %i", qPrintable(probeName), qPrintable(device), newRssi); // FIXME: for testing only!
    rssi[device][probeName] = newRssi;
}
