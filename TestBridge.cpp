#include "TestBridge.h"
#include <QTimer>
#include <QPointer>

TestBridge::TestBridge(QObject *parent) : QObject(parent)
{
    udpSocket = new QUdpSocket(this);
    // QUdpSocket::DontShareAddress -> Başka bir programın bu portu gizlice dinlemesini engeller
    bool isBound = udpSocket->bind(QHostAddress::AnyIPv4, RX_PORT, QUdpSocket::DontShareAddress | QUdpSocket::ReuseAddressHint);
    connect(udpSocket, &QUdpSocket::readyRead, this, &TestBridge::readDatagrams);

    QTimer::singleShot(1000, this, [this, isBound]() {
        if (isBound) {
            emit logAdded("[SİSTEM BİLGİSİ] BC Ağ Portu başarıyla bağlandı ve dinleniyor.");
        } else {
            QString errorMsg = udpSocket->errorString();
            emit logAdded("[SİSTEM HATASI] BC PORTU AÇILAMADI! Neden: " + errorMsg);
            emit logAdded("[ÇÖZÜM ÖNERİSİ] Güvenlik duvarını kontrol edin veya portu kullanan başka bir programı kapatın.");
        }
    });

    responseTimeoutTimer = new QTimer(this);
    responseTimeoutTimer->setSingleShot(true); // Zamanlayıcılar süresi dolduğunda durur
    connect(responseTimeoutTimer, &QTimer::timeout, this, &TestBridge::onResponseTimeout);

    nicResponseTimeoutTimer = new QTimer(this);
    nicResponseTimeoutTimer->setSingleShot(true);
    connect(nicResponseTimeoutTimer, &QTimer::timeout, this, &TestBridge::onNicResponseTimeout);
}

// MIL ayarlarının guncellenmesi
void TestBridge::updateMilSettings(int rt, int sensorSa, int powerSa) {
    targetRt = rt; targetSensorSa = sensorSa; targetPowerSa = powerSa;
    QString msg = QString("MIL-1553 Ayarları Değişti -> Hedef RT: %1, Sensör SA: %2, Güç SA: %3").arg(rt).arg(sensorSa).arg(powerSa);
    emit logAdded("[BC-SİSTEM] " + msg);
    saveToTestLog(msg); // ayar değişikliği kalıcı olarak kaydedilir
}

void TestBridge::requestNicConfig() {
    QString cmd = "$GET_NIC#";
    udpSocket->writeDatagram(cmd.toUtf8(), QHostAddress(TARGET_IP), TX_PORT);
    emit logAdded("[TX-CMD] " + cmd);
}

// NIC Ayarlarının Guncellenmesi
void TestBridge::updateNicConfig(QString ip, QString mac, QString rx, QString tx) {
    // sadece komut gonderilir, ayarlar guncellenmez
    QString cmd = QString("$SET_NIC,%1,%2,%3,%4#").arg(ip).arg(mac).arg(rx).arg(tx);
    udpSocket->writeDatagram(cmd.toUtf8(), QHostAddress(TARGET_IP), TX_PORT);
    emit logAdded("[TX-CMD] " + cmd);
    saveToTestLog(QString("NIC DEGISIKLIGI ISTENDI -> IP:%1 MAC:%2 RX:%3 TX:%4").arg(ip, mac, rx, tx));

    pendingOldIp = TARGET_IP;
    pendingOldTxPort = TX_PORT;
    pendingOldRxPort = RX_PORT;

    nicResponseTimeoutTimer->start(NIC_RESPONSE_TIMEOUT_MS);
}

// karttan güncel sıcaklık, ışık ve sistem durumu verilerini ister
void TestBridge::requestSensorData() {
    sendMilCommand(targetRt, 1, targetSensorSa, 3);
}

// Güç aç komutu (1) veya güç kapat komutu (0) için 16-bitlik bir veri listesi oluşturur, sendMilCommand fonksiyonuna gonderir
void TestBridge::sendPowerCommand(bool powerOn) {
    QList<quint16> data; data.append(powerOn ? 0x0001 : 0x0000);
    sendMilCommand(targetRt, 0, targetPowerSa, 1, data);
}

//
void TestBridge::sendMilCommand(int targetRt, int tr, int sa, int wc, QList<quint16> dataWords) {
    int wcField = (wc == 32) ? 0 : wc;
    // 16-bitlik Command Word (Komut Sözcüğü) üretilir
    quint16 cw = ((targetRt & 0x1F) << 11) | ((tr & 0x1) << 10) | ((sa & 0x1F) << 5) | (wcField & 0x1F);

    QString cmd = QString("$MIL_BC,%1").arg(cw, 4, 16, QChar('0')).toUpper();
    for (quint16 dw : dataWords) cmd += QString(",%1").arg(dw, 4, 16, QChar('0')).toUpper();
    cmd += "#"; // Oluşturulan 16-bit sayı (cw), Hexadecimal (16'lık taban) stringe çevirilir

    udpSocket->writeDatagram(cmd.toUtf8(), QHostAddress(TARGET_IP), TX_PORT);
    emit logAdded("[TX] " + cmd);

    lastSentCommand = cmd;
    retryCount = 0;
    responseTimeoutTimer->start(RESPONSE_TIMEOUT_MS);
}

