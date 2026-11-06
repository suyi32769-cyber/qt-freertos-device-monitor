#include "protocol.h"
#include "worker.h"
#include <QtWidgets>
#include <QSerialPortInfo>
#include <QThread>
#include <QDateTime>
#include <QPainter>
#include <QTextStream>
#include <cmath>

class Chart : public QWidget {
public:
    Chart() { setMinimumHeight(260); }
    void add(double value) { values.append(value); if (values.size() > 120) values.removeFirst(); update(); }
    void clear() { values.clear(); update(); }
    int sampleCount() const { return values.size(); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(), QColor("#ffffff"));
        QRectF area = rect().adjusted(50, 20, -20, -38); p.setPen(QColor("#dce4ec"));
        for (int i = 0; i <= 4; ++i) { double y = area.bottom() - area.height() * i / 4; p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y)); p.setPen(QColor("#607286")); p.drawText(QRectF(0, y-10, 45, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(i*25)); p.setPen(QColor("#dce4ec")); }
        p.setPen(QColor("#607286")); p.drawText(QRectF(area.left(), area.bottom()+10, area.width(), 20), Qt::AlignCenter, QStringLiteral("最近 120 个采样点 · 温度 °C（显示范围 0–100）"));
        QPainterPath path; for (int i=0; i<values.size(); ++i) { double x = area.right() - (values.size()-1-i)*area.width()/119.0; double y=area.bottom()-qBound(0.0, values[i], 100.0)*area.height()/100.0; if (!i) path.moveTo(x,y); else path.lineTo(x,y); }
        p.setPen(QPen(QColor("#1976d2"), 2)); p.drawPath(path);
    }
private: QVector<double> values;
};

