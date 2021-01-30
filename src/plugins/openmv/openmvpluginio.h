#ifndef OPENMVPLUGINIO_H
#define OPENMVPLUGINIO_H

#include <QtCore>
#include <QtGui>

#include "openmvpluginserialport.h"

#define ATTR_CONTRAST                   0
#define ATTR_BRIGHTNESS                 1
#define ATTR_SATURATION                 2
#define ATTR_GAINCEILING                3

#define TEMPLATE_SAVE_PATH_MAX_LEN      55
#define DESCRIPTOR_SAVE_PATH_MAX_LEN    55

#define FLASH_SECTOR_START              4
#define FLASH_SECTOR_END                11
#define FLASH_SECTOR_ALL_START          1
#define FLASH_SECTOR_ALL_END            11

#define FLASH_WRITE_CHUNK_SIZE          56

#define FLASH_ERASE_DELAY               2000
#define FLASH_WRITE_DELAY               1

#define IS_JPG(bpp)                     ((bpp) >= 3)
#define IS_RGB(bpp)                     ((bpp) == 2)
#define IS_GS(bpp)                      ((bpp) == 1)
#define IS_BINARY(bpp)                  ((bpp) == 0)

int getImageSize(int w, int h, int bpp);
QPixmap getImageFromData(QByteArray data, int w, int h, int bpp, bool rgb565ByteReversed);

class OpenMVPluginIO : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVPluginIO(OpenMVPluginSerialPort *port, QObject *parent = Q_NULLPTR);

    bool getTimeout();
    bool frameSizeDumpQueued() const;
    bool getScriptRunningQueued() const;
    bool getAttributeQueued() const;
    bool getTxBufferQueued() const;

public slots:

    void getFirmwareVersion();
    void frameSizeDump();
    void getArchString();
    void learnMTU();
    void scriptExec(const QByteArray &data);
    void scriptStop();
    void getScriptRunning();
    void templateSave(int x, int y, int w, int h, const QByteArray &path);
    void descriptorSave(int x, int y, int w, int h, const QByteArray &path);
    void getAttribute(int attribute);
    void setAttribute(int attribute, int value);
    void sysReset(bool enterBootloader = false);
    void fbEnable(bool enable);
    void jpegEnable(bool enabled);
    void getTxBuffer();
    void sensorId();
    void bootloaderStart();
    void bootloaderReset();
    void flashErase(int sector);
    void flashWrite(const QByteArray &data);
    void bootloaderQuery();
    void bootloaderQSPIFErase(int sector);
    void bootloaderQSPIFWrite(const QByteArray &data);
    void bootloaderQSPIFLayout();
    void bootloaderQSPIFMemtest();
    void close();

public slots: // private

    void command();
    void commandResult(const OpenMVPluginSerialPortCommandResult &commandResult);
    void breakUpGetAttributeCommand(bool on) { m_breakUpGetAttributeCommand = on; }
    void breakUpSetAttributeCommand(bool on) { m_breakUpSetAttributeCommand = on; }
    void breakUpFBEnable(bool on) { m_breakUpFBEnable = on; }
    void breakUpJPEGEnable(bool on) { m_breakUpJPEGEnable = on; }
    void rgb565ByteReservedEnable(bool on) { m_rgb565ByteReversed = on; }

signals:

    void firmwareVersion(int major, int minor, int patch);
    void frameBufferData(const QPixmap &data);
    void archString(const QString &arch);
    void learnedMTU(bool ok);
    void scriptExecDone();
    void scriptStopDone();
    void scriptRunning(bool running);
    void templateSaveDone();
    void descriptorSaveDone();
    void attribute(int);
    void setAttrributeDone();
    void sysResetDone();
    void fbEnableDone();
    void jpegEnableDone();
    void printData(const QByteArray &data);
    void sensorIdDone(int id);
    void gotBootloaderStart(bool ok, int version);
    void bootloaderResetDone(bool ok);
    void flashEraseDone(bool ok);
    void flashWriteDone(bool ok);
    void bootloaderQueryDone(int all_start, int start, int last);
    void bootloaderQSPIFEraseDone(bool ok);
    void bootloaderQSPIFWriteDone(bool ok);
    void bootloaderQSPIFLayoutDone(int start_block, int max_block, int block_size_in_bytes);
    void bootloaderQSPIFMemtestDone(bool ok);
    void closeResponse();

private:

    QByteArray pasrsePrintData(const QByteArray &data);

    OpenMVPluginSerialPort *m_port;

    QQueue<OpenMVPluginSerialPortCommand> m_postedQueue;
    QQueue<int> m_completionQueue;
    int m_frameSizeW, m_frameSizeH, m_frameSizeBPP, m_mtu;
    QByteArray m_pixelBuffer, m_lineBuffer;
    bool m_timeout;
    bool m_breakUpGetAttributeCommand;
    bool m_breakUpSetAttributeCommand;
    bool m_breakUpFBEnable;
    bool m_breakUpJPEGEnable;
    bool m_rgb565ByteReversed;
};

#endif // OPENMVPLUGINIO_H
