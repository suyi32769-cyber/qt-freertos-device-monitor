#include "protocol.h"
#include <QList>
#include <cmath>

QByteArray makeFrame(const Sample &s) {
    QByteArray body = "DATA," + QByteArray::number(s.sequence) + ',' + QByteArray::number(s.temperature, 'f', 2)
        + ',' + QByteArray::number(s.voltage, 'f', 2) + ',' + QByteArray::number(s.status);
    quint8 sum = 0; for (char c : body) sum ^= quint8(c);
    return "$" + body + "*" + QByteArray::number(sum, 16).rightJustified(2, '0').toUpper() + "\n";
}

bool parseFrame(const QByteArray &input, Sample *out, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    QByteArray line = input; if (line.endsWith('\n')) line.chop(1); if (line.endsWith('\r')) line.chop(1);
    if (line.size() > 128 || !line.startsWith('$')) return fail(QStringLiteral("帧格式错误"));
    int star = line.indexOf('*');
    if (star < 0 || star != line.lastIndexOf('*') || star != line.size() - 3) return fail(QStringLiteral("校验字段格式错误"));
    QByteArray hex = line.mid(star + 1);
    for (char c : hex) if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) return fail(QStringLiteral("校验字段格式错误"));
    bool ok = false; uint expected = hex.toUInt(&ok, 16); quint8 actual = 0;
    QByteArray body = line.mid(1, star - 1); for (char c : body) actual ^= quint8(c);
    if (!ok || expected != actual) return fail(QStringLiteral("校验失败"));
    auto fields = body.split(','); if (fields.size() != 5 || fields[0] != "DATA") return fail(QStringLiteral("字段错误"));
    bool a, b, c, d; quint64 seq = fields[1].toULongLong(&a); double temp = fields[2].toDouble(&b); double volt = fields[3].toDouble(&c); int status = fields[4].toInt(&d);
    if (!a || seq > 0xffffffffULL || !b || !c || !d || !std::isfinite(temp) || !std::isfinite(volt) || temp < -100 || temp > 200 || volt < 0 || volt > 100 || (status != 0 && status != 1)) return fail(QStringLiteral("字段超出范围"));
    *out = {quint32(seq), temp, volt, status}; return true;
}
