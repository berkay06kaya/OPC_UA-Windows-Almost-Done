import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 1100
    height: 720
    minimumWidth: 860
    minimumHeight: 560
    title: "OPC UA - Baglanti Testi"

    property int selectedIndex: -1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // ---- Ust cubuk: baglanti durumu, modbus durumu, ayarlar ----
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            spacing: 10

            Rectangle {
                Layout.preferredHeight: 28
                Layout.preferredWidth: statusText.implicitWidth + 24
                radius: 14
                color: opc.connectionState === 3 ? "#2e7d32"
                     : opc.connectionState === 4 ? "#ef6c00"
                     : (opc.connectionState === 1 || opc.connectionState === 2)
                                                 ? "#1565c0"
                                                 : "#c62828"
                Text {
                    id: statusText
                    anchors.centerIn: parent
                    text: opc.connectionStateText
                    color: "white"
                    font.bold: true
                    font.pixelSize: 12
                }
            }

            Item { Layout.fillWidth: true }

            Row {
                spacing: 5
                Layout.alignment: Qt.AlignVCenter
                Rectangle {
                    width: 8; height: 8; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: opc.modbus.modbusServerRunning ? "#2e7d32" : "#bdbdbd"
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: opc.modbus.modbusServerRunning ? "Modbus calisiyor" : "Modbus durduruldu"
                    font.pixelSize: 11
                    color: "#666666"
                }
            }

            ToolButton {
                text: "⚙"
                implicitWidth: 32
                implicitHeight: 28
                font.pixelSize: 16
                ToolTip.text: "Ayarlar (baglanti / Modbus sunucu)"
                ToolTip.visible: hovered
                ToolTip.delay: 400
                onClicked: settingsPopup.visible ? settingsPopup.close() : settingsPopup.open()
            }
        }

        // ---- Ana alan: sol/sag bolunmus, altta log - hepsi kaydirilabilir ----
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical

            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal

                Frame {
                    id: leftPane
                    SplitView.minimumWidth: 220
                    SplitView.preferredWidth: 380
                    padding: 6

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        visible: opc.connectionState === 3

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            ToolButton {
                                text: "▲"
                                implicitWidth: 28
                                implicitHeight: 26
                                enabled: opc.treeDepth > 1
                                onClicked: opc.browseUp()
                                ToolTip.text: "Yukari"
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                            }
                            Text {
                                Layout.fillWidth: true
                                text: opc.treePath
                                elide: Text.ElideLeft
                                font.pixelSize: 11
                                color: "#666666"
                                verticalAlignment: Text.AlignVCenter
                            }
                            BusyIndicator {
                                running: opc.treeBusy
                                visible: opc.treeBusy
                                implicitWidth: 18; implicitHeight: 18
                            }
                            Button {
                                text: "Tumunu Izle"
                                implicitHeight: 26
                                onClicked: opc.subscribeAllInCurrentFolder()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: opc.serverCapacityText.length > 0
                            text: opc.serverCapacityText
                            font.pixelSize: 10
                            color: "#888888"
                            elide: Text.ElideRight
                        }

                        ListView {
                            id: treeList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: opc.treeChildren
                            delegate: Rectangle {
                                width: treeList.width
                                height: 32
                                color: modelData.expandable ? "transparent" : "#f5f5f5"
                                border.color: "#dddddd"
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    elide: Text.ElideRight
                                    text: (modelData.expandable ? "▶ " : "• ") + modelData.displayName
                                    color: modelData.expandable ? "black" : "#555555"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: opc.browseInto(index)
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        visible: opc.connectionState !== 3

                        Text {
                            text: "Sunucular"
                            font.bold: true
                            font.pixelSize: 12
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: urlField
                                Layout.fillWidth: true
                                text: "opc.tcp://192.168.0.1:54545/OPCUA/CircutorPowerStudio"
                                placeholderText: "opc.tcp://sunucu:port/yol"
                            }
                            Button {
                                text: "Ara"
                                onClicked: { root.selectedIndex = -1; opc.discover(urlField.text) }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignHCenter
                            visible: endpointList.count === 0
                            text: "Bir sunucu adresi girip Ara'ya basin"
                            color: "#888888"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        ListView {
                            id: endpointList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: opc.endpoints
                            delegate: Rectangle {
                                width: endpointList.width
                                height: 36
                                radius: 4
                                color: index === root.selectedIndex ? "#bbdefb"
                                     : modelData.supported ? "transparent" : "#eeeeee"
                                border.color: "#cccccc"
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    elide: Text.ElideRight
                                    text: "[" + index + "] " + modelData.mode + " | " + modelData.policy
                                          + (modelData.supported ? "" : "   << ISTEMCI DESTEKLEMIYOR >>")
                                    color: modelData.supported ? "black" : "#999999"
                                    font.pixelSize: 11
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    enabled: modelData.supported
                                    onClicked: {
                                        root.selectedIndex = index
                                        settingsPopup.open()
                                    }
                                }
                            }
                        }
                    }
                }

                Frame {
                    id: rightPane
                    SplitView.minimumWidth: 260
                    SplitView.fillWidth: true
                    padding: 6

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 4
                        visible: opc.connectionState === 3

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Takip Edilen Etiketler"
                                font.bold: true
                                font.pixelSize: 12
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "Tumunu Donustur"
                                implicitHeight: 26
                                enabled: valuesList.count > 0
                                onClicked: {
                                    convertAllDialog.loadDefaults()
                                    convertAllDialog.open()
                                }
                            }
                            Button {
                                text: "Tumunu Cikar"
                                implicitHeight: 26
                                enabled: valuesList.count > 0
                                onClicked: opc.unsubscribeAll()
                            }
                        }

                        ListView {
                            id: valuesList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: opc.watchedValuesModel
                            delegate: Rectangle {
                                width: valuesList.width
                                height: 34
                                color: good ? "#e8f5e9" : "#fff3e0"
                                border.color: "#dddddd"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 4
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        verticalAlignment: Text.AlignVCenter
                                        text: logicalName + " = " + value
                                              + (good ? "" : "  (durum!=GOOD)")
                                              + (hasModbusMapping ? "  → " + modbusPreview : "")
                                        color: "#333333"
                                    }
                                    ToolButton {
                                        text: "✎"
                                        implicitWidth: 26; implicitHeight: 26
                                        ToolTip.text: "Gorunen adi degistir"
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 400
                                        onClicked: {
                                            renameDialog.rowIndex = index
                                            renameField.text = logicalName
                                            renameDialog.open()
                                        }
                                    }
                                    ToolButton {
                                        text: "⇄"
                                        implicitWidth: 26; implicitHeight: 26
                                        ToolTip.text: "Donusturucu eslemesi"
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 400
                                        onClicked: {
                                            converterDialog.rowIndex = index
                                            converterDialog.loadCurrent()
                                            converterDialog.open()
                                        }
                                    }
                                    ToolButton {
                                        text: "✕"
                                        implicitWidth: 26; implicitHeight: 26
                                        ToolTip.text: "Takipten cikar"
                                        ToolTip.visible: hovered
                                        ToolTip.delay: 400
                                        onClicked: opc.unsubscribeNode(index)
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: opc.connectionState !== 3
                        text: "Baglanti bekleniyor"
                        color: "#888888"
                        font.pixelSize: 13
                    }
                }
            }

            Frame {
                SplitView.preferredHeight: 160
                SplitView.minimumHeight: 70
                padding: 6

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4

                    Text {
                        text: "Gunluk"
                        font.bold: true
                        font.pixelSize: 12
                        color: "#555555"
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            text: opc.log
                            readOnly: true
                            wrapMode: TextArea.Wrap
                            font.family: "Menlo"
                            font.pixelSize: 11
                            onTextChanged: cursorPosition = length
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: settingsPopup
        x: root.width - width - 16
        y: 46
        width: 340
        modal: true
        dim: false
        focus: true
        clip: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        ColumnLayout {
            width: 300
            spacing: 8

            Text { text: "Baglanti"; font.bold: true; font.pixelSize: 13 }

            RowLayout {
                Layout.fillWidth: true
                TextField { id: userField; Layout.fillWidth: true; placeholderText: "Kullanici adi (bos = anonim)" }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: passField; Layout.fillWidth: true; placeholderText: "Sifre"; echoMode: TextInput.Password }
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "Baglan"
                    Layout.fillWidth: true
                    enabled: root.selectedIndex >= 0
                    onClicked: opc.connectToEndpoint(root.selectedIndex, userField.text, passField.text)
                }
                Button {
                    text: "Kes"
                    Layout.fillWidth: true
                    onClicked: { root.selectedIndex = -1; opc.disconnectFromServer() }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#dddddd" }

            Text { text: "Modbus Sunucu"; font.bold: true; font.pixelSize: 13 }

            Text { text: "Dinleme IP:"; font.pixelSize: 11; color: "#666666" }
            TextField {
                id: modbusBindField
                Layout.fillWidth: true
                placeholderText: "bos=tum arayuzler"
                enabled: !opc.modbus.modbusServerRunning
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: "Port:"; font.pixelSize: 11; color: "#666666" }
                    TextField {
                        id: modbusPortField
                        Layout.fillWidth: true
                        text: "1502"
                        enabled: !opc.modbus.modbusServerRunning
                        validator: IntValidator { bottom: 1; top: 65535 }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: "Unit ID:"; font.pixelSize: 11; color: "#666666" }
                    SpinBox {
                        id: modbusUnitIdField
                        Layout.fillWidth: true
                        from: 1
                        to: 247
                        value: 1
                        enabled: !opc.modbus.modbusServerRunning
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: opc.modbus.modbusServerRunning ? "Durdur" : "Baslat"
                    Layout.fillWidth: true
                    onClicked: {
                        if (opc.modbus.modbusServerRunning) opc.modbus.stopModbusServer()
                        else opc.modbus.startModbusServer(modbusBindField.text.trim(), parseInt(modbusPortField.text), modbusUnitIdField.value)
                    }
                }
                Text {
                    text: opc.modbus.modbusServerRunning ? "● Calisiyor" : "○ Durduruldu"
                    color: opc.modbus.modbusServerRunning ? "#2e7d32" : "#999999"
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }

    Dialog {
        id: renameDialog
        property int rowIndex: -1
        title: "Gorunen adi degistir"
        modal: true
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: opc.renameNode(rowIndex, renameField.text)
        ColumnLayout {
            TextField {
                id: renameField
                implicitWidth: 340
                selectByMouse: true
                onAccepted: renameDialog.accept()
            }
        }
    }

    Dialog {
        id: converterDialog
        property int rowIndex: -1
        title: "Donusturucu eslemesi"
        modal: true
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: {
            var warning = opc.modbus.modbusOversizeWarning(rowIndex, formatCombo.currentIndex)
            if (warning.length > 0) {
                oversizeWarningDialog.pendingWarning = warning
                oversizeWarningDialog.open()
            } else {
                opc.modbus.setModbusMapping(rowIndex, parseInt(addressSpin.text), formatCombo.currentIndex,
                                      typeCombo.currentIndex, countSpin.value)
            }
        }

        function loadCurrent() {
            var m = opc.modbus.modbusMappingFor(rowIndex)
            if (m.hasMapping) {
                addressSpin.text = String(m.registerAddress)
                formatCombo.currentIndex = m.formatIndex
                countSpin.value = m.registerCount
            } else {
                addressSpin.text = "1"
                formatCombo.currentIndex = 0
                countSpin.value = 2
            }
            applyFixedCountFor(formatCombo.currentIndex)
        }

        function applyFixedCountFor(formatIndex) {
            var fixed = opc.modbus.modbusFixedRegisterCount(formatIndex)
            if (fixed > 0) {
                countSpin.value = fixed
                countSpin.enabled = false
            } else {
                countSpin.enabled = true
            }
        }

        ColumnLayout {
            implicitWidth: 360
            spacing: 4

            Text { text: "Tur:" }
            ComboBox {
                id: converterTypeCombo
                Layout.fillWidth: true
                model: opc.converterNames
                currentIndex: 0
            }

            Text { text: "Register adresi:" }
            TextField {
                id: addressSpin
                Layout.fillWidth: true
                text: "1"
                validator: IntValidator { bottom: 0; top: 65535 }
            }

            Text { text: "Format:" }
            ComboBox {
                id: formatCombo
                Layout.fillWidth: true
                model: opc.modbus.modbusFormats
                textRole: "name"
                onCurrentIndexChanged: converterDialog.applyFixedCountFor(currentIndex)
            }

            Text { text: "Register tipi:" }
            ComboBox {
                id: typeCombo
                Layout.fillWidth: true
                model: opc.modbus.modbusRegisterTypes
                textRole: "name"
                currentIndex: opc.modbus.inputRegisterTypeIndex
            }

            Text { text: "Register sayisi (yalnizca metin formatlarinda kullanilir):" }
            SpinBox {
                id: countSpin
                Layout.fillWidth: true
                from: 1
                to: 64
                value: 2
            }
        }
    }

    Dialog {
        id: convertAllDialog
        title: "Tumunu donustur"
        modal: true
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: opc.modbus.convertAllWatchedTags(parseInt(allAddressField.text), allFormatCombo.currentIndex,
                                              allTypeCombo.currentIndex, allCountSpin.value)

        function loadDefaults() {
            allAddressField.text = "1"
            allFormatCombo.currentIndex = 0
            allCountSpin.value = 2
            applyFixedCountFor(allFormatCombo.currentIndex)
        }

        function applyFixedCountFor(formatIndex) {
            var fixed = opc.modbus.modbusFixedRegisterCount(formatIndex)
            if (fixed > 0) {
                allCountSpin.value = fixed
                allCountSpin.enabled = false
            } else {
                allCountSpin.enabled = true
            }
        }

        ColumnLayout {
            implicitWidth: 360
            spacing: 4

            Text { text: "Tum izlenen tag'ler bu ayarlarla, girilen adresten baslayarak bosluksuz siralanir."; wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#666666"; font.pixelSize: 11 }

            Text { text: "Baslangic adresi:" }
            TextField {
                id: allAddressField
                Layout.fillWidth: true
                text: "1"
                validator: IntValidator { bottom: 0; top: 65535 }
            }

            Text { text: "Format:" }
            ComboBox {
                id: allFormatCombo
                Layout.fillWidth: true
                model: opc.modbus.modbusFormats
                textRole: "name"
                onCurrentIndexChanged: convertAllDialog.applyFixedCountFor(currentIndex)
            }

            Text { text: "Register tipi:" }
            ComboBox {
                id: allTypeCombo
                Layout.fillWidth: true
                model: opc.modbus.modbusRegisterTypes
                textRole: "name"
                currentIndex: opc.modbus.inputRegisterTypeIndex
            }

            Text { text: "Register sayisi (yalnizca metin formatlarinda kullanilir):" }
            SpinBox {
                id: allCountSpin
                Layout.fillWidth: true
                from: 1
                to: 64
                value: 2
            }
        }
    }

    Dialog {
        id: oversizeWarningDialog
        property string pendingWarning: ""
        title: "Ekonomiklik uyarisi"
        modal: true
        standardButtons: Dialog.Cancel
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        ColumnLayout {
            implicitWidth: 320
            Text { text: oversizeWarningDialog.pendingWarning; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button {
                text: "Yine de kaydet"
                onClicked: {
                    opc.modbus.setModbusMapping(converterDialog.rowIndex, parseInt(addressSpin.text), formatCombo.currentIndex,
                                          typeCombo.currentIndex, countSpin.value)
                    oversizeWarningDialog.close()
                }
            }
        }
    }
}
