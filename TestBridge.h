#ifndef TESTBRIDGE_H
#define TESTBRIDGE_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QList>
#include <QTimer>

class TestBridge : public QObject
{
    Q_OBJECT
public:
    explicit TestBridge(QObject *parent = nullptr);

    // QML'den Çağrılacak Buton Fonksiyonları
    Q_INVOKABLE void requestSensorData();
    Q_INVOKABLE void sendPowerCommand(bool powerOn);
    Q_INVOKABLE void updateMilSettings(int rt, int sensorSa, int powerSa);

    // YENİ: NIC Ayarları Fonksiyonları
    Q_INVOKABLE void requestNicConfig();
    Q_INVOKABLE void updateNicConfig(QString ip, QString mac, QString rx, QString tx);

signals:
    void telemetryReceived(float temp, int light, QString state);
    void milReceived();
    void logAdded(QString logMsg);
    void commandTimeout(QString command);

    // YENİ: KART'tan gelen NIC bilgilerini QML'e aktaracak sinyal
    void nicConfigReceived(QString ip, QString mac, QString rx, QString tx);
    void safeStateReceived();

private slots:
    void readDatagrams();
    void onResponseTimeout(); // Yanıt bekleme süresi doldu[cite: 3]
    void onNicResponseTimeout(); // NIC/port değişikliği KART tarafından onaylanmadı

private:
    QUdpSocket *udpSocket;

    // Test Arayüzü 5003'ü dinler, KART'a (5001'e) komut gönderir.
    int RX_PORT = 5003; // Test Arayüzü 5003'ü dinler
    // DÜZELTME: KART'ın gerçek dinleme (rx) portu 5001'dir. Burası eskiden
    // yanlışlıkla 5000 idi; bu yüzden başlangıçta KART'a giden hiçbir komut
    // (GET_NIC, SET_NIC, MIL_BC...) hedefine ulaşmıyordu.
    int TX_PORT = 5001;
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

    // YENİ: NIC/port değişikliği için AYRI bir onay-zaman-aşımı mekanizması.
    // MIL protokolündeki responseTimeoutTimer'dan bilerek bağımsız tutuldu.
    // updateNicConfig() öncesinde SET_NIC "gönder ve unut" şeklindeydi; KART
    // hiç yanıt vermese bile ne bir hata görünüyordu ne de eski ayarlara
    // dönülüyordu. Bu, tam olarak sessizce bağlanamama sorununun kaynağıydı.
    QTimer *nicResponseTimeoutTimer;
    const int NIC_RESPONSE_TIMEOUT_MS = 500;
    QString pendingOldIp;
    int pendingOldTxPort = 0;
    int pendingOldRxPort = 0;

    void sendMilCommand(int targetRt, int tr, int sa, int wc, QList<quint16> dataWords = QList<quint16>());
};
#endif