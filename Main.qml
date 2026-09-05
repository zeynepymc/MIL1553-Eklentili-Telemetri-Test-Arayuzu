import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 1000
    height: 650
    title: "MIL-1553 & Sistem Kontrol Arayüzü"
    color: "#1e1e1e"

    property bool isSystemPowered: true

    Connections {
        target: testBridge
        function onTelemetryReceived(temp, light, state) {
            if (isSystemPowered) {
                cpuLed.blink(); ramLed.blink(); spwLed.blink(); flashLed.blink();
            }
        }
        function onMilReceived() { milLed.blink() }

        // YENİ: Gelen NIC verilerini Pop-up'taki kutulara yaz
        function onNicConfigReceived(ip, mac, rx, tx) {
            ipInput.text = ip
            macInput.text = mac
            rxInput.text = rx
            txInput.text = tx
        }

        // YENİ: KART gücü kapattığında sistemi tamamen durdurur
        function onSafeStateReceived() {
            isSystemPowered = false
            autoPollSwitch.checked = false
        }

        function onLogAdded(msg) {
            // DÜZELTME: Güç yokken gelen ağ paketlerinde NIC LED tepki vermesin
            if (isSystemPowered && (msg.indexOf("[RX]") !== -1 || msg.indexOf("[TX]") !== -1)) {
                nicLed.blink()
            }

            var time = new Date().toLocaleTimeString(Qt.locale(), "hh:mm:ss")
            var fullMsg = "[" + time + "] " + msg
            var vbar = logScroll.ScrollBar.vertical
            var isAtBottom = (vbar.position >= 1.0 - vbar.size - 0.05) || (vbar.size === 1.0)
            if (logArea.length > 5000) logArea.remove(0, 2000)
            logArea.insert(logArea.length, fullMsg + "\n")
            if (isAtBottom) Qt.callLater(function() { vbar.position = 1.0 - vbar.size })
        }
    }

    Timer {
            id: sensorPoller
            interval: 1500
            running: autoPollSwitch.checked
            repeat: true
            onTriggered: testBridge.requestSensorData()
        }

    // --- NIC AYARLARI POP-UP ---
    Popup {
        id: nicPopup
        anchors.centerIn: parent
        width: 350
        height: 400
        modal: true
        focus: true
        background: Rectangle { color: "#252526"; radius: 10; border.color: "#555"; border.width: 2 }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Text { text: "⚙️ KART Ağ Ayarları (NIC)"; color: "white"; font.bold: true; font.pixelSize: 15; Layout.alignment: Qt.AlignHCenter }

            Label { text: "IP Adresi:"; color: "#bbb"; font.pixelSize: 12 }
            TextField { id: ipInput; text: "Bekleniyor..."; Layout.fillWidth: true; color: "black"; font.bold: true }

            Label { text: "MAC Adresi:"; color: "#bbb"; font.pixelSize: 12 }
            TextField { id: macInput; text: "Bekleniyor..."; Layout.fillWidth: true; color: "black"; font.bold: true }

            Label { text: "RX Port (KART'ın Dinlediği):"; color: "#bbb"; font.pixelSize: 12 }
            TextField { id: rxInput; text: "Bekleniyor..."; Layout.fillWidth: true; color: "black"; font.bold: true }

            Label { text: "TX Port (KART'ın Gönderdiği):"; color: "#bbb"; font.pixelSize: 12 }
            TextField { id: txInput; text: "Bekleniyor..."; Layout.fillWidth: true; color: "black"; font.bold: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Layout.topMargin: 10

                Button {
                    text: "SORGULA (GET)"
                    Layout.fillWidth: true
                    onClicked: testBridge.requestNicConfig()
                }

                Button {
                    text: "KAYDET (SET)"
                    Layout.fillWidth: true
                    onClicked: {
                        testBridge.updateNicConfig(ipInput.text, macInput.text, rxInput.text, txInput.text)
                        nicPopup.close()
                    }
                }
            }

            Button {
                text: "Kapat"
                Layout.alignment: Qt.AlignHCenter
                onClicked: nicPopup.close()
            }
        }
    }

    // --- MIL AYARLARI POP-UP ---
    Popup {
        id: milPopup
        anchors.centerIn: parent
        width: 420
        height: 380
        modal: true
        focus: true
        background: Rectangle { color: "#252526"; radius: 10; border.color: "#555"; border.width: 2 }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Text { text: "⚙️ MIL-STD-1553 Format & Ayarları"; color: "white"; font.bold: true; font.pixelSize: 15; Layout.alignment: Qt.AlignHCenter }

            Rectangle {
                Layout.fillWidth: true
                height: 50
                color: "#1e1e1e"
                border.color: "#444"
                radius: 5
                ColumnLayout {
                    anchors.centerIn: parent
                    Text { text: "Command Word (16-Bit) Formatı:"; color: "#aaa"; font.pixelSize: 11; font.bold: true; Layout.alignment: Qt.AlignHCenter }
                    Text { text: "[15-11] RT | [10] T/R | [9-5] SubAddr | [4-0] WordCount"; color: "#00ffcc"; font.pixelSize: 11; font.family: "Consolas"; Layout.alignment: Qt.AlignHCenter }
                }
            }

            Label { text: "Hedef Terminal (RT) Adresi (0-31):"; color: "#bbb"; font.pixelSize: 12 }
            TextField { id: rtInput; text: "15"; Layout.fillWidth: true; color: "black"; font.bold: true }

            Label { text: "Sensör Verisi Alt Adresi (SA) (1-30):"; color: "#bbb"; font.pixelSize: 12 }
            TextField { id: sensorSaInput; text: "1"; Layout.fillWidth: true; color: "black"; font.bold: true }

            Label { text: "Güç Kontrol Alt Adresi (SA) (1-30):"; color: "#bbb"; font.pixelSize: 12 }
            TextField { id: powerSaInput; text: "2"; Layout.fillWidth: true; color: "black"; font.bold: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Layout.topMargin: 10

                Button {
                    text: "İPTAL"
                    Layout.fillWidth: true
                    onClicked: milPopup.close()
                }

                Button {
                    text: "AYARLARI KAYDET"
                    Layout.fillWidth: true
                    onClicked: {
                        testBridge.updateMilSettings(parseInt(rtInput.text), parseInt(sensorSaInput.text), parseInt(powerSaInput.text))
                        milPopup.close()
                    }
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Button {
                text: "GÜCÜ KES"
                font.bold: true; Layout.preferredHeight: 45; width: 90
                onClicked: {
                    isSystemPowered = false
                    testBridge.sendPowerCommand(false)
                    autoPollSwitch.checked = false
                }
            }

            Button {
                text: "GÜCÜ VER"
                font.bold: true; Layout.preferredHeight: 45; width: 90
                onClicked: {
                    isSystemPowered = true
                    testBridge.sendPowerCommand(true)
                    autoPollSwitch.checked = true
                }
            }

            // NIC Ayarları
            Button {
                text: "🌐 NIC AYARLARI"
                font.bold: true; Layout.preferredHeight: 45; width: 130
                onClicked: {
                    nicPopup.open()
                    testBridge.requestNicConfig() // Açılırken otomatik sorgula
                }
            }

            Button {
                text: "⚙️ MIL AYARLARI"
                font.bold: true; Layout.preferredHeight: 45; width: 130
                onClicked: milPopup.open()
            }

            Switch {
                id: autoPollSwitch
                text: "Oto-Poll"
                font.bold: true
                font.pixelSize: 13
                checked: true
            }

            Item { Layout.fillWidth: true }

            RowLayout {
                spacing: 18
                component LedIndicator: Column {
                    property string name: ""
                    spacing: 5

                    Rectangle {
                        id: ledCircle
                        width: 20; height: 20; radius: 10
                        color: "#222222"
                        border.color: "#000"; border.width: 1
                        anchors.horizontalCenter: parent.horizontalCenter

                        SequentialAnimation {
                            id: blinkAnim
                            ColorAnimation { target: ledCircle; property: "color"; from: "#00ff00"; to: "#222222"; duration: 250 }
                        }
                    }

                    Text {
                        text: name; color: "#e0e0e0"; font.bold: true; font.pixelSize: 11
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    function blink() { blinkAnim.restart() }
                }

                LedIndicator { id: nicLed; name: "NIC" }
                LedIndicator { id: cpuLed; name: "CPU" }
                LedIndicator { id: ramLed; name: "RAM" }
                LedIndicator { id: spwLed; name: "SpW" }
                LedIndicator { id: flashLed; name: "FLASH" }
                LedIndicator { id: milLed; name: "MIL" }
            }
        }

        Text { text: "Arka Planda Akan Ağ Trafiği & MIL-1553 Hex Logları:"; color: "#aaa"; font.bold: true; font.pixelSize: 14 }

        ScrollView {
            id: logScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { color: "#0d0d0d"; radius: 5; border.color: "#333"; border.width: 1 }

            TextArea {
                id: logArea
                readOnly: true
                background: null
                color: "#4ade80"
                font.family: "Consolas"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }
    }
}