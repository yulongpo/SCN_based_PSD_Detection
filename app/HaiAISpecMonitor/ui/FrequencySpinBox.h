#pragma once

#include <QDoubleSpinBox>
#include <QMetaType>
#include <QValidator>
#include <QVariant>

#include "common/Frequency.h"

#include <cstdint>
#include <charconv>
#include <string>

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
        setDecimals(0);
        setSingleStep(1.0);
    }

    [[nodiscard]] std::int64_t frequencyHz() const noexcept
    {
        std::int64_t result = 0;
        (void)scn::common::toIntegerHz(value(), result);
        return result;
    }

    void setFrequencyHz(std::int64_t hz)
    {
        setValue(static_cast<double>(hz));
    }

    static bool parseFrequencyText(const QString& text, std::int64_t& result)
    {
        const QByteArray utf8 = text.toUtf8();
        return scn::common::parseFrequencyHz(
            std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())), result);
    }

    static bool parseStoredFrequency(const QVariant& stored, std::int64_t& result)
    {
        const int typeId = stored.metaType().id();
        if (typeId == QMetaType::Int || typeId == QMetaType::UInt ||
            typeId == QMetaType::LongLong || typeId == QMetaType::ULongLong) {
            bool ok = false;
            const qlonglong exact = stored.toLongLong(&ok);
            if (ok) {
                result = static_cast<std::int64_t>(exact);
                return true;
            }
            return false;
        }
        if (typeId == QMetaType::QString) {
            const QByteArray bytes = stored.toString().toLatin1();
            const char* begin = bytes.constData();
            const char* end = begin + bytes.size();
            const auto parsed = std::from_chars(begin, end, result);
            if (parsed.ec == std::errc{} && parsed.ptr == end) return true;
        }
        return scn::common::toIntegerHz(stored.toDouble(), result);
    }

    static QString formatFrequency(std::int64_t hz)
    {
        return QString::fromStdString(scn::common::formatFrequencyHz(hz));
    }

    static QString formatFrequency(double hz, int = 0)
    {
        return QString::fromStdString(scn::common::formatFrequencyHz(hz));
    }

protected:
    QString textFromValue(double value) const override
    {
        return formatFrequency(value);
    }

    double valueFromText(const QString& text) const override
    {
        std::int64_t result = 0;
        return parseFrequencyText(text, result) ? static_cast<double>(result) : QDoubleSpinBox::value();
    }

    QValidator::State validate(QString& input, int& position) const override
    {
        Q_UNUSED(position)
        if (input.trimmed().isEmpty()) return QValidator::Intermediate;

        std::int64_t result = 0;
        if (!parseFrequencyText(input, result)) return QValidator::Invalid;
        return result >= minimum() && result <= maximum()
            ? QValidator::Acceptable : QValidator::Invalid;
    }
};

} // namespace scn::app
