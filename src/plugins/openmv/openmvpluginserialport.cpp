#include "openmvpluginserialport.h"

#define OPENMVCAM_BAUD_RATE 12000000
#define OPENMVCAM_BAUD_RATE_2 921600

#define WRITE_LOOPS 1 // disabled
#define WRITE_DELAY 0 // disabled
#define WRITE_TIMEOUT 3000
#define READ_TIMEOUT 5000
#define READ_STALL_TIMEOUT 1000
#define BOOTLOADER_WRITE_TIMEOUT 6
#define BOOTLOADER_READ_TIMEOUT 10
#define BOOTLOADER_READ_STALL_TIMEOUT 2
#define LEARN_MTU_WRITE_TIMEOUT 30
#define LEATN_MTU_READ_TIMEOUT 50

#define LEARN_MTU_MAX 4096
#define LEARN_MTU_MIN 64

void serializeByte(QByteArray &buffer, int value) // LittleEndian
{
    buffer.append(reinterpret_cast<const char *>(&value), 1);
}

void serializeWord(QByteArray &buffer, int value) // LittleEndian
{
    buffer.append(reinterpret_cast<const char *>(&value), 2);
}

void serializeLong(QByteArray &buffer, int value) // LittleEndian
{
    buffer.append(reinterpret_cast<const char *>(&value), 4);
}

int deserializeByte(QByteArray &buffer) // LittleEndian
{
    int r = int();
    memcpy(&r, buffer.data(), 1);
    buffer = buffer.mid(1);
    return r;
}

int deserializeWord(QByteArray &buffer) // LittleEndian
{
    int r = int();
    memcpy(&r, buffer.data(), 2);
    buffer = buffer.mid(2);
    return r;
}

int deserializeLong(QByteArray &buffer) // LittleEndian
{
    int r = int();
    memcpy(&r, buffer.data(), 4);
    buffer = buffer.mid(4);
    return r;
}

OpenMVPluginSerialPort_thing::OpenMVPluginSerialPort_thing(const QString &name, QObject *parent) : QObject(parent)
{
    if(QSerialPortInfo(name).isValid())
    {
        m_serialPort = new QSerialPort(name, this);
        m_tcpSocket = Q_NULLPTR;
    }
    else
    {
        m_serialPort = Q_NULLPTR;
        m_tcpSocket = new QTcpSocket(this);
        m_tcpSocket->setProperty("name", name);
    }
}

QString OpenMVPluginSerialPort_thing::portName()
{
    if(m_serialPort)
    {
        return m_serialPort->portName();
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->property("name").toString();
    }

    return QString();
}

void OpenMVPluginSerialPort_thing::setReadBufferSize(qint64 size)
{
    if(m_serialPort)
    {
        m_serialPort->setReadBufferSize(size);
    }

    if(m_tcpSocket)
    {
        m_tcpSocket->setReadBufferSize(size);
    }
}

bool OpenMVPluginSerialPort_thing::setBaudRate(qint32 baudRate)
{
    if(m_serialPort)
    {
        return m_serialPort->setBaudRate(baudRate);
    }

    if(m_tcpSocket)
    {
        return true;
    }

    return bool();
}

bool OpenMVPluginSerialPort_thing::open(QIODevice::OpenMode mode)
{
    if(m_serialPort)
    {
        return m_serialPort->open(mode);
    }

    if(m_tcpSocket)
    {
        QStringList list = m_tcpSocket->property("name").toString().split(QLatin1Char(':'));

        if(list.size() != 3)
        {
            return false;
        }

        QString hostName = list.at(1);
        QString port = list.at(2);

        bool portNumberOkay;
        quint16 portNumber = port.toUInt(&portNumberOkay);

        if(!portNumberOkay)
        {
            return false;
        }

        m_tcpSocket->connectToHost(hostName, portNumber, mode);
        return m_tcpSocket->waitForConnected(3000);
    }

    return bool();
}

bool OpenMVPluginSerialPort_thing::flush()
{
    if(m_serialPort)
    {
        return m_serialPort->flush();
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->flush();
    }

    return bool();
}

