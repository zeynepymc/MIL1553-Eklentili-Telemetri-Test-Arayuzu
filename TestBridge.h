#ifndef TESTBRIDGE_H
#define TESTBRIDGE_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QList>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QRegularExpression>

class TestBridge : public QObject
{
    Q_OBJECT
public:
    explicit TestBridge(QObject *parent = nullptr);

    // QML'den çağrılacak Buton Fonksiyonları
    Q_INVOKABLE void requestSensorData();
    Q_INVOKABLE void sendPowerCommand(bool powerOn);
    Q_INVOKABLE void updateMilSettings(int rt, int sensorSa, int powerSa);

    // NIC ayarları fonksiyonları
    Q_INVOKABLE void requestNicConfig();
    Q_INVOKABLE void updateNicConfig(QString ip, QString mac, QString rx, QString tx);

    // Manuel komut girişi için komut paneli bu fonksiyonu çağırır
    Q_INVOKABLE void sendRawCommand(const QString &rawCommand);

    Q_INVOKABLE void requestSpaceWireData();

signals:
    void telemetryReceived(float temp, int light, QString state); //karta veri ulastı sinyali
    void milReceived(); // karta MIL-STD-1553 ile veri geldi sinyali
    void spwReceived();
    void logAdded(QString logMsg);
    void commandTimeout(QString command); // komut hedefe ulasamadı

    // Karttan gelen NIC bilgilerini QML'e aktaracak sinyal
    void nicConfigReceived(QString ip, QString mac, QString rx, QString tx);
    void safeStateReceived();

private slots:
    void readDatagrams(); // UDP'den paket geldiğinde tetiklenir
    void onResponseTimeout(); // Yanıt bekleme süresi doldugunda tetiklenir
    void onNicResponseTimeout(); // NIC/port değişikliği kart tarafından onaylanmadıgında tetiklenir

private:
    QUdpSocket *udpSocket;

    // Test Arayüzü 5003'ü dinler, karta (5001'e) komut gönderir
    int RX_PORT = 5003; // Test Arayüzü 5003'ü dinler
    int TX_PORT = 5001; // Kartın gerçek dinleme (rx) portu
    QString TARGET_IP = "127.0.0.1";

    int targetRt = 15;
    int targetSensorSa = 1;
    int targetPowerSa = 2;

    // Timeout Mekanizması (MIL-1553 komutları için)
    QTimer *responseTimeoutTimer;
    QString lastSentCommand;
    int retryCount = 0;
    const int MAX_RETRIES = 2;
    const int RESPONSE_TIMEOUT_MS = 200;

    // NIC/port değişikliği için ayrı bir onay-zaman-aşımı mekanizması
    QTimer *nicResponseTimeoutTimer;
    const int NIC_RESPONSE_TIMEOUT_MS = 500;
    QString pendingOldIp;
    int pendingOldTxPort = 0;
    int pendingOldRxPort = 0;

    void sendMilCommand(int targetRt, int tr, int sa, int wc, QList<quint16> dataWords = QList<quint16>());

    //  NIC ve MIL ayar değişikliklerini kalıcı olarak txt dosyasına kaydeder (flash memory simülasyonu)
    void saveToTestLog(const QString &entry);
};
#endif