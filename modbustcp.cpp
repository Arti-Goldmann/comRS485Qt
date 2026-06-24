#include "modbustcp.h"
#include <QDebug>

ModbusTCP::ModbusTCP(QObject *parent) : QObject(parent)
{
}

QByteArray ModbusTCP::buildFrame(uint16_t transactionId, uint8_t unitId, const QByteArray &pdu)
{
    QByteArray frame;

    // MBAP-заголовок
    frame.append(uint16ToBytes(transactionId));         // Transaction Identifier
    frame.append(uint16ToBytes(0));                     // Protocol Identifier (0 = Modbus)
    frame.append(uint16ToBytes(static_cast<uint16_t>(pdu.length() + 1))); // Length = UnitId + PDU
    frame.append(static_cast<char>(unitId));            // Unit Identifier

    // PDU
    frame.append(pdu);

    return frame;
}

QByteArray ModbusTCP::createReadRequest(const ReadRequest &request, uint16_t transactionId)
{
    QByteArray pdu;
    pdu.append(static_cast<char>(request.functionCode));
    pdu.append(uint16ToBytes(request.startAddress));
    pdu.append(uint16ToBytes(request.quantity));

    return buildFrame(transactionId, request.unitId, pdu);
}

QByteArray ModbusTCP::createWriteSingleRequest(const WriteSingleRequest &request, uint16_t transactionId)
{
    QByteArray pdu;
    pdu.append(static_cast<char>(request.functionCode));
    pdu.append(uint16ToBytes(request.address));
    pdu.append(uint16ToBytes(request.value));

    return buildFrame(transactionId, request.unitId, pdu);
}

QByteArray ModbusTCP::createWriteMultipleRequest(const WriteMultipleRequest &request, uint16_t transactionId)
{
    QByteArray pdu;
    pdu.append(static_cast<char>(request.functionCode));
    pdu.append(uint16ToBytes(request.startAddress));
    pdu.append(uint16ToBytes(request.quantity));

    if (request.functionCode == WriteMultipleCoils) {
        uint8_t byteCount = (request.quantity + 7) / 8;
        pdu.append(static_cast<char>(byteCount));
        pdu.append(request.data.left(byteCount));
    } else if (request.functionCode == WriteMultipleRegisters) {
        uint8_t byteCount = request.quantity * 2;
        pdu.append(static_cast<char>(byteCount));
        pdu.append(request.data.left(byteCount));
    }

    return buildFrame(transactionId, request.unitId, pdu);
}

bool ModbusTCP::validateResponse(const QByteArray &response)
{
    // Минимум: MBAP(7) + код функции(1)
    if (response.length() < MbapLength + 1) {
        return false;
    }

    // Protocol Identifier должен быть 0
    uint16_t protocolId = bytesToUint16(response, 2);
    if (protocolId != 0) {
        return false;
    }

    // Поле Length должно соответствовать фактической длине кадра.
    // Length считает байты после себя: UnitId(1) + PDU. Полный кадр = 6 + Length.
    uint16_t length = bytesToUint16(response, 4);
    return response.length() == 6 + length;
}

QString ModbusTCP::parseResponse(const QByteArray &response)
{
    if (response.length() < MbapLength + 1) {
        return QString("Неверная длина ответа (получено %1 байт)").arg(response.length());
    }
    if (!validateResponse(response)) {
        return QString("Некорректный MBAP-заголовок");
    }
    return parseFrame(response);
}

int ModbusTCP::detectFrameLength(const QByteArray &buffer, int offset)
{
    int n = buffer.length();

    // Для чтения поля Length нужны как минимум первые 6 байт MBAP-заголовка.
    if (offset + 6 > n) {
        return -1; // данных пока недостаточно даже для заголовка
    }

    // Protocol Identifier должен быть 0, иначе это не начало кадра Modbus TCP.
    uint16_t protocolId = bytesToUint16(buffer, offset + 2);
    if (protocolId != 0) {
        return 0;
    }

    uint16_t length = bytesToUint16(buffer, offset + 4);
    // Поле Length должно быть осмысленным: минимум UnitId(1) + код функции(1).
    if (length < 2) {
        return 0;
    }

    int total = 6 + length; // полная длина кадра
    if (offset + total > n) {
        return -1; // кадр пришёл не целиком
    }

    return total;
}

QList<QByteArray> ModbusTCP::splitFrames(const QByteArray &buffer, QByteArray &unrecognized)
{
    QList<QByteArray> frames;
    unrecognized.clear();

    int i = 0;
    int n = buffer.length();
    while (i < n) {
        int length = detectFrameLength(buffer, i);
        if (length > 0) {
            frames.append(buffer.mid(i, length));
            i += length;
        } else if (length < 0) {
            // Кадр валиден, но пришёл не целиком — сохраняем остаток как «хвост».
            unrecognized.append(buffer.mid(i));
            break;
        } else {
            // Не начало корректного кадра — пропускаем байт (ресинхронизация).
            unrecognized.append(buffer[i]);
            ++i;
        }
    }

    return frames;
}