QString OpenMVPluginSerialPort_thing::errorString()
{
    if(m_serialPort)
    {
        return m_serialPort->errorString();
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->errorString();
    }

    return QString();
}

void OpenMVPluginSerialPort_thing::clearError()
{
    if(m_serialPort)
    {
        m_serialPort->clearError();
    }
}

QByteArray OpenMVPluginSerialPort_thing::readAll()
{
    if(m_serialPort)
    {
        return m_serialPort->readAll();
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->readAll();
    }

    return QByteArray();
}

qint64 OpenMVPluginSerialPort_thing::write(const QByteArray &data)
{
    if(m_serialPort)
    {
        return m_serialPort->write(data);
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->write(data);
    }

    return qint64();
}

qint64 OpenMVPluginSerialPort_thing::bytesAvailable()
{
    if(m_serialPort)
    {
        return m_serialPort->bytesAvailable();
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->bytesAvailable();
    }

    return qint64();
}

qint64 OpenMVPluginSerialPort_thing::bytesToWrite()
{
    if(m_serialPort)
    {
        return m_serialPort->bytesToWrite();
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->bytesToWrite();
    }

    return qint64();
}

bool OpenMVPluginSerialPort_thing::waitForReadyRead(int msecs)
{
    if(m_serialPort)
    {
        return m_serialPort->waitForReadyRead(msecs);
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->waitForReadyRead(msecs);
    }

    return bool();
}

bool OpenMVPluginSerialPort_thing::waitForBytesWritten(int msecs)
{
    if(m_serialPort)
    {
        return m_serialPort->waitForBytesWritten(msecs);
    }

    if(m_tcpSocket)
    {
        return m_tcpSocket->waitForBytesWritten(msecs);
    }

    return bool();
}

bool OpenMVPluginSerialPort_thing::setDataTerminalReady(bool set)
{
    if(m_serialPort)
    {
        return m_serialPort->setDataTerminalReady(set);
    }

    if(m_tcpSocket)
    {
        return true;
    }

    return bool();
}

bool OpenMVPluginSerialPort_thing::setRequestToSend(bool set)
{
    if(m_serialPort)
    {
        return m_serialPort->setRequestToSend(set);
    }

    if(m_tcpSocket)
    {
        return true;
    }

    return bool();
}

OpenMVPluginSerialPort_private::OpenMVPluginSerialPort_private(int override_read_timeout, int override_read_stall_timeout, QObject *parent) : QObject(parent)
{
    m_port = Q_NULLPTR;
    m_bootloaderStop = false;
    read_timeout = (override_read_timeout > 0) ? override_read_timeout : READ_TIMEOUT;
    read_stall_timeout = (override_read_stall_timeout > 0) ? override_read_stall_timeout : READ_STALL_TIMEOUT;
}

void OpenMVPluginSerialPort_private::open(const QString &portName)
{
    if(m_port)
    {
        delete m_port;
    }

    m_port = new OpenMVPluginSerialPort_thing(portName, this);
    // QSerialPort is buggy unless this is set.
    m_port->setReadBufferSize(1000000);

    if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE))
    || (!m_port->open(QIODevice::ReadWrite))
    || (!m_port->setDataTerminalReady(true)))
    {
        delete m_port;
        m_port = new OpenMVPluginSerialPort_thing(portName, this);
        // QSerialPort is buggy unless this is set.
        m_port->setReadBufferSize(1000000);

        if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE_2))
        || (!m_port->open(QIODevice::ReadWrite))
        || (!m_port->setDataTerminalReady(true)))
        {
            emit openResult(m_port->errorString());
            delete m_port;
            m_port = Q_NULLPTR;
        }
    }

    if(m_port)
    {
        emit openResult(QString());
    }
}

