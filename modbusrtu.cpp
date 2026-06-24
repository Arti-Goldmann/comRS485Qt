#include "modbusrtu.h"
#include <QDebug>
#include <QVector>
#include <algorithm>

ModbusRTU::ModbusRTU(QObject *parent) : QObject(parent)
{
}

QByteArray ModbusRTU::createReadRequest(const ReadRequest &request)
{
    QByteArray packet;
    
    packet.append(static_cast<char>(request.slaveAddress));
    packet.append(static_cast<char>(request.functionCode));
    packet.append(uint16ToBytes(request.startAddress));
    packet.append(uint16ToBytes(request.quantity));
    
    uint16_t crc = calculateCRC(packet);
    packet.append(static_cast<char>(crc & 0xFF));
    packet.append(static_cast<char>((crc >> 8) & 0xFF));
    
    return packet;
}

QByteArray ModbusRTU::createWriteSingleRequest(const WriteSingleRequest &request)
{
    QByteArray packet;
    
    packet.append(static_cast<char>(request.slaveAddress));
    packet.append(static_cast<char>(request.functionCode));
    packet.append(uint16ToBytes(request.address));
    packet.append(uint16ToBytes(request.value));
    
    uint16_t crc = calculateCRC(packet);
    packet.append(static_cast<char>(crc & 0xFF));
    packet.append(static_cast<char>((crc >> 8) & 0xFF));
    
    return packet;
}

QByteArray ModbusRTU::createWriteMultipleRequest(const WriteMultipleRequest &request)
{
    QByteArray packet;
    
    packet.append(static_cast<char>(request.slaveAddress));
    packet.append(static_cast<char>(request.functionCode));
    packet.append(uint16ToBytes(request.startAddress));
    packet.append(uint16ToBytes(request.quantity));
    
    if (request.functionCode == WriteMultipleCoils) {
        uint8_t byteCount = (request.quantity + 7) / 8;
        packet.append(static_cast<char>(byteCount));
        packet.append(request.data.left(byteCount));
    } else if (request.functionCode == WriteMultipleRegisters) {
        uint8_t byteCount = request.quantity * 2;
        packet.append(static_cast<char>(byteCount));
        packet.append(request.data.left(byteCount));
    }
    
    uint16_t crc = calculateCRC(packet);
    packet.append(static_cast<char>(crc & 0xFF));
    packet.append(static_cast<char>((crc >> 8) & 0xFF));
    
    return packet;
}

bool ModbusRTU::validateResponse(const QByteArray &response)
{
    if (response.length() < 4) {
        return false;
    }
    
    QByteArray dataWithoutCRC = response.left(response.length() - 2);
    // CRC в Modbus RTU передается в Little Endian формате
    uint16_t receivedCRC = static_cast<uint8_t>(response[response.length() - 2]) | 
                          (static_cast<uint8_t>(response[response.length() - 1]) << 8);
    uint16_t calculatedCRC = calculateCRC(dataWithoutCRC);
    
    return receivedCRC == calculatedCRC;
}

