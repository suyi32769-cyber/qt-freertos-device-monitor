#pragma once
#include <QByteArray>
#include <QString>

struct Sample { quint32 sequence; double temperature; double voltage; int status; };
QByteArray makeFrame(const Sample &sample);
bool parseFrame(const QByteArray &line, Sample *out, QString *error);
