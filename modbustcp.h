#ifndef MODBUSTCP_H
#define MODBUSTCP_H

#include <QObject>
#include <QByteArray>
#include <QList>
#include <QString>
#include <cstdint>

// Modbus TCP отличается от Modbus RTU способом обрамления PDU:
//   RTU:  [адрес устройства][PDU][CRC16]
//   TCP:  [MBAP-заголовок (7 байт)][PDU]
// где MBAP = TransactionId(2) + ProtocolId(2, =0) + Length(2) + UnitId(1).
// Сам PDU (код функции + данные) у RTU и TCP идентичен. CRC в TCP не используется,
// целостность обеспечивает транспорт (TCP). Поле Length содержит число байт,
// следующих за ним: UnitId(1) + PDU.
class ModbusTCP : public QObject
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
        uint8_t unitId;
        FunctionCode functionCode;
        uint16_t startAddress;
        uint16_t quantity;
    };

    struct WriteSingleRequest {
        uint8_t unitId;
        FunctionCode functionCode;
        uint16_t address;
        uint16_t value;
    };

    struct WriteMultipleRequest {
        uint8_t unitId;
        FunctionCode functionCode;
        uint16_t startAddress;
        uint16_t quantity;
        QByteArray data;
    };

    explicit ModbusTCP(QObject *parent = nullptr);

    // transactionId — идентификатор транзакции, который устройство вернёт в ответе.
    // Обычно его инкрементируют на каждый запрос.
    static QByteArray createReadRequest(const ReadRequest &request, uint16_t transactionId);
    static QByteArray createWriteSingleRequest(const WriteSingleRequest &request, uint16_t transactionId);
    static QByteArray createWriteMultipleRequest(const WriteMultipleRequest &request, uint16_t transactionId);

    // Проверяет согласованность MBAP-заголовка: ProtocolId == 0 и поле Length
    // соответствует фактической длине кадра.
    static bool validateResponse(const QByteArray &response);
    static QString parseResponse(const QByteArray &response);

    // Декодирует один кадр Modbus TCP (запрос или ответ) в читаемую строку.
    static QString parseFrame(const QByteArray &frame);

    // Выделяет из потока байт отдельные корректные кадры Modbus TCP по полю Length
    // в MBAP-заголовке. Незавершённый «хвост» (когда кадр пришёл не целиком)
    // и мусор возвращаются через параметр unrecognized.
    static QList<QByteArray> splitFrames(const QByteArray &buffer, QByteArray &unrecognized);

    static QString functionCodeToString(FunctionCode code);
    static QString exceptionCodeToString(uint8_t code);

private:
    // Собирает полный кадр: MBAP-заголовок + готовый PDU.
    static QByteArray buildFrame(uint16_t transactionId, uint8_t unitId, const QByteArray &pdu);

    // Определяет длину корректного кадра Modbus TCP, начинающегося с offset.
    // Возвращает 0, если в этой позиции нет валидного начала кадра, и -1,
    // если кадр валиден, но пришёл не целиком (нужно ждать ещё байты).
    static int detectFrameLength(const QByteArray &buffer, int offset);

    static QByteArray uint16ToBytes(uint16_t value);
    static uint16_t bytesToUint16(const QByteArray &bytes, int offset = 0);

    static const int MbapLength = 7; // TransactionId(2)+ProtocolId(2)+Length(2)+UnitId(1)
};

#endif // MODBUSTCP_H
