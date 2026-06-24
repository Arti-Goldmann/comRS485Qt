#ifndef MODBUSRTU_H
#define MODBUSRTU_H

#include <QObject>
#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>

class ModbusRTU : public QObject
{
    Q_OBJECT

public:
    enum FunctionCode {
        ReadCoils = 0x01,
        ReadDiscreteInputs = 0x02,
        ReadHoldingRegisters = 0x03,
        ReadInputRegisters = 0x04,
        WriteSingleCoil = 0x05,
        WriteSingleRegister = 0x06,
        WriteMultipleCoils = 0x0F,
        WriteMultipleRegisters = 0x10
    };

    struct ReadRequest {
        uint8_t slaveAddress;
        FunctionCode functionCode;
        uint16_t startAddress;
        uint16_t quantity;
    };

    struct WriteSingleRequest {
        uint8_t slaveAddress;
        FunctionCode functionCode;
        uint16_t address;
        uint16_t value;
    };

    struct WriteMultipleRequest {
        uint8_t slaveAddress;
        FunctionCode functionCode;
        uint16_t startAddress;
        uint16_t quantity;
        QByteArray data;
    };

    explicit ModbusRTU(QObject *parent = nullptr);

    static QByteArray createReadRequest(const ReadRequest &request);
    static QByteArray createWriteSingleRequest(const WriteSingleRequest &request);
    static QByteArray createWriteMultipleRequest(const WriteMultipleRequest &request);
    
    static bool validateResponse(const QByteArray &response);
    static QString parseResponse(const QByteArray &response);

    // Декодирует один кадр Modbus RTU (запрос или ответ) в читаемую строку.
    static QString parseFrame(const QByteArray &frame);

    // Выделяет из потока байт отдельные корректные (по CRC) кадры Modbus RTU.
    // Применяется при прослушивании шины, когда в одном пакете приходит
    // несколько склеенных кадров. Байты, не вошедшие ни в один кадр,
    // возвращаются через параметр unrecognized.
    static QList<QByteArray> splitFrames(const QByteArray &buffer, QByteArray &unrecognized);

    static QString functionCodeToString(FunctionCode code);
    static QString exceptionCodeToString(uint8_t code);

private:
    // Определяет длину корректного кадра Modbus RTU, начинающегося с offset,
    // либо 0, если корректный кадр (по CRC) там не найден.
    static int detectFrameLength(const QByteArray &buffer, int offset);

    static uint16_t calculateCRC(const QByteArray &data);
    static QByteArray uint16ToBytes(uint16_t value);
    static uint16_t bytesToUint16(const QByteArray &bytes, int offset = 0);
};

#endif // MODBUSRTU_H