QString ModbusTCP::parseFrame(const QByteArray &frame)
{
    if (frame.length() < MbapLength + 1) {
        return QString("Неверная длина кадра (%1 байт)").arg(frame.length());
    }
    if (!validateResponse(frame)) {
        return QString("Некорректный MBAP-заголовок");
    }

    uint16_t transactionId = bytesToUint16(frame, 0);
    uint8_t unitId = static_cast<uint8_t>(frame[6]);
    uint8_t functionCode = static_cast<uint8_t>(frame[7]);
    int length = frame.length();

    QString head = QString("TID: %1, Unit: %2, Функция: %3 (%4)")
                   .arg(transactionId)
                   .arg(unitId)
                   .arg(functionCode, 2, 16, QChar('0'))
                   .arg(functionCodeToString(static_cast<FunctionCode>(functionCode & 0x7F)));

    if (functionCode & 0x80) {
        uint8_t exceptionCode = static_cast<uint8_t>(frame[8]);
        return QString("[Ответ-ошибка] %1, Исключение: %2 (%3)")
               .arg(head)
               .arg(exceptionCode, 2, 16, QChar('0'))
               .arg(exceptionCodeToString(exceptionCode));
    }

    // Данные PDU начинаются сразу после кода функции (смещение 8).
    switch (functionCode) {
        case ReadCoils:
        case ReadDiscreteInputs:
        case ReadHoldingRegisters:
        case ReadInputRegisters: {
            if (length == 12) {
                // Запрос: начальный адрес + количество
                uint16_t startAddress = bytesToUint16(frame, 8);
                uint16_t quantity = bytesToUint16(frame, 10);
                return QString("[Запрос] %1, Начальный адрес: %2, Количество: %3")
                       .arg(head).arg(startAddress).arg(quantity);
            } else {
                // Ответ: счётчик байт + данные
                uint8_t byteCount = static_cast<uint8_t>(frame[8]);
                QByteArray payload = frame.mid(9, byteCount);
                return QString("[Ответ] %1, Байт данных: %2, Данные: %3")
                       .arg(head).arg(byteCount).arg(QString(payload.toHex(' ').toUpper()));
            }
        }
        case WriteSingleCoil:
        case WriteSingleRegister: {
            // Запрос и ответ идентичны (эхо)
            uint16_t address = bytesToUint16(frame, 8);
            uint16_t value = bytesToUint16(frame, 10);
            return QString("[Запрос/Ответ] %1, Адрес: %2, Значение: %3")
                   .arg(head).arg(address).arg(value);
        }
        case WriteMultipleCoils:
        case WriteMultipleRegisters: {
            if (length == 12) {
                // Ответ: начальный адрес + количество
                uint16_t startAddress = bytesToUint16(frame, 8);
                uint16_t quantity = bytesToUint16(frame, 10);
                return QString("[Ответ] %1, Начальный адрес: %2, Количество: %3")
                       .arg(head).arg(startAddress).arg(quantity);
            } else {
                // Запрос: начальный адрес + количество + счётчик байт + данные
                uint16_t startAddress = bytesToUint16(frame, 8);
                uint16_t quantity = bytesToUint16(frame, 10);
                uint8_t byteCount = static_cast<uint8_t>(frame[12]);
                QByteArray payload = frame.mid(13, byteCount);
                return QString("[Запрос] %1, Начальный адрес: %2, Количество: %3, Данные: %4")
                       .arg(head).arg(startAddress).arg(quantity)
                       .arg(QString(payload.toHex(' ').toUpper()));
            }
        }
        default:
            return head;
    }
}

QString ModbusTCP::exceptionCodeToString(uint8_t code)
{
    switch (code) {
        case 0x01: return "Недопустимая функция";
        case 0x02: return "Недопустимый адрес данных";
        case 0x03: return "Недопустимое значение данных";
        case 0x04: return "Ошибка устройства";
        case 0x05: return "Подтверждение (требуется время)";
        case 0x06: return "Устройство занято";
        case 0x08: return "Ошибка чётности памяти";
        case 0x0A: return "Шлюз: нет пути";
        case 0x0B: return "Шлюз: устройство не ответило";
        default:   return "Неизвестная ошибка";
    }
}

QString ModbusTCP::functionCodeToString(FunctionCode code)
{
    switch (code) {
        case ReadCoils: return "Чтение катушек";
        case ReadDiscreteInputs: return "Чтение дискретных входов";
        case ReadHoldingRegisters: return "Чтение регистров хранения";
        case ReadInputRegisters: return "Чтение входных регистров";
        case WriteSingleCoil: return "Запись одной катушки";
        case WriteSingleRegister: return "Запись одного регистра";
        case WriteMultipleCoils: return "Запись нескольких катушек";
        case WriteMultipleRegisters: return "Запись нескольких регистров";
        default: return "Неизвестная функция";
    }
}

QByteArray ModbusTCP::uint16ToBytes(uint16_t value)
{
    QByteArray bytes;
    bytes.append(static_cast<char>((value >> 8) & 0xFF));
    bytes.append(static_cast<char>(value & 0xFF));
    return bytes;
}

uint16_t ModbusTCP::bytesToUint16(const QByteArray &bytes, int offset)
{
    if (bytes.length() < offset + 2) {
        return 0;
    }

    return (static_cast<uint8_t>(bytes[offset]) << 8) | static_cast<uint8_t>(bytes[offset + 1]);
}