// Manuel komut girişi
void TestBridge::sendRawCommand(const QString &rawCommand)
{
    QString cmd = rawCommand.trimmed();

    // satır sonu karakterleri temizlenir (log injection / paket sınırlarının bozulmasını önlemek için)
    cmd.remove(QRegularExpression("[\\r\\n]"));

    if (cmd.isEmpty()) {
        emit logAdded("[HATA] Boş komut gönderilemez.");
        return;
    }
    if (cmd.size() > 256) {
        emit logAdded("[HATA] Komut çok uzun, gönderilmedi (maks 256 karakter).");
        return;
    }

    // Kullanıcı $ ve # işaretlerini yazmayı unutursa otomatik tamamlanır
    if (!cmd.startsWith('$')) cmd.prepend('$');
    if (!cmd.endsWith('#'))   cmd.append('#');

    udpSocket->writeDatagram(cmd.toUtf8(), QHostAddress(TARGET_IP), TX_PORT);
    emit logAdded("[TX-MANUEL] " + cmd);
}

void TestBridge::readDatagrams() {
    while (udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpSocket->receiveDatagram();

        QString senderIp = datagram.senderAddress().toString();
        if (senderIp.startsWith("::ffff:")) senderIp = senderIp.mid(7); // IP'yi normale çevirir

        // Kaynak IP kontrolü
        // Sadece bilinen kart IP'sinden (TARGET_IP) gelen paketler işlenir
        if (senderIp != TARGET_IP) {
            emit logAdded("[GUVENLIK] Beklenmeyen kaynaktan paket reddedildi: " + senderIp);
            continue;
        }

        //paket okunabilir bir metne çevrilir
        QString packet = QString::fromUtf8(datagram.data()).trimmed();
        emit logAdded("[RX] " + packet);


        if (packet.startsWith("$MIL_RT") || packet.startsWith("$NIC_INFO") || packet.startsWith("$DATA")) {
            responseTimeoutTimer->stop();
        }

        // Kartın tanımadığı komutlar için NACK yanıtı
        // manuel panelden hatalı bir komut gönderildiğinde bunu log ekranına yazar
        if (packet.startsWith("$NACK")) {
            emit logAdded("[KART-UYARI] Kart bu komutu tanımadı: " + packet);
        }

        // MIL-1553 yanıtı gelmesi durumu
        if (packet.startsWith("$MIL_RT") && packet.split(',').size() == 5) {
            QStringList parts = packet.mid(1, packet.length() - 2).split(','); // baştaki ve sondaki işaretleri siler ve virgullerden parcalar
            bool okTemp = false, okLight = false, okState = false;
            quint16 rawTemp = parts[2].toUShort(&okTemp, 16); // hex tabanlı sayıları 16-bit tamsayıya çevirir
            quint16 rawLight = parts[3].toUShort(&okLight, 16);
            quint16 stateHex = parts[4].toUShort(&okState, 16);
            if (okTemp && okLight && okState) {
                float temp = rawTemp / 100.0f; // kart tarafında hassasiyetin kaybolmaması için 100 ile çarpılarak gönderilen sıcaklık verisi tekrar 100'e bölerek gerçek sıcaklık değerine (float) dönüştürülür
                int light = rawLight;
                QString state;
                // Hex kodlarına bakarak sistemin durumu belirlenir
                if (stateHex == 0x0000) state = "NORMAL";
                else if (stateHex == 0x0001) state = "WARNING";
                else state = "CRITICAL";

                emit telemetryReceived(temp, light, state);
                emit milReceived();
            } else {
                emit logAdded("[HATA] Bozuk MIL_RT paketi: " + packet);
            }
        }
        else if (packet.startsWith("$MIL_RT") && packet.split(',').size() == 2) {
            emit logAdded("[BC-SİSTEM] KART komutu başarıyla aldı.");
            emit milReceived();
        }
        else if (packet == "$DATA,0,0,SAFE_STATE#") {
            emit logAdded("[SİSTEM BİLGİSİ] KART Güvenli Moda (SAFE_STATE) geçti. Güç kesildi.");
            emit safeStateReceived();
            emit telemetryReceived(0.0f, 0, "SAFE_STATE");
        }
        // Ağ Ayarlarının (NIC) Güncellenmesi
        else if (packet.startsWith("$NIC_INFO") && packet.split(',').size() == 5) {
            nicResponseTimeoutTimer->stop();

            QStringList parts = packet.mid(1, packet.length() - 2).split(',');
            TARGET_IP = parts[1];

            bool okTx = false, okRx = false;
            int confirmedKartRx = parts[3].toInt(&okTx);
            int confirmedKartTx = parts[4].toInt(&okRx);
            if (!okTx || !okRx) {
                emit logAdded("[HATA] Bozuk NIC_INFO paketi, port bilgisi ayrıştırılamadı: " + packet);
                continue;
            }

            TX_PORT = confirmedKartRx;
            saveToTestLog(QString("NIC AYARLARI DOGRULANDI -> IP:%1 MAC:%2 KartRX:%3 KartTX:%4")
                              .arg(parts[1], parts[2]).arg(confirmedKartRx).arg(confirmedKartTx));

            // Eğer test arayüzünün dinlediği port da değişecekse, burası çalışır
            if (RX_PORT != confirmedKartTx) {
                QPointer<TestBridge> safeThis(this);
                QTimer::singleShot(10, this, [safeThis, confirmedKartTx]() {
                    if (!safeThis) return;
                    // İşletim sisteminden portu anında kopar ve soketi tamamen sıfırla
                    safeThis->udpSocket->abort();
                    safeThis->udpSocket->deleteLater();
                    safeThis->udpSocket = new QUdpSocket(safeThis);

                    connect(safeThis->udpSocket, &QUdpSocket::readyRead, safeThis, &TestBridge::readDatagrams);

                    if (safeThis->udpSocket->bind(QHostAddress::AnyIPv4, confirmedKartTx, QUdpSocket::DontShareAddress | QUdpSocket::ReuseAddressHint)) {
                        safeThis->RX_PORT = confirmedKartTx;
                        emit safeThis->logAdded("[SİSTEM] Test Arayüzü dinleme portu güncellendi: " + QString::number(safeThis->RX_PORT));
                    } else {
                        emit safeThis->logAdded("[HATA] Yeni dinleme portu açılamadı! Hata: " + safeThis->udpSocket->errorString());
                    }
                });
            }

            emit nicConfigReceived(parts[1], parts[2], parts[3], parts[4]);
            emit logAdded("[SİSTEM] KART ile senkronize olundu. Yeni Hedef Port: " + QString::number(TX_PORT));
        }
    }
}