class Window : public QMainWindow {
    Q_OBJECT
public:
    Window() {
        setWindowTitle(QStringLiteral("设备数据监控上位机")); resize(1050, 720);
        QWidget *root = new QWidget; setCentralWidget(root); auto *layout = new QVBoxLayout(root);
        auto *title = new QLabel(QStringLiteral("设备数据监控上位机")); title->setStyleSheet("font-size:24px;font-weight:600;color:#193b5c"); layout->addWidget(title);
        auto *row = new QHBoxLayout; mode = new QComboBox; mode->addItems({QStringLiteral("本地模拟数据"), QStringLiteral("串口设备"), QStringLiteral("FreeRTOS 模拟设备")}); mode->setCurrentIndex(2); ports = new QComboBox; baud = new QComboBox; baud->addItems({"9600","19200","38400","57600","115200"}); baud->setCurrentText("115200");
        auto *refresh = new QPushButton(QStringLiteral("刷新串口")); connectButton = new QPushButton(QStringLiteral("连接"));
        row->addWidget(new QLabel(QStringLiteral("数据源"))); row->addWidget(mode); row->addWidget(new QLabel(QStringLiteral("串口"))); row->addWidget(ports); row->addWidget(refresh); row->addWidget(new QLabel(QStringLiteral("波特率"))); row->addWidget(baud); row->addWidget(connectButton); layout->addLayout(row);
        auto *cards = new QHBoxLayout; state = card(QStringLiteral("连接状态"), cards); deviceState = card(QStringLiteral("设备状态"), cards); temperature = card(QStringLiteral("温度"), cards); voltage = card(QStringLiteral("电压"), cards); alarm = card(QStringLiteral("报警状态"), cards); layout->addLayout(cards);
        auto *control = new QHBoxLayout; threshold = new QDoubleSpinBox; threshold->setRange(-100,200); threshold->setValue(45); threshold->setSuffix(QStringLiteral(" °C")); control->addWidget(new QLabel(QStringLiteral("温度报警阈值（≥）"))); control->addWidget(threshold); control->addStretch(); auto *clear = new QPushButton(QStringLiteral("清空曲线")); control->addWidget(clear); layout->addLayout(control);
        auto *actions = new QHBoxLayout; faultButton = new QPushButton(QStringLiteral("模拟传感器故障")); resetButton = new QPushButton(QStringLiteral("恢复传感器")); actions->addWidget(faultButton); actions->addWidget(resetButton); actions->addStretch(); layout->addLayout(actions);
        chart = new Chart; layout->addWidget(chart, 1); auto *logLabel = new QLabel(QStringLiteral("运行事件")); layout->addWidget(logLabel); events = new QPlainTextEdit; events->setReadOnly(true); events->setMaximumBlockCount(500); events->setMaximumHeight(140); layout->addWidget(events);
        statusBar()->showMessage(QStringLiteral("数据和事件自动保存至 logs 文件夹"));
        root->setStyleSheet("QWidget{font-family:'Microsoft YaHei';font-size:13px} QPushButton{padding:6px 12px} QComboBox,QDoubleSpinBox{padding:4px} QPlainTextEdit{background:#f7f9fc}");
        worker = new Worker; worker->moveToThread(&thread); connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(this, &Window::startSimulation, worker, &Worker::startSimulation); connect(this, &Window::openPort, worker, &Worker::openPort); connect(this, &Window::stopWorker, worker, &Worker::stop);
        connect(this, &Window::openController, worker, &Worker::openController); connect(this, &Window::sendCommand, worker, &Worker::sendCommand);
        connect(worker, &Worker::stateChanged, this, &Window::onState); connect(worker, &Worker::sampleReady, this, &Window::onSample); connect(worker, &Worker::invalidFrame, this, &Window::onInvalid);
        connect(worker, &Worker::deviceMessage, this, &Window::onDeviceMessage);
        connect(faultButton, &QPushButton::clicked, this, [this] { emit sendCommand("FAULT"); event(QStringLiteral("已发送故障模拟请求，等待设备确认")); });
        connect(resetButton, &QPushButton::clicked, this, [this] { emit sendCommand("RESET"); event(QStringLiteral("已发送恢复请求，等待设备确认")); });
        connect(refresh, &QPushButton::clicked, this, &Window::refreshPorts); connect(clear, &QPushButton::clicked, chart, &Chart::clear);
        connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Window::updateControls);
        connect(connectButton, &QPushButton::clicked, this, [this] { if (connected) { emit stopWorker(); } else if (mode->currentIndex() == 0) emit startSimulation(); else if (mode->currentIndex() == 2) emit openController(); else if (ports->currentText().isEmpty()) event(QStringLiteral("没有可用串口")); else emit openPort(ports->currentData().toString(), baud->currentText().toInt()); });
        refreshPorts(); updateControls(); state->setText(QStringLiteral("未连接")); deviceState->setText("--"); temperature->setText("--"); voltage->setText("--"); alarm->setText(QStringLiteral("--"));
        QDir dir(QCoreApplication::applicationDirPath()); if (dir.mkpath("logs")) { logFile.setFileName(dir.filePath("logs/monitor_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + ".csv")); if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) { logFile.write("\xEF\xBB\xBFtime,type,sequence,temperature_c,voltage_v,status,message\n"); logFile.flush(); } }
        if (!logFile.isOpen()) { statusBar()->showMessage(QStringLiteral("日志无法写入：请检查程序目录权限")); event(QStringLiteral("日志文件创建失败")); } else event(QStringLiteral("日志已创建：") + logFile.fileName());
        thread.start();
    }
    ~Window() override { if (thread.isRunning()) { QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection); thread.quit(); thread.wait(); } }
    void integrationTest() {
        emit startSimulation();
        QTimer::singleShot(4000, this, [this] {
            bool ok = connected && chart->sampleCount() >= 3 && temperature->text() != "--" && alarmActive && logFile.isOpen();
            logFile.flush(); QFile f(logFile.fileName());
            ok = ok && f.open(QIODevice::ReadOnly);
            QByteArray contents = f.readAll();
            ok = ok && contents.contains(",sample,") && contents.contains(QStringLiteral("报警触发").toUtf8());
            emit stopWorker(); QCoreApplication::exit(ok ? 0 : 4);
        });
    }
    void controllerTest() {
        controllerTestMode = true; threshold->setValue(100); emit openController();
        QTimer::singleShot(1800, this, [this] { emit sendCommand("FAULT"); });
        QTimer::singleShot(12000, this, [this] {
            logFile.flush(); QFile f(logFile.fileName()); bool ok = connected && chart->sampleCount() >= 5 && sawAck && sawFault && sawRecovering && sawRunningAfterFault && f.open(QIODevice::ReadOnly);
            QByteArray data=f.readAll(); ok = ok && data.contains(",sample,") && data.contains(QStringLiteral("设备状态：异常").toUtf8()) && data.contains(QStringLiteral("设备状态：恢复中").toUtf8());
            emit stopWorker(); QCoreApplication::exit(ok ? 0 : 5);
        });
    }
signals:
    void startSimulation(); void openPort(const QString &, int); void openController(); void sendCommand(const QString &); void stopWorker();
private:
    QLabel *card(const QString &label, QHBoxLayout *layout) { auto *box = new QGroupBox(label); auto *l = new QVBoxLayout(box); auto *value = new QLabel; value->setStyleSheet("font-size:20px;font-weight:600;color:#205b8e"); l->addWidget(value); layout->addWidget(box); return value; }
    void refreshPorts() { QString previous = ports->currentData().toString(); ports->clear(); for (const auto &p : QSerialPortInfo::availablePorts()) ports->addItem(p.portName() + (p.description().isEmpty() ? "" : " — " + p.description()), p.portName()); int index = ports->findData(previous); if (index >= 0) ports->setCurrentIndex(index); }
    void updateControls() { bool serial = mode->currentIndex() == 1 && !connected; ports->setEnabled(serial); baud->setEnabled(serial); mode->setEnabled(!connected); faultButton->setEnabled(connected && mode->currentIndex()!=0); resetButton->setEnabled(connected && mode->currentIndex()!=0); }
    void writeRow(const QString &type, const QString &seq, const QString &temp, const QString &volt, const QString &status, QString message) { if (!logFile.isOpen()) return; message.replace('"', "\"\""); QByteArray row = (QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + "," + type + "," + seq + "," + temp + "," + volt + "," + status + ",\"" + message + "\"\n").toUtf8(); if (logFile.write(row) != row.size() || !logFile.flush()) { logFile.close(); statusBar()->showMessage(QStringLiteral("日志写入失败")); events->appendPlainText(QStringLiteral("日志写入失败，请检查磁盘和权限")); } }
    void event(const QString &message) { events->appendPlainText(QTime::currentTime().toString("HH:mm:ss") + "  " + message); writeRow("event", "", "", "", "", message); }
    void onState(const QString &s, bool isConnected) { connected = isConnected; state->setText(s); connectButton->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接")); updateControls(); if (!connected) { temperature->setText("--"); voltage->setText("--"); deviceState->setText("--"); currentDeviceState.clear(); alarm->setText("--"); alarmActive = false; } event(s); }
    void setAlarm(bool active) { alarm->setText(active ? QStringLiteral("报警") : QStringLiteral("正常")); alarm->setStyleSheet(active ? "font-size:20px;font-weight:600;color:#cf3b36" : "font-size:20px;font-weight:600;color:#17865a"); if (active != alarmActive) { alarmActive = active; event(active ? QStringLiteral("报警触发：温度超限或设备报告异常") : QStringLiteral("报警已恢复")); } }
    void onSample(quint32 seq, double t, double v, int deviceStatus) { lastTemp=t; temperature->setText(QString::number(t,'f',2) + " °C"); voltage->setText(QString::number(v,'f',2) + " V"); chart->add(t); setAlarm(t >= threshold->value() || deviceStatus != 0 || currentDeviceState == "FAULT" || currentDeviceState == "RECOVERING"); writeRow("sample", QString::number(seq), QString::number(t,'f',2), QString::number(v,'f',2), QString::number(deviceStatus), ""); }
    void onDeviceMessage(const QString &message) { if (message.startsWith("STATE,")) { QString next=message.mid(6); if (controllerTestMode) { if (next=="FAULT") sawFault=true; if (next=="RECOVERING" && sawFault) sawRecovering=true; if (next=="RUNNING" && sawRecovering) sawRunningAfterFault=true; } if (next != currentDeviceState) { currentDeviceState=next; QString label = next == "RUNNING" ? QStringLiteral("运行") : next == "FAULT" ? QStringLiteral("异常") : next == "RECOVERING" ? QStringLiteral("恢复中") : QStringLiteral("启动中"); deviceState->setText(label); event(QStringLiteral("设备状态：") + label); if (next == "FAULT" || next == "RECOVERING") setAlarm(true); else if (next == "RUNNING") setAlarm(lastTemp >= threshold->value()); } } else { if (message=="ACK,FAULT") sawAck=true; event(QStringLiteral("设备回复：") + message); } }
    void onInvalid(const QString &reason) { event(QStringLiteral("丢弃无效数据帧：") + reason); }
    QThread thread; Worker *worker; bool connected = false, alarmActive = false, controllerTestMode = false, sawAck = false, sawFault = false, sawRecovering = false, sawRunningAfterFault = false; double lastTemp = -100; QString currentDeviceState; QFile logFile;
    QComboBox *mode, *ports, *baud; QPushButton *connectButton, *faultButton, *resetButton; QLabel *state, *deviceState, *temperature, *voltage, *alarm; QDoubleSpinBox *threshold; QPlainTextEdit *events; Chart *chart;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (app.arguments().contains("--self-test")) {
        Sample source{42, 23.5, 3.3, 0}, parsed; QString error; QByteArray frame = makeFrame(source);
        if (!parseFrame(frame, &parsed, &error) || parsed.sequence != 42 || std::abs(parsed.temperature-23.5) > 0.001) return 1;
        QByteArray broken = frame; broken[4] = 'X'; if (parseFrame(broken, &parsed, &error)) return 2;
        QThread thread; Worker *worker = new Worker; worker->moveToThread(&thread); QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        int count = 0; QObject::connect(worker, &Worker::sampleReady, &app, [&](quint32, double, double, int) { if (++count >= 3) app.quit(); });
        thread.start(); QMetaObject::invokeMethod(worker, "startSimulation", Qt::QueuedConnection); QTimer::singleShot(3000, &app, &QCoreApplication::quit); app.exec();
        QMetaObject::invokeMethod(worker, "stop", Qt::BlockingQueuedConnection); thread.quit(); thread.wait(); return count >= 3 ? 0 : 3;
    }
    Window w; w.show(); if (app.arguments().contains("--integration-test")) w.integrationTest(); if (app.arguments().contains("--controller-test")) w.controllerTest(); return app.exec();
}
#include "main.moc"
