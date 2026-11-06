#include "worker.h"
#include <QHostAddress>
#include <cmath>

void Worker::stop() {
    if (timer) { timer->stop(); delete timer; timer = nullptr; }
    if (port) { QSerialPort *old = port; port = nullptr; old->disconnect(this); old->close(); old->deleteLater(); }
    if (socket) { QTcpSocket *old = socket; socket = nullptr; old->disconnect(this); old->abort(); old->deleteLater(); }
    buffer.clear(); discarding = false; emit stateChanged(QStringLiteral("已断开"), false);
}
void Worker::startSimulation() {
    stop(); sequence = 0; timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Worker::simulate); timer->start(250);
    emit stateChanged(QStringLiteral("模拟设备已连接"), true); simulate();
}
void Worker::openPort(const QString &name, int baud) {
    stop(); port = new QSerialPort(this); port->setPortName(name); port->setBaudRate(baud);
    port->setDataBits(QSerialPort::Data8); port->setParity(QSerialPort::NoParity); port->setStopBits(QSerialPort::OneStop); port->setFlowControl(QSerialPort::NoFlowControl);
    connect(port, &QSerialPort::readyRead, this, &Worker::readSerial);
    if (!port->open(QIODevice::ReadWrite)) { QString message = port->errorString(); stop(); emit stateChanged(QStringLiteral("打开失败：") + message, false); return; }
    connect(port, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError e) {
        if (e == QSerialPort::ResourceError || e == QSerialPort::ReadError || e == QSerialPort::DeviceNotFoundError || e == QSerialPort::PermissionError) {
            QString message = port->errorString(); stop(); emit stateChanged(QStringLiteral("串口错误：") + message, false);
        }
    });
    emit stateChanged(QStringLiteral("串口已连接：") + name, true);
}
void Worker::openController() {
    stop(); socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, this, [this] { consume(socket->readAll()); });
    connect(socket, &QTcpSocket::connected, this, [this] { emit stateChanged(QStringLiteral("FreeRTOS 模拟设备已连接"), true); });
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error), this, [this](QAbstractSocket::SocketError) { QString message = socket->errorString(); stop(); emit stateChanged(QStringLiteral("连接错误：") + message, false); });
    socket->connectToHost(QHostAddress::LocalHost, 47653);
}
void Worker::sendCommand(const QString &command) {
    if (command != "FAULT" && command != "RESET") return;
    QByteArray body = "CMD," + command.toLatin1(); quint8 sum = 0; for (char c : body) sum ^= quint8(c);
    QByteArray frame = "$" + body + "*" + QByteArray::number(sum,16).rightJustified(2,'0').toUpper() + "\n";
    if (socket && socket->state() == QAbstractSocket::ConnectedState) socket->write(frame);
    else if (port && port->isOpen()) port->write(frame);
    else emit deviceMessage(QStringLiteral("未连接可控制的设备"));
}
void Worker::simulate() {
    double t = sequence / 12.0; Sample s{sequence++, 35.0 + 16.0 * std::sin(t), 3.30 + 0.15 * std::sin(t / 2), 0};
    consume(makeFrame(s));
}
void Worker::readSerial() { consume(port->readAll()); }
void Worker::consume(const QByteArray &bytes) {
    for (char c : bytes) {
        if (discarding) { if (c == '\n') discarding = false; continue; }
        buffer.append(c);
        if (buffer.size() > 130) { buffer.clear(); discarding = true; emit invalidFrame(QStringLiteral("帧过长")); continue; }
        if (c == '\n') { Sample sample; QString error;
            if (buffer.startsWith("$STATE,") || buffer.startsWith("$ACK,")) {
                QByteArray line=buffer.trimmed(); int star=line.indexOf('*'); quint8 sum=0; bool ok=false;
                if (star > 1 && star == line.size()-3) { for (int i=1;i<star;++i) sum ^= quint8(line[i]); uint expected=line.mid(star+1).toUInt(&ok,16); if (ok && expected == sum) emit deviceMessage(QString::fromLatin1(line.mid(1,star-1))); else emit invalidFrame(QStringLiteral("状态帧校验失败")); }
                else emit invalidFrame(QStringLiteral("状态帧格式错误"));
            } else if (parseFrame(buffer, &sample, &error)) emit sampleReady(sample.sequence, sample.temperature, sample.voltage, sample.status); else emit invalidFrame(error); buffer.clear(); }
    }
}
