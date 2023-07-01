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

#define FAST_FLASH_PACKET_BATCH_COUNT   32
#define SAFE_FLASH_PACKET_BATCH_COUNT   1

#define FLASH_SECTOR_START              4
#define FLASH_SECTOR_END                11
#define FLASH_SECTOR_ALL_START          1
#define FLASH_SECTOR_ALL_END            11

#define SAFE_FLASH_ERASE_DELAY          2000
#define SAFE_FLASH_WRITE_DELAY          1

#define OLD_IS_JPG(bpp)                 ((bpp) > 3)
#define OLD_IS_BAYER(bpp)               ((bpp) == 3)
#define OLD_IS_RGB(bpp)                 ((bpp) == 2)
#define OLD_IS_GS(bpp)                  ((bpp) == 1)
#define OLD_IS_BINARY(bpp)              ((bpp) == 0)

typedef enum {
    PIXFORMAT_ID_BINARY     = 1,
    PIXFORMAT_ID_GRAY       = 2,
    PIXFORMAT_ID_RGB565     = 3,
    PIXFORMAT_ID_BAYER      = 4,
    PIXFORMAT_ID_YUV422     = 5,
    PIXFORMAT_ID_JPEG       = 6,
    PIXFORMAT_ID_PNG        = 7,
    PIXFORMAT_ID_ARGB8      = 8,
} pixformat_id_t;

typedef enum {
    SUBFORMAT_ID_GRAY8      = 0,
    SUBFORMAT_ID_GRAY16     = 1,
    SUBFORMAT_ID_BGGR       = 0,
    SUBFORMAT_ID_GBRG       = 1,
    SUBFORMAT_ID_GRBG       = 2,
    SUBFORMAT_ID_RGGB       = 3,
    SUBFORMAT_ID_YUV422     = 0,
    SUBFORMAT_ID_YVU422     = 1,
} subformat_id_t;

typedef enum {
    PIXFORMAT_BPP_BINARY    = 0,
    PIXFORMAT_BPP_GRAY8     = 1,
    PIXFORMAT_BPP_GRAY16    = 2,
    PIXFORMAT_BPP_RGB565    = 2,
    PIXFORMAT_BPP_BAYER     = 1,
    PIXFORMAT_BPP_YUV422    = 2,
    PIXFORMAT_BPP_ARGB8     = 4,
} pixformat_bpp_t;

// Pixel format flags.
#define PIXFORMAT_FLAGS_Y       (1 << 28) // YUV format.
#define PIXFORMAT_FLAGS_M       (1 << 27) // Mutable format.
#define PIXFORMAT_FLAGS_C       (1 << 26) // Colored format.
#define PIXFORMAT_FLAGS_J       (1 << 25) // Compressed format (JPEG/PNG).
#define PIXFORMAT_FLAGS_R       (1 << 24) // RAW/Bayer format.
#define PIXFORMAT_FLAGS_CY      (PIXFORMAT_FLAGS_C | PIXFORMAT_FLAGS_Y)
#define PIXFORMAT_FLAGS_CM      (PIXFORMAT_FLAGS_C | PIXFORMAT_FLAGS_M)
#define PIXFORMAT_FLAGS_CR      (PIXFORMAT_FLAGS_C | PIXFORMAT_FLAGS_R)
#define PIXFORMAT_FLAGS_CJ      (PIXFORMAT_FLAGS_C | PIXFORMAT_FLAGS_J)