void OpenMVPluginSerialPort_private::write(const QByteArray &data, int startWait, int stopWait, int timeout)
{
    if(m_port)
    {
        QString portName = m_port->portName();

        for(int i = 0; i < WRITE_LOOPS; i++)
        {
            if(!m_port)
            {
                m_port = new OpenMVPluginSerialPort_thing(portName, this);
                // QSerialPort is buggy unless this is set.
                m_port->setReadBufferSize(1000000);

                if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE))
                || (!m_port->open(QIODevice::ReadWrite))
                || (!m_port->setDataTerminalReady(true)))
                {
                    delete m_port;
                    m_port = new OpenMVPluginSerialPort_thing(portName, this);
                    // QSerialPort is buggy unless this is set.
                    m_port->setReadBufferSize(1000000);

                    if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE_2))
                    || (!m_port->open(QIODevice::ReadWrite))
                    || (!m_port->setDataTerminalReady(true)))
                    {
                        delete m_port;
                        m_port = Q_NULLPTR;
                    }
                }
            }

            if(m_port)
            {
                if(startWait)
                {
                    QThread::msleep(startWait);
                }

                m_port->clearError();

                if((m_port->write(data) != data.size()) || (!m_port->flush()))
                {
                    delete m_port;
                    m_port = Q_NULLPTR;
                }
                else
                {
                    QElapsedTimer elaspedTimer;
                    elaspedTimer.start();

                    while(m_port->bytesToWrite())
                    {
                        m_port->waitForBytesWritten(1);

                        if(m_port->bytesToWrite() && elaspedTimer.hasExpired(timeout))
                        {
                            break;
                        }
                    }

                    if(m_port->bytesToWrite())
                    {
                        delete m_port;
                        m_port = Q_NULLPTR;
                    }
                    else if(stopWait)
                    {
                        QThread::msleep(stopWait);
                    }
                }
            }

            if(m_port)
            {
                break;
            }

            QThread::msleep(WRITE_DELAY);
        }
    }
}

void OpenMVPluginSerialPort_private::command(const OpenMVPluginSerialPortCommand &command)
{
    if(command.m_data.isEmpty())
    {
        if(!command.m_responseLen) // close
        {
            if(m_port)
            {
                delete m_port;
                m_port = Q_NULLPTR;
            }

            emit commandResult(OpenMVPluginSerialPortCommandResult(true, QByteArray()));
        }
        else if(m_port) // learn
        {
            bool ok = false;

            for(int i = LEARN_MTU_MAX; i >= LEARN_MTU_MIN; i /= 2)
            {
                QByteArray learnMTU;
                serializeByte(learnMTU, __USBDBG_CMD);
                serializeByte(learnMTU, __USBDBG_LEARN_MTU);
                serializeLong(learnMTU, i - 1);

                write(learnMTU, LEARN_MTU_START_DELAY, LEARN_MTU_END_DELAY, LEARN_MTU_WRITE_TIMEOUT);

                if(!m_port)
                {
                    break;
                }
                else
                {
                    QByteArray response;
                    QElapsedTimer elaspedTimer;
                    elaspedTimer.start();

                    do
                    {
                        m_port->waitForReadyRead(1);
                        response.append(m_port->readAll());
                    }
                    while((response.size() < (i - 1)) && (!elaspedTimer.hasExpired(LEATN_MTU_READ_TIMEOUT)));

                    if(response.size() >= (i - 1))
                    {
                        QByteArray temp;
                        serializeLong(temp, (i - 1));
                        emit commandResult(OpenMVPluginSerialPortCommandResult(true, temp));
                        ok = true;
                        break;
                    }
                }
            }

            if(!ok)
            {
                if(m_port)
                {
                    delete m_port;
                    m_port = Q_NULLPTR;
                }

                emit commandResult(OpenMVPluginSerialPortCommandResult(false, QByteArray()));
            }
        }
        else
        {
            emit commandResult(OpenMVPluginSerialPortCommandResult(false, QByteArray()));
        }
    }
    else if(m_port)
    {
        write(command.m_data, command.m_startWait, command.m_endWait, WRITE_TIMEOUT);

        if((!m_port) || (!command.m_responseLen))
        {
            emit commandResult(OpenMVPluginSerialPortCommandResult(m_port, QByteArray()));
        }
        else
        {
            QByteArray response;
            int responseLen = command.m_responseLen;
            QElapsedTimer elaspedTimer;
            QElapsedTimer elaspedTimer2;
            elaspedTimer.start();
            elaspedTimer2.start();

            do
            {
                m_port->waitForReadyRead(1);
                response.append(m_port->readAll());

                if((response.size() < responseLen) && elaspedTimer2.hasExpired(read_stall_timeout))
                {
                    QByteArray data;
                    serializeByte(data, __USBDBG_CMD);
                    serializeByte(data, __USBDBG_SCRIPT_RUNNING);
                    serializeLong(data, SCRIPT_RUNNING_RESPONSE_LEN);
                    write(data, SCRIPT_RUNNING_START_DELAY, SCRIPT_RUNNING_END_DELAY, WRITE_TIMEOUT);

                    if(m_port)
                    {
                        responseLen += SCRIPT_RUNNING_RESPONSE_LEN;
                        elaspedTimer2.restart();
                    }
                    else
                    {
                        break;
                    }
                }
            }
            while((response.size() < responseLen) && (!elaspedTimer.hasExpired(read_timeout)));

            if(response.size() >= responseLen)
            {
                emit commandResult(OpenMVPluginSerialPortCommandResult(true, response.left(command.m_responseLen)));
            }
            else
            {
                if(m_port)
                {
                    delete m_port;
                    m_port = Q_NULLPTR;
                }

                emit commandResult(OpenMVPluginSerialPortCommandResult(false, QByteArray()));
            }
        }
    }
    else
    {
        emit commandResult(OpenMVPluginSerialPortCommandResult(false, QByteArray()));
    }

    // Execute commands slowly so as to not overload the OpenMV Cam board.

    QThread::msleep(Utils::HostOsInfo::isMacHost() ? 2 : 1);
}

