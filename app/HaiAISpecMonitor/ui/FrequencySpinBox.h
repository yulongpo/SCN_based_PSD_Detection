#pragma once

#include <QDoubleSpinBox>
#include <QValidator>

#include <cmath>

namespace scn::app
{

/**
 * @brief ISA-style frequency editor.
 *
 * Values are stored in Hz. Input accepts Hz/kHz/MHz/GHz and the one-letter
 * forms H/K/M/G, without case sensitivity or with whitespace between the
 * number and unit. The displayed value is formatted using the most suitable
 * unit automatically.
 */
class FrequencySpinBox : public QDoubleSpinBox
{
public:
    explicit FrequencySpinBox(QWidget* parent = nullptr)
        : QDoubleSpinBox(parent)
    {
        setSuffix(QString());
    }

    static bool parseFrequencyText(const QString& text, double& result)
    {
        QString value = text.simplified();
        value.remove(QLatin1Char(' '));
        if (value.isEmpty()) return false;

        QString lower = value.toLower();
        double multiplier = 1.0;
        const auto stripSuffix = [&lower, &value](const QString& suffix) {
            lower.chop(suffix.size());
            value.chop(suffix.size());
        };

        if (lower.endsWith(QStringLiteral("ghz"))) {
            stripSuffix(QStringLiteral("ghz"));
            multiplier = 1e9;
        } else if (lower.endsWith(QStringLiteral("mhz"))) {
            stripSuffix(QStringLiteral("mhz"));
            multiplier = 1e6;
        } else if (lower.endsWith(QStringLiteral("khz"))) {
            stripSuffix(QStringLiteral("khz"));
            multiplier = 1e3;
        } else if (lower.endsWith(QStringLiteral("hz"))) {
            stripSuffix(QStringLiteral("hz"));
        } else if (lower.endsWith(QLatin1Char('g'))) {
            stripSuffix(QStringLiteral("g"));
            multiplier = 1e9;
        } else if (lower.endsWith(QLatin1Char('m'))) {
            stripSuffix(QStringLiteral("m"));
            multiplier = 1e6;
        } else if (lower.endsWith(QLatin1Char('k'))) {
            stripSuffix(QStringLiteral("k"));
            multiplier = 1e3;
        } else if (lower.endsWith(QLatin1Char('h'))) {
            stripSuffix(QStringLiteral("h"));
        }

        bool ok = false;
        const double numeric = value.toDouble(&ok);
        if (!ok || !std::isfinite(numeric)) return false;
        result = numeric * multiplier;
        return std::isfinite(result);
    }

    static QString formatFrequency(double hz, int decimals = 6)
    {
        const double absHz = std::abs(hz);
        if (absHz >= 1e9)
            return QStringLiteral("%1 GHz").arg(hz / 1e9, 0, 'f', decimals);
        if (absHz >= 1e6)
            return QStringLiteral("%1 MHz").arg(hz / 1e6, 0, 'f', decimals);
        if (absHz >= 1e3)
            return QStringLiteral("%1 kHz").arg(hz / 1e3, 0, 'f', decimals);
        return QStringLiteral("%1 Hz").arg(hz, 0, 'f', 0);
    }

protected:
    QString textFromValue(double value) const override
    {
        const double absHz = std::abs(value);
        if (absHz >= 1e9)
            return QStringLiteral("%1 GHz").arg(value / 1e9, 0, 'f', 9);
        if (absHz >= 1e6)
            return QStringLiteral("%1 MHz").arg(value / 1e6, 0, 'f', 6);
        if (absHz >= 1e3)
            return QStringLiteral("%1 kHz").arg(value / 1e3, 0, 'f', 3);
        return QStringLiteral("%1 Hz").arg(value, 0, 'f', 0);
    }

    double valueFromText(const QString& text) const override
    {
        double result = 0.0;
        return parseFrequencyText(text, result) ? result : QDoubleSpinBox::value();
    }

    QValidator::State validate(QString& input, int& position) const override
    {
        Q_UNUSED(position)
        if (input.trimmed().isEmpty()) return QValidator::Intermediate;

        double result = 0.0;
        if (!parseFrequencyText(input, result)) return QValidator::Invalid;
        return result >= minimum() && result <= maximum()
            ? QValidator::Acceptable : QValidator::Invalid;
    }
};

} // namespace scn::app