// Each pixel format encodes flags, pixel format id and bpp as follows:
// 31......29  28  27  26  25  24  23..........16  15...........8  7.............0
// <RESERVED>  YF  MF  CF  JF  RF  <PIXFORMAT_ID>  <SUBFORMAT_ID>  <BYTES_PER_PIX>
// NOTE: Bit 31-30 must Not be used for pixformat_t to be used as mp_int_t.
typedef enum {
    PIXFORMAT_INVALID    = (0x00000000U),
    PIXFORMAT_BINARY     = (PIXFORMAT_FLAGS_M  | (PIXFORMAT_ID_BINARY << 16) | (0                   << 8) | PIXFORMAT_BPP_BINARY ),
    PIXFORMAT_GRAYSCALE  = (PIXFORMAT_FLAGS_M  | (PIXFORMAT_ID_GRAY   << 16) | (SUBFORMAT_ID_GRAY8  << 8) | PIXFORMAT_BPP_GRAY8  ),
    PIXFORMAT_RGB565     = (PIXFORMAT_FLAGS_CM | (PIXFORMAT_ID_RGB565 << 16) | (0                   << 8) | PIXFORMAT_BPP_RGB565 ),
    PIXFORMAT_ARGB8      = (PIXFORMAT_FLAGS_CM | (PIXFORMAT_ID_ARGB8  << 16) | (0                   << 8) | PIXFORMAT_BPP_ARGB8  ),
    PIXFORMAT_BAYER      = (PIXFORMAT_FLAGS_CR | (PIXFORMAT_ID_BAYER  << 16) | (SUBFORMAT_ID_BGGR   << 8) | PIXFORMAT_BPP_BAYER  ),
    PIXFORMAT_BAYER_BGGR = (PIXFORMAT_FLAGS_CR | (PIXFORMAT_ID_BAYER  << 16) | (SUBFORMAT_ID_BGGR   << 8) | PIXFORMAT_BPP_BAYER  ),
    PIXFORMAT_BAYER_GBRG = (PIXFORMAT_FLAGS_CR | (PIXFORMAT_ID_BAYER  << 16) | (SUBFORMAT_ID_GBRG   << 8) | PIXFORMAT_BPP_BAYER  ),
    PIXFORMAT_BAYER_GRBG = (PIXFORMAT_FLAGS_CR | (PIXFORMAT_ID_BAYER  << 16) | (SUBFORMAT_ID_GRBG   << 8) | PIXFORMAT_BPP_BAYER  ),
    PIXFORMAT_BAYER_RGGB = (PIXFORMAT_FLAGS_CR | (PIXFORMAT_ID_BAYER  << 16) | (SUBFORMAT_ID_RGGB   << 8) | PIXFORMAT_BPP_BAYER  ),
    PIXFORMAT_YUV        = (PIXFORMAT_FLAGS_CY | (PIXFORMAT_ID_YUV422 << 16) | (SUBFORMAT_ID_YUV422 << 8) | PIXFORMAT_BPP_YUV422 ),
    PIXFORMAT_YUV422     = (PIXFORMAT_FLAGS_CY | (PIXFORMAT_ID_YUV422 << 16) | (SUBFORMAT_ID_YUV422 << 8) | PIXFORMAT_BPP_YUV422 ),
    PIXFORMAT_YVU422     = (PIXFORMAT_FLAGS_CY | (PIXFORMAT_ID_YUV422 << 16) | (SUBFORMAT_ID_YVU422 << 8) | PIXFORMAT_BPP_YUV422 ),
    PIXFORMAT_JPEG       = (PIXFORMAT_FLAGS_CJ | (PIXFORMAT_ID_JPEG   << 16) | (0                   << 8) | 0                    ),
    PIXFORMAT_PNG        = (PIXFORMAT_FLAGS_CJ | (PIXFORMAT_ID_PNG    << 16) | (0                   << 8) | 0                    ),
    PIXFORMAT_LAST       = (0xFFFFFFFFU),
} pixformat_t;

#define IMLIB_PIXFORMAT_IS_VALID(x)     \
    ((x == PIXFORMAT_BINARY)            \
     || (x == PIXFORMAT_GRAYSCALE)      \
     || (x == PIXFORMAT_RGB565)         \
     || (x == PIXFORMAT_ARGB8)          \
     || (x == PIXFORMAT_BAYER_BGGR)     \
     || (x == PIXFORMAT_BAYER_GBRG)     \
     || (x == PIXFORMAT_BAYER_GRBG)     \
     || (x == PIXFORMAT_BAYER_RGGB)     \
     || (x == PIXFORMAT_YUV422)         \
     || (x == PIXFORMAT_YVU422)         \
     || (x == PIXFORMAT_JPEG)           \
     || (x == PIXFORMAT_PNG))

#define PIXFORMAT_BAYER_ANY             \
        PIXFORMAT_BAYER_BGGR:           \
        case PIXFORMAT_BAYER_GBRG:      \
        case PIXFORMAT_BAYER_GRBG:      \
        case PIXFORMAT_BAYER_RGGB

#define PIXFORMAT_YUV_ANY               \
        PIXFORMAT_YUV422:               \
        case PIXFORMAT_YVU422

#define PIXFORMAT_COMPRESSED_ANY        \
        PIXFORMAT_JPEG:                 \
        case PIXFORMAT_PNG

int getImageSize(int w, int h, int bpp, bool newPixformat, int pixformat);
QPixmap getImageFromData(QByteArray data, int w, int h, int bpp, bool rgb565ByteReversed, bool newPixformat, int pixformat);

class OpenMVPluginIO : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVPluginIO(OpenMVPluginSerialPort *port, QObject *parent = Q_NULLPTR);

    bool getTimeout();
    bool queueisEmpty() const;
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
    void mainTerminalInput(const QByteArray &data);
    void timeInput();
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
    void newPixformatEnable(bool on) { m_newPixformat = on; }
    void mainTerminalInputEnable(bool on) { m_mainTerminalInput = on; }
    void bootloaderHS(bool on) { m_bootloaderHS = on; }
    void bootloaderFastMode(bool on) { m_bootloaderFastMode = on; }

signals:

    void firmwareVersion(int major, int minor, int patch);
    void frameBufferData(const QPixmap &data);
    void frameBufferEmpty(bool ok);
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
    void printEmpty(bool ok);
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
    void queueEmpty();
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
    bool m_newPixformat;
    bool m_mainTerminalInput;
    bool m_bootloaderHS;
    bool m_bootloaderFastMode;
};

#endif // OPENMVPLUGINIO_H