void TestBridge::onResponseTimeout() {
    if (retryCount < MAX_RETRIES) {
        retryCount++;
        emit logAdded(QString("[TIMEOUT] Yanıt alınamadı, yeniden deneniyor (%1/%2)...").arg(retryCount).arg(MAX_RETRIES));
        udpSocket->writeDatagram(lastSentCommand.toUtf8(), QHostAddress(TARGET_IP), TX_PORT);
        responseTimeoutTimer->start(RESPONSE_TIMEOUT_MS);
    } else {
        emit logAdded("[HATA] RT yanıt vermiyor (No Response) - bus üzerinde cevap alınamadı.");
        emit commandTimeout(lastSentCommand);
    }
}

// Donanıma komut gidip 200ms boyunca yanıt gelmezse tetiklenir
void TestBridge::onNicResponseTimeout() {
    emit logAdded("[NIC-HATA] KART'tan port değişikliği onayı (NIC_INFO) alınamadı - bağlantı doğrulanamadı.");

    bool needRebind = (RX_PORT != pendingOldRxPort);

    TARGET_IP = pendingOldIp;
    TX_PORT = pendingOldTxPort;
    saveToTestLog("NIC DEGISIKLIGI ZAMAN ASIMINA UGRADI, ESKI AYARLARA DONULUYOR");

    if (needRebind) {
        udpSocket->abort();
        if (udpSocket->bind(QHostAddress::AnyIPv4, pendingOldRxPort, QUdpSocket::DontShareAddress | QUdpSocket::ReuseAddressHint)) {
            RX_PORT = pendingOldRxPort;
            emit logAdded(QString("[NIC-SİSTEM] Doğrulanamayan değişiklik geri alındı, eski port (%1) geri yüklendi.").arg(RX_PORT));
        } else {
            emit logAdded(QString("[NIC-HATA] Eski porta (%1) da geri dönülemedi! Neden: %2")
                              .arg(pendingOldRxPort).arg(udpSocket->errorString()));
        }
    } else {
        emit logAdded(QString("[NIC-SİSTEM] Doğrulanamayan değişiklik geri alındı, hedef port (%1) eski haline döndürüldü.").arg(TX_PORT));
    }
}

// Ayar değişiklikleri (NIC, MIL) kalıcı olarak dosyaya yazılır (flash memory simülasyonu)
void TestBridge::saveToTestLog(const QString &entry)
{
    QString cleanEntry = entry;
    cleanEntry.remove(QRegularExpression("[\\r\\n]")); // log injection önleme

    QFile file("test_settings_log.txt");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        QString timeStamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        out << "[" << timeStamp << "] " << cleanEntry << "\n";
        file.close();
    }
}