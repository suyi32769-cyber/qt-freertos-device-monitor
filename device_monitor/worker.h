#pragma once
#include "protocol.h"
#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <QTcpSocket>

class Worker : public QObject {
    Q_OBJECT
public slots:
    void startSimulation();
    void openPort(const QString &name, int baud);
    void openController();
    void sendCommand(const QString &command);
    void stop();
signals:
    void sampleReady(quint32 sequence, double temperature, double voltage, int status);
    void stateChanged(const QString &state, bool connected);
    void invalidFrame(const QString &reason);
    void deviceMessage(const QString &message);
private slots:
    void simulate();
    void readSerial();
private:
    void consume(const QByteArray &bytes);
    QSerialPort *port = nullptr;
    QTcpSocket *socket = nullptr;
    QTimer *timer = nullptr;
    QByteArray buffer;
    bool discarding = false;
    quint32 sequence = 0;
};