void OpenMVPluginSerialPort_private::bootloaderStart(const QString &selectedPort)
{
    if(m_port)
    {
        QByteArray buffer;
        serializeByte(buffer, __USBDBG_CMD);
        serializeByte(buffer, __USBDBG_SYS_RESET);
        serializeLong(buffer, int());
        write(buffer, SYS_RESET_START_DELAY, SYS_RESET_END_DELAY, WRITE_TIMEOUT);

        if(m_port)
        {
            delete m_port;
            m_port = Q_NULLPTR;
        }
    }

    forever
    {
        QStringList stringList;

        foreach(QSerialPortInfo port, QSerialPortInfo::availablePorts())
        {
            if(port.hasVendorIdentifier() && (port.vendorIdentifier() == OPENMVCAM_VID)
            && port.hasProductIdentifier() && (port.productIdentifier() == OPENMVCAM_PID))
            {
                stringList.append(port.portName());
            }
        }

        if(Utils::HostOsInfo::isMacHost())
        {
            stringList = stringList.filter(QStringLiteral("cu"), Qt::CaseInsensitive);
        }

        if(!stringList.isEmpty())
        {
            const QString portName = ((!selectedPort.isEmpty()) && stringList.contains(selectedPort)) ? selectedPort : stringList.first();

            if(Q_UNLIKELY(m_port))
            {
                delete m_port;
            }

            m_port = new OpenMVPluginSerialPort_thing(portName, this);
            // QSerialPort is buggy unless this is set.
            m_port->setReadBufferSize(1000000);

            if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE))
            || (!m_port->open(QIODevice::ReadWrite))
            || (!m_port->setDataTerminalReady(true)))
            {
                delete m_port;
                m_port = new OpenMVPluginSerialPort_thing(portName, this);
                // QSerialPort is buggy unless this is set.
                m_port->setReadBufferSize(1000000);

                if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE_2))
                || (!m_port->open(QIODevice::ReadWrite))
                || (!m_port->setDataTerminalReady(true)))
                {
                    delete m_port;
                    m_port = Q_NULLPTR;
                }
            }

            if(m_port)
            {
                QByteArray buffer;
                serializeLong(buffer, __BOOTLDR_START);
                write(buffer, BOOTLDR_START_START_DELAY, BOOTLDR_START_END_DELAY, BOOTLOADER_WRITE_TIMEOUT);

                if(m_port)
                {
                    QByteArray response;
                    int responseLen = BOOTLDR_START_RESPONSE_LEN;
                    QElapsedTimer elaspedTimer;
                    QElapsedTimer elaspedTimer2;
                    elaspedTimer.start();
                    elaspedTimer2.start();

                    do
                    {
                        m_port->waitForReadyRead(1);
                        response.append(m_port->readAll());

                        if((response.size() < responseLen) && elaspedTimer2.hasExpired(BOOTLOADER_READ_STALL_TIMEOUT))
                        {
                            QByteArray data;
                            serializeLong(data, __BOOTLDR_START);
                            write(data, BOOTLDR_START_START_DELAY, BOOTLDR_START_END_DELAY, BOOTLOADER_WRITE_TIMEOUT);

                            if(m_port)
                            {
                                responseLen += BOOTLDR_START_RESPONSE_LEN;
                                elaspedTimer2.restart();
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    while((response.size() < responseLen) && (!elaspedTimer.hasExpired(BOOTLOADER_READ_TIMEOUT)));

                    if(response.size() >= responseLen)
                    {
                        int result = deserializeLong(response);

                        if((result == OLD_BOOTLDR)
                        || (result == NEW_BOOTLDR))
                        {
                            emit bootloaderStartResponse(true, result);
                            return;
                        }
                    }

                    if(m_port)
                    {
                        delete m_port;
                        m_port = Q_NULLPTR;
                    }
                }
            }
        }

        QCoreApplication::processEvents();

        if(m_bootloaderStop)
        {
            emit bootloaderStartResponse(false, int());
            return;
        }
    }
}

