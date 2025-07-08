#include "modbusrtu.h"
#include <QDebug>

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