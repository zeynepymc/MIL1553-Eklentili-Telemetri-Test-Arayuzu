#include "TestBridge.h"
#include <QTimer>

TestBridge::TestBridge(QObject *parent) : QObject(parent)
{
    udpSocket = new QUdpSocket(this);

    // Port kilitlenmesini önlemek için AnyIPv4 ve ShareAddress kullanıyoruz
    bool isBound = udpSocket->bind(QHostAddress::AnyIPv4, RX_PORT, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
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
    responseTimeoutTimer->setSingleShot(true);
    connect(responseTimeoutTimer, &QTimer::timeout, this, &TestBridge::onResponseTimeout);

    nicResponseTimeoutTimer = new QTimer(this);
    nicResponseTimeoutTimer->setSingleShot(true);
    connect(nicResponseTimeoutTimer, &QTimer::timeout, this, &TestBridge::onNicResponseTimeout);
}

void TestBridge::updateMilSettings(int rt, int sensorSa, int powerSa) {
    targetRt = rt; targetSensorSa = sensorSa; targetPowerSa = powerSa;
    emit logAdded(QString("[BC-SİSTEM] MIL-1553 Ayarları Değişti -> Hedef RT: %1, Sensör SA: %2, Güç SA: %3").arg(rt).arg(sensorSa).arg(powerSa));
}

void TestBridge::requestNicConfig() {
    QString cmd = "$GET_NIC#";
    udpSocket->writeDatagram(cmd.toUtf8(), QHostAddress(TARGET_IP), TX_PORT);
    emit logAdded("[TX-CMD] " + cmd);
}

void TestBridge::updateNicConfig(QString ip, QString mac, QString rx, QString tx) {
    // SADECE KOMUTU GÖNDER - Portları burada ASLA değiştirme!
    QString cmd = QString("$SET_NIC,%1,%2,%3,%4#").arg(ip).arg(mac).arg(rx).arg(tx);
    udpSocket->writeDatagram(cmd.toUtf8(), QHostAddress(TARGET_IP), TX_PORT);
    emit logAdded("[TX-CMD] " + cmd);

    pendingOldIp = TARGET_IP;
    pendingOldTxPort = TX_PORT;
    pendingOldRxPort = RX_PORT;

    nicResponseTimeoutTimer->start(NIC_RESPONSE_TIMEOUT_MS);
}

void TestBridge::requestSensorData() {
    sendMilCommand(targetRt, 1, targetSensorSa, 3);
}

void TestBridge::sendPowerCommand(bool powerOn) {
    QList<quint16> data; data.append(powerOn ? 0x0001 : 0x0000);
    sendMilCommand(targetRt, 0, targetPowerSa, 1, data);
}

void TestBridge::sendMilCommand(int targetRt, int tr, int sa, int wc, QList<quint16> dataWords) {
    int wcField = (wc == 32) ? 0 : wc;
    quint16 cw = ((targetRt & 0x1F) << 11) | ((tr & 0x1) << 10) | ((sa & 0x1F) << 5) | (wcField & 0x1F);

    QString cmd = QString("$MIL_BC,%1").arg(cw, 4, 16, QChar('0')).toUpper();
    for (quint16 dw : dataWords) cmd += QString(",%1").arg(dw, 4, 16, QChar('0')).toUpper();
    cmd += "#";

    udpSocket->writeDatagram(cmd.toUtf8(), QHostAddress(TARGET_IP), TX_PORT);
    emit logAdded("[TX] " + cmd);

    lastSentCommand = cmd;
    retryCount = 0;
    responseTimeoutTimer->start(RESPONSE_TIMEOUT_MS);
}

void TestBridge::readDatagrams() {
    while (udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = udpSocket->receiveDatagram();
        QString packet = QString::fromUtf8(datagram.data()).trimmed();
        emit logAdded("[RX] " + packet);

        if (packet.startsWith("$MIL_RT") || packet.startsWith("$NIC_INFO") || packet.startsWith("$DATA")) {
            responseTimeoutTimer->stop();
        }

        if (packet.startsWith("$MIL_RT") && packet.split(',').size() == 5) {
            QStringList parts = packet.mid(1, packet.length() - 2).split(',');
            bool okTemp = false, okLight = false, okState = false;
            quint16 rawTemp = parts[2].toUShort(&okTemp, 16);
            quint16 rawLight = parts[3].toUShort(&okLight, 16);
            quint16 stateHex = parts[4].toUShort(&okState, 16);

            if (okTemp && okLight && okState) {
                float temp = rawTemp / 100.0f;
                int light = rawLight;
                QString state;
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
        else if (packet.startsWith("$NIC_INFO") && packet.split(',').size() == 5) {
            nicResponseTimeoutTimer->stop();

            QStringList parts = packet.mid(1, packet.length() - 2).split(',');
            TARGET_IP = parts[1];
            int confirmedKartRx = parts[3].toInt();
            int confirmedKartTx = parts[4].toInt();

            TX_PORT = confirmedKartRx;

            if (RX_PORT != confirmedKartTx) {
                QTimer::singleShot(10, this, [this, confirmedKartTx]() {
                    // İşletim sisteminden portu anında kopar ve soketi tamamen sıfırla
                    udpSocket->abort();
                    udpSocket->deleteLater();
                    udpSocket = new QUdpSocket(this);

                    connect(udpSocket, &QUdpSocket::readyRead, this, &TestBridge::readDatagrams);

                    if (udpSocket->bind(QHostAddress::AnyIPv4, confirmedKartTx, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
                        RX_PORT = confirmedKartTx;
                        emit logAdded("[SİSTEM] Test Arayüzü dinleme portu güncellendi: " + QString::number(RX_PORT));
                    } else {
                        emit logAdded("[HATA] Yeni dinleme portu açılamadı! Hata: " + udpSocket->errorString());
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

void TestBridge::onNicResponseTimeout() {
    emit logAdded("[NIC-HATA] KART'tan port değişikliği onayı (NIC_INFO) alınamadı - bağlantı doğrulanamadı.");

    bool needRebind = (RX_PORT != pendingOldRxPort);

    TARGET_IP = pendingOldIp;
    TX_PORT = pendingOldTxPort;

    if (needRebind) {
        udpSocket->abort();
        if (udpSocket->bind(QHostAddress::AnyIPv4, pendingOldRxPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
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