QString ModbusRTU::parseResponse(const QByteArray &response)
{
    if (response.length() < 4) {
        return QString("Неверная длина ответа (получено %1 байт)").arg(response.length());
    }
    
    if (!validateResponse(response)) {
        QByteArray dataWithoutCRC = response.left(response.length() - 2);
        uint16_t receivedCRC = static_cast<uint8_t>(response[response.length() - 2]) | 
                              (static_cast<uint8_t>(response[response.length() - 1]) << 8);
        uint16_t calculatedCRC = calculateCRC(dataWithoutCRC);
        return QString("Ошибка CRC (получено: %1, ожидалось: %2)")
               .arg(receivedCRC, 4, 16, QChar('0'))
               .arg(calculatedCRC, 4, 16, QChar('0'));
    }
    
    uint8_t slaveAddress = static_cast<uint8_t>(response[0]);
    uint8_t functionCode = static_cast<uint8_t>(response[1]);
    
    QString result = QString("Адрес: %1, Функция: %2 (%3)")
                     .arg(slaveAddress)
                     .arg(functionCode, 2, 16, QChar('0'))
                     .arg(functionCodeToString(static_cast<FunctionCode>(functionCode)));
    
    if (functionCode & 0x80) {
        uint8_t exceptionCode = static_cast<uint8_t>(response[2]);
        result += QString(", Исключение: %1").arg(exceptionCode, 2, 16, QChar('0'));
    } else {
        switch (functionCode) {
            case ReadCoils:
            case ReadDiscreteInputs:
            case ReadHoldingRegisters:
            case ReadInputRegisters: {
                if (response.length() > 3) {
                    uint8_t byteCount = static_cast<uint8_t>(response[2]);
                    result += QString(", Байт данных: %1").arg(byteCount);
                    if (response.length() >= 3 + byteCount) {
                        QByteArray data = response.mid(3, byteCount);
                        result += QString(", Данные: %1").arg(QString(data.toHex(' ').toUpper()));
                    }
                }
                break;
            }
            case WriteSingleCoil:
            case WriteSingleRegister: {
                if (response.length() >= 6) {
                    uint16_t address = bytesToUint16(response, 2);
                    uint16_t value = bytesToUint16(response, 4);
                    result += QString(", Адрес: %1, Значение: %2").arg(address).arg(value);
                }
                break;
            }
            case WriteMultipleCoils:
            case WriteMultipleRegisters: {
                if (response.length() >= 6) {
                    uint16_t startAddress = bytesToUint16(response, 2);
                    uint16_t quantity = bytesToUint16(response, 4);
                    result += QString(", Начальный адрес: %1, Количество: %2").arg(startAddress).arg(quantity);
                }
                break;
            }
        }
    }
    
    return result;
}

int ModbusRTU::detectFrameLength(const QByteArray &buffer, int offset)
{
    int n = buffer.length();
    // Минимальный кадр Modbus RTU: адрес + функция + 2 байта CRC
    if (offset + 4 > n) {
        return 0;
    }

    uint8_t functionCode = static_cast<uint8_t>(buffer[offset + 1]);
    uint8_t baseFunction = functionCode & 0x7F;

    // Возможные длины кадра для данного кода функции.
    // Перебираем их по возрастанию и принимаем первую, у которой сходится CRC.
    QVector<int> candidates;

    if (functionCode & 0x80) {
        // Ответ-исключение: адрес + функция + код ошибки + CRC
        candidates.append(5);
    }

    switch (baseFunction) {
        case ReadCoils:
        case ReadDiscreteInputs:
        case ReadHoldingRegisters:
        case ReadInputRegisters:
            candidates.append(8); // запрос
            if (offset + 2 < n) {
                candidates.append(5 + static_cast<uint8_t>(buffer[offset + 2])); // ответ
            }
            break;
        case WriteSingleCoil:
        case WriteSingleRegister:
            candidates.append(8); // запрос и ответ (эхо) одинаковой длины
            break;
        case WriteMultipleCoils:
        case WriteMultipleRegisters:
            candidates.append(8); // ответ
            if (offset + 6 < n) {
                candidates.append(9 + static_cast<uint8_t>(buffer[offset + 6])); // запрос
            }
            break;
        default:
            break;
    }

    std::sort(candidates.begin(), candidates.end());

    int previous = -1;
    for (int length : candidates) {
        if (length == previous) {
            continue;
        }
        previous = length;

        if (length < 4 || offset + length > n) {
            continue;
        }
        if (validateResponse(buffer.mid(offset, length))) {
            return length;
        }
    }

    return 0;
}

QList<QByteArray> ModbusRTU::splitFrames(const QByteArray &buffer, QByteArray &unrecognized)
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
        } else {
            // Байт не является началом корректного кадра — пропускаем его
            // (ресинхронизация) и пробуем со следующего.
            unrecognized.append(buffer[i]);
            ++i;
        }
    }

    return frames;
}