void OpenMVPluginSerialPort_private::bootloaderStop()
{
    m_bootloaderStop = true;
    emit bootloaderStopResponse();
}

void OpenMVPluginSerialPort_private::bootloaderReset()
{
    m_bootloaderStop = false;
    emit bootloaderResetResponse();
}

OpenMVPluginSerialPort::OpenMVPluginSerialPort(int override_read_timeout, int override_read_stall_timeout, QObject *parent) : QObject(parent)
{
    QThread *thread = new QThread;
    OpenMVPluginSerialPort_private *port = new OpenMVPluginSerialPort_private(override_read_timeout, override_read_stall_timeout);
    port->moveToThread(thread);

    connect(this, &OpenMVPluginSerialPort::open,
            port, &OpenMVPluginSerialPort_private::open);

    connect(port, &OpenMVPluginSerialPort_private::openResult,
            this, &OpenMVPluginSerialPort::openResult);

    connect(this, &OpenMVPluginSerialPort::command,
            port, &OpenMVPluginSerialPort_private::command);

    connect(port, &OpenMVPluginSerialPort_private::commandResult,
            this, &OpenMVPluginSerialPort::commandResult);

    connect(this, &OpenMVPluginSerialPort::bootloaderStart,
            port, &OpenMVPluginSerialPort_private::bootloaderStart);

    connect(this, &OpenMVPluginSerialPort::bootloaderStop,
            port, &OpenMVPluginSerialPort_private::bootloaderStop);

    connect(this, &OpenMVPluginSerialPort::bootloaderReset,
            port, &OpenMVPluginSerialPort_private::bootloaderReset);

    connect(port, &OpenMVPluginSerialPort_private::bootloaderStartResponse,
            this, &OpenMVPluginSerialPort::bootloaderStartResponse);

    connect(port, &OpenMVPluginSerialPort_private::bootloaderStopResponse,
            this, &OpenMVPluginSerialPort::bootloaderStopResponse);

    connect(port, &OpenMVPluginSerialPort_private::bootloaderResetResponse,
            this, &OpenMVPluginSerialPort::bootloaderResetResponse);

    connect(this, &OpenMVPluginSerialPort::destroyed,
            port, &OpenMVPluginSerialPort_private::deleteLater);

    connect(port, &OpenMVPluginSerialPort_private::destroyed,
            thread, &QThread::quit);

    connect(thread, &QThread::finished,
            thread, &QThread::deleteLater);

    thread->start();
}