QString ModbusRTU::parseFrame(const QByteArray &frame)
{
    if (frame.length() < 4) {
        return QString("Неверная длина кадра (%1 байт)").arg(frame.length());
    }
    if (!validateResponse(frame)) {
        return QString("Ошибка CRC");
    }

    uint8_t slaveAddress = static_cast<uint8_t>(frame[0]);
    uint8_t functionCode = static_cast<uint8_t>(frame[1]);
    int length = frame.length();

    QString head = QString("Адрес: %1, Функция: %2 (%3)")
                   .arg(slaveAddress)
                   .arg(functionCode, 2, 16, QChar('0'))
                   .arg(functionCodeToString(static_cast<FunctionCode>(functionCode & 0x7F)));

    if (functionCode & 0x80) {
        uint8_t exceptionCode = static_cast<uint8_t>(frame[2]);
        return QString("[Ответ-ошибка] %1, Исключение: %2 (%3)")
               .arg(head)
               .arg(exceptionCode, 2, 16, QChar('0'))
               .arg(exceptionCodeToString(exceptionCode));
    }

    switch (functionCode) {
        case ReadCoils:
        case ReadDiscreteInputs:
        case ReadHoldingRegisters:
        case ReadInputRegisters: {
            if (length == 8) {
                // Запрос: начальный адрес + количество
                uint16_t startAddress = bytesToUint16(frame, 2);
                uint16_t quantity = bytesToUint16(frame, 4);
                return QString("[Запрос] %1, Начальный адрес: %2, Количество: %3")
                       .arg(head).arg(startAddress).arg(quantity);
            } else {
                // Ответ: счётчик байт + данные
                uint8_t byteCount = static_cast<uint8_t>(frame[2]);
                QByteArray payload = frame.mid(3, byteCount);
                return QString("[Ответ] %1, Байт данных: %2, Данные: %3")
                       .arg(head).arg(byteCount).arg(QString(payload.toHex(' ').toUpper()));
            }
        }
        case WriteSingleCoil:
        case WriteSingleRegister: {
            // Запрос и ответ идентичны (эхо)
            uint16_t address = bytesToUint16(frame, 2);
            uint16_t value = bytesToUint16(frame, 4);
            return QString("[Запрос/Ответ] %1, Адрес: %2, Значение: %3")
                   .arg(head).arg(address).arg(value);
        }
        case WriteMultipleCoils:
        case WriteMultipleRegisters: {
            if (length == 8) {
                // Ответ: начальный адрес + количество
                uint16_t startAddress = bytesToUint16(frame, 2);
                uint16_t quantity = bytesToUint16(frame, 4);
                return QString("[Ответ] %1, Начальный адрес: %2, Количество: %3")
                       .arg(head).arg(startAddress).arg(quantity);
            } else {
                // Запрос: начальный адрес + количество + счётчик байт + данные
                uint16_t startAddress = bytesToUint16(frame, 2);
                uint16_t quantity = bytesToUint16(frame, 4);
                uint8_t byteCount = static_cast<uint8_t>(frame[6]);
                QByteArray payload = frame.mid(7, byteCount);
                return QString("[Запрос] %1, Начальный адрес: %2, Количество: %3, Данные: %4")
                       .arg(head).arg(startAddress).arg(quantity)
                       .arg(QString(payload.toHex(' ').toUpper()));
            }
        }
        default:
            return head;
    }
}

QString ModbusRTU::exceptionCodeToString(uint8_t code)
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

QString ModbusRTU::functionCodeToString(FunctionCode code)
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

uint16_t ModbusRTU::calculateCRC(const QByteArray &data)
{
    uint16_t crc = 0xFFFF;
    
    for (int i = 0; i < data.length(); ++i) {
        crc ^= static_cast<uint8_t>(data[i]);
        
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

QByteArray ModbusRTU::uint16ToBytes(uint16_t value)
{
    QByteArray bytes;
    bytes.append(static_cast<char>((value >> 8) & 0xFF));
    bytes.append(static_cast<char>(value & 0xFF));
    return bytes;
}

uint16_t ModbusRTU::bytesToUint16(const QByteArray &bytes, int offset)
{
    if (bytes.length() < offset + 2) {
        return 0;
    }
    
    return (static_cast<uint8_t>(bytes[offset]) << 8) | static_cast<uint8_t>(bytes[offset + 1]);
}