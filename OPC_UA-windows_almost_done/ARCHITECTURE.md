# JarvisIoT Gateway — Mimari ve Geliştirme Rehberi

Bu belge projenin mimarisini, katmanlarını, thread modelini ve **nasıl ilerleneceğini** anlatır.
Yeni özellik eklerken bu belgedeki katman ayrımına ve kurallara uyulmalıdır.

---

## 1. Amaç

Proje özünde bir **OPC UA → (Modbus TCP / MQTT / ...) gateway**'idir. Bir OPC UA sunucusundan
değer okur, bir ara katmanda dönüştürür ve bir veya birden fazla çıkışa yazar.

Temel ilke: **giriş ve çıkış değiştirilebilir olmalı.** Bugün çıkış Modbus, yarın MQTT olabilir;
girdi bugün OPC UA, yarın başka bir protokol olabilir. Bu, arayüzler (`IDataSource`, `IDataSink`)
ve bağımlılık enjeksiyonu (DI) ile sağlanır.

İkinci ilke: **çekirdek (core) arayüzden ve Qt'den bağımsızdır.** Qt yalnızca opsiyonel bir
görüntüleme katmanıdır; çekirdek onsuz da derlenir/çalışır.

---

## 2. Katmanlar ve dizin yapısı

```
src/
├── core/        SAF C++ — domain + orkestratör + arayüzler. Qt YOK, open62541 YOK.
│   ├── TagValue.h        Nötr değer tipi (UA_Variant'ın core karşılığı)
│   ├── Tag.h             Bir izlenen değişken (logicalName + nodeId + TagValue)
│   ├── ModbusMapping.h   Modbus'a özel eşleme metadata'sı (aktif kullanımda — bkz. `src/sink/`)
│   ├── DataStore.h/.cpp  Thread-safe "TagBus" (giriş yazar, çıkış/GUI okur)
│   ├── IDataSource.h     GİRİŞ arayüzü: run(running) + isConnected()
│   ├── IOpcUaSource.h    OPC'ye özel giriş arayüzü (IDataSource + browse/subscribe/config)
│   ├── IDataSink.h       ÇIKIŞ arayüzü: onTagUpdate()
│   ├── EndpointInfo.h    Nötr endpoint DTO
│   ├── BrowseNode.h      Nötr adres-uzayı düğüm DTO'su
│   ├── Logger.h/.cpp     Basit error/timing log yardımcıları + opsiyonel GUI sink'i (bkz. §12)
│   └── GatewayManager.h/.cpp   ORKESTRATÖR: pipeline + thread sahibi
│
├── source/      open62541'e BAĞIMLI giriş implementasyonu (IOpcUaSource).
│   ├── OpcUaDataSource.h/.cpp   Bağlantı yaşam döngüsü + worker loop (run)
│   ├── OpcUaSubscriber.h/.cpp   Subscription/monitored item + değer akışı + rotasyon (bkz. §9)
│   ├── OpcUaBrowser.h/.cpp      Adres uzayı gezme (browse)
│   ├── EndpointResolver.h/.cpp  Endpoint keşfi + IP yamalaması
│   └── UaUtils.h/.cpp           UA <-> nötr dönüşümler (variantToTagValue vb.)
│
├── sink/        ÇIKIŞ implementasyonu (IDataSink). Qt YOK, open62541 YOK — sadece libmodbus.
│   ├── ModbusConverter.h/.cpp     IConverter<vector<uint16_t>> — TagValue'yu Modbus register
│   │                              word'lerine paketler (word/byte-order varyantlı format seti)
│   ├── ModbusFormatNames.h/.cpp   Format/register-tipi enum <-> görüntü metni lookup tabloları
│   └── ModbusSink.h/.cpp          IDataSink somutu: libmodbus tabanlı TCP sunucu, kendi thread'i
│
├── qt/          Qt'YE BAĞIMLI adapter (opsiyonel). Sadece burada Qt var.
│   ├── OpcUaController.h/.cpp   Manager'ı GÖZLEMLEYEN QML köprüsü (OPC UA tarafı; Modbus'u ModbusController'a devreder)
│   ├── ModbusController.h/.cpp  OpcUaController'ın alt-nesnesi (`opc.modbus`): Modbus mapping/sunucu kontrolü (bkz. §7 Adım E)
│   ├── WatchRow.h               İzlenen bir satırın GUI-katmanı DTO'su (OPC-UA + Modbus alanları bilinçli iç içe)
│   ├── WatchedValuesModel.h/.cpp  QAbstractListModel projeksiyonu (OpcUaController'ın m_watched'ına referans)
│   ├── GuiApp.h/.cpp            QML bootstrap / mod seçimi
│   ├── GuiSelfTest.h/.cpp       --selftest başsız doğrulama harness'ı
│   └── qml/Main.qml             Arayüz
│
└── main.cpp     COMPOSITION ROOT (tek main): somut tipleri kurar, DI yapar, GUI'yi çalıştırır.
```

**Bağımlılık yönü (tek yönlü):** `qt` → `core` ← `source`, `core` ← `sink`. `core` kimseyi tanımaz.
`source` open62541'i bilir ama Qt'yi bilmez. `sink` libmodbus'u bilir ama Qt/open62541 bilmez.
`qt` core'u bilir ama open62541/libmodbus'u bilmez.

**Kütüphane sınırı (CMake):** `opccore` statik kütüphanesi = `core` + `source` + `sink`, **Qt LINK
ETMEZ** (`CMakeLists.txt`). `OPC` çalıştırılabiliri = `opccore` + Qt. Böylece core'un Qt bağımsızlığı
link zamanında zorlanır.

---

## 3. Bileşenler ve sorumlulukları

| Bileşen | Sorumluluk |
|---|---|
| `GatewayManager` | Pipeline'ın ve **thread'lerin** sahibi. `DataStore` + giriş + isimli converter/sink kayıtları tutar. Girişten gelen değeri tüm sink'lere (`onTagUpdate`) ve GUI gözlemcisine dağıtır (`onValue`). |
| `IDataSource` / `IOpcUaSource` | Giriş soyutlaması. Somut: `OpcUaDataSource`. |
| `IDataSink` | Çıkış soyutlaması (`onTagUpdate()` + opsiyonel `run()`). Somut: `ModbusSink` (tam implement). |
| `IConverter<Out>` | Templated dönüşüm soyutlaması (`convert(nodeId, TagValue) const -> Out`). Somut: `ModbusConverter` (`Out = vector<uint16_t>`). `GatewayManager::onValue` bunu ÇAĞIRMAZ — dönüşüm sink'in kendi içinde (bkz. `ModbusSink`) yapılır, bkz. §7. |
| `OpcUaDataSource` | OPC bağlantı yaşam döngüsü + worker loop (`run`). Browse/subscribe işini `OpcUaBrowser`/`OpcUaSubscriber`'a delege eder. |
| `OpcUaSubscriber` | Subscription/monitored-item yönetimi + ~72 değer bütçesi için pencereli rotasyon (bkz. §9). |
| `ModbusConverter` / `ModbusSink` | `IConverter`/`IDataSink` somutları: `TagValue`'yu Modbus register word'lerine paketler, `libmodbus` ile kendi thread'inde TCP sunucusu çalıştırır. |
| `DataStore` | Thread-safe tag deposu (TagBus). Giriş yazar; çıkış/GUI okur. |
| `TagValue` | Kütüphaneden bağımsız nötr değer (`std::variant` + quality + timestamp). |
| `OpcUaController` | Qt/QML adapter. Manager'ı gözlemler, worker thread'inden gelen olayları GUI thread'ine marshal eder. Pipeline'ın SAHİBİ DEĞİLDİR. Modbus'a özgü kontrol mantığını `ModbusController`'a devreder. |
| `ModbusController` | Qt/QML adapter, `OpcUaController`'ın alt-nesnesi (`Q_PROPERTY(QObject* modbus)` ile `opc.modbus` olarak QML'e expose edilir, aynı GUI thread'inde). Modbus mapping CRUD, oversize-uyarısı, sunucu start/stop, mapping-önizleme metni üretir. `ModbusConverter`/`ModbusSink`'i `GatewayManager` üzerinden isimle LOOKUP eder, SAHİBİ DEĞİLDİR (bkz. §7 Adım E). |
| `main.cpp` | Composition root: somut `OpcUaDataSource`/`ModbusConverter`/`ModbusSink`'i kurup manager'a enjekte eder, controller'ı bağlar, GUI'yi çalıştırır. |

---

## 4. Thread modeli

Şu an **en az iki, GUI'den Modbus sunucusu başlatılınca üç (veya daha fazla)** thread vardır:

1. **Ana (GUI) thread** — program açılışında başlar; `QGuiApplication::exec()` (Qt event loop + QML).
2. **OPC worker thread** — `GatewayManager` oluşturur; `OpcUaDataSource::run()` burada döner.
3. **Sink thread'leri (dinamik, isimli)** — kullanıcı GUI'den "Modbus sunucusunu başlat" dediğinde
   (`OpcUaController::startModbusServer` → `GatewayManager::startSink("Modbus")`) manager, o sink'e
   özel bir `SinkRuntime{running, thread}` açar ve `ModbusSink::run()`'ı orada çalıştırır;
   "durdur" ile join edilip kapanır. Birden fazla isimli sink eklenirse her biri kendi thread'ini alır.

### Thread sahipliği: MERKEZİ

Alt-sistemler kendi `std::thread`'ini YARATMAZ. Thread'lerin sahibi `GatewayManager`'dır — kaynak
thread'i `m_threads` (`std::vector<std::thread>` + tek `std::atomic<bool> m_running`) içinde, her
sink'in thread'i ise kendi `SinkRuntime` (`m_sinkRuntimes`, isim → ayrı `atomic<bool> running` +
`thread`) içinde tutulur. Alt-sistem yalnızca "çalıştırılacak iş"i (`run(const std::atomic<bool>&
running)`) sunar; manager thread'i oluşturup işi ona verir (`GatewayManager.cpp`).

```cpp
bool GatewayManager::start() {
    if (m_running.exchange(true)) return true;
    if (m_source) {
        IOpcUaSource* src = m_source.get();
        m_threads.emplace_back([this, src] { src->run(m_running); });
    }
    return true;
}
void GatewayManager::stop() {
    m_running = false;
    for (auto& t : m_threads) if (t.joinable()) t.join();
    m_threads.clear();
}

bool GatewayManager::startSink(const std::string& name) {
    auto& runtime = m_sinkRuntimes[name];
    runtime = std::make_unique<SinkRuntime>();
    runtime->running = true;
    IDataSink* sinkPtr = m_sinks[name].get();
    std::atomic<bool>* flag = &runtime->running;
    runtime->thread = std::thread([sinkPtr, flag] { sinkPtr->run(*flag); });
    return true;
}
```

### Veri akışı

```
     ANA (GUI) THREAD                              OPC WORKER THREAD
 ┌──────────────────────────┐              ┌──────────────────────────────┐
 │ QML ↔ OpcUaController     │              │ OpcUaDataSource::run()        │
 │                          │  KOMUT       │  connectAsync + run_iterate   │
 │ Baglan / subscribe ──────┼──(kuyruk)───▶│  reconnect / subscription     │
 │                          │              │  OpcUaSubscriber (UA çağrıları)│
 │ applyPhase / onValue ◀───┼──VERİ────────┤  faz callback / değer callback│
 │  (QueuedConnection)      │              └──────────────┬───────────────┘
 └──────────────────────────┘                             │ yazar (TagValue)
        ▲                                    ┌────────────▼───────────────┐
        │ startModbusServer/                 │ DataStore (mutex) = TagBus  │
        │ stopModbusServer                   └─────────────────────────────┘
        │                                                  │ GatewayManager::onValue
        │                                                  │ TÜM sink'lere onTagUpdate() dağıtır
 ┌──────┴────────────────────────────────────┐             ▼ (bloklamaz — kuyruğa atar)
 │  MODBUS SINK THREAD (SinkRuntime, "Modbus")│◀────────────┘
 │  ModbusSink::run(): drainQueue() + Modbus  │
 │  TCP sunucusu (libmodbus, select loop)     │
 └─────────────────────────────────────────────┘
```

**GUI → worker (komut):** `subscribe`/`unsubscribe`/`browse` doğrudan UA çağırmaz; mutex'li
kuyruklara iş bırakır, worker `run()` içinde boşaltır. Sebep: **open62541 thread-safe değildir**,
tüm `UA_*` çağrıları yalnız worker thread'inde olur.

**worker → GUI (olay/veri):** faz ve değer callback'leri worker thread'inde tetiklenir →
`GatewayManager` gözlemcisi → `OpcUaController` → `QMetaObject::invokeMethod(..., QueuedConnection)`
ile GUI thread'ine marshal. QML'e asla worker thread'inden dokunulmaz.

**Durdurma:** `stop()` → `m_running=false` → `run()` döngüsü çıkar → `cleanupClient()` worker
thread'inde çalışır → manager join eder. `UA_Client`'a ömrü boyunca **tek thread** dokunur.

---

## 5. SOLID / arayüz ilkeleri

- **Bağımlılığın ters çevrilmesi (DIP):** `GatewayManager` ve `OpcUaController` somut sınıflara
  değil arayüzlere (`IOpcUaSource`) bağlıdır. Somut tipler yalnızca `main.cpp`'de bilinir.
- **Açık/kapalı (OCP):** yeni çıkış = yeni `IDataSink` implementasyonu; mevcut kod değişmez.
- **Tek sorumluluk (SRP):** browse/subscribe/dönüşüm ayrı sınıflarda; `Tag` yalnız kimlik+değer,
  Modbus eşlemesi `ModbusMapping`'de.
- **Interface'i abartma:** metotları ihtiyaç doğunca ekle. `IDataSink`'te `onTagUpdate()` zorunlu,
  `run()` ise varsayılan no-op gövdeli (`{ (void)running; }`) — kendi thread'ini gerektirmeyen basit
  bir sink `run()`'ı hiç override etmek zorunda değil; `ModbusSink` kendi thread'ini gerektirdiği
  için override eder (§4).

---

## 6. Derleme

```
cmake -S . -B build
cmake --build build
```

Üretilenler: `opccore` (Qt'siz statik kütüphane) + `OPC` (GUI executable). Bağımlılıklar:
open62541 v1.3.11 (FetchContent, ilk configure'da internet gerekir), OpenSSL, **libmodbus**
(pkg-config üzerinden zorunlu — `find_package(PkgConfig) + pkg_check_modules(MODBUS REQUIRED
IMPORTED_TARGET libmodbus)`, `CMakeLists.txt:11`; macOS'ta `brew install libmodbus`), Qt6
(Core+Quick). Sertifikalar `certs/` altında (`certs/generate_certs.sh`). Loglama dosyaya
yazmaz (bkz. §12) — sadece konsol + GUI paneli.

**Çalıştırma:**
- GUI: `OPC` → URL gir → Ara → endpoint seç → Baglan → ağaçta yaprağa tıkla (abone ol).
- Başsız doğrulama: `OPC --selftest <url> <index> <saniye> [kullanici] [sifre]`.

**Bağımsızlık kontrolü:** `src/core` içinde open62541/Qt YOK; `src/qt` içinde open62541 YOK.
```
g++ -std=c++17 -Isrc -fsyntax-only src/core/DataStore.cpp src/core/GatewayManager.cpp src/core/Logger.cpp
```
→ open62541/Qt olmadan derlenir (core'un bağımsızlığının kanıtı; 2026-07-31'de tekrar doğrulandı,
hatasız geçiyor).

---

## 7. Nasıl ilerlenecek (yol haritası)

### Adım A — Convert (veri dönüşüm) katmanı — ✅ TAMAMLANDI
Gerçekleşen tasarım, ilk taslaktan farklı: `src/core/IConverter.h` **templated** ve `const`:
```cpp
template <typename Out>
class IConverter {
public:
    virtual ~IConverter() = default;
    virtual Out convert(const std::string& nodeId, const TagValue& in) const = 0;
};
```
Somut: `src/sink/ModbusConverter.h/.cpp` (`Out = std::vector<std::uint16_t>`). DI:
`main.cpp`'de `manager.setConverter<std::vector<std::uint16_t>>("Modbus", ...)`, `GatewayManager`
isimli bir `unordered_map<std::string, unique_ptr<IConverterSlot>>` üzerinden tutar
(`ConverterSlot<Out>` + `dynamic_cast` ile type-erase).

**Önemli fark:** `GatewayManager::onValue` converter'ı ÇAĞIRMAZ — ham `TagValue`'yu doğrudan tüm
sink'lerin `onTagUpdate`'ine dağıtır (`GatewayManager.cpp`). Dönüşüm bunun yerine **sink'in kendi
içinde** yapılır: `ModbusSink`, kurucusunda aldığı `ModbusConverter*` işaretçisini kendi
`run()`/`drainQueue()` akışında çağırır. Bu, ilk taslaktaki "manager dönüştürür" fikrinden
kasıtlı bir sapma — her sink kendi dönüşüm zamanlamasını (senkron/tamponlu) kendi seçebiliyor.

### Adım B — Modbus TCP çıkışı (`IDataSink`) — ✅ TAMAMLANDI
Gerçek yol `src/modbus/` değil **`src/sink/`** oldu (proje ilerledikçe yeniden adlandırıldı):
`src/sink/ModbusSink.h/.cpp` + `src/sink/ModbusFormatNames.h/.cpp`. `IDataSink::run()` eklendi,
`GatewayManager` her isimli sink için `startSink(name)`/`stopSink(name)` ile ayrı bir `SinkRuntime`
thread'i açıp kapatıyor (§4). `onTagUpdate` gerçekten bloklamıyor — sadece mutex'li `m_queue`'ya
`QueueItem` atıyor (`ModbusSink.cpp`), asıl `libmodbus` TCP I/O'su `run()` içinde. Register/format
eşlemesi `ModbusMapping` + `ModbusConverter` ile çalışıyor. CMake: `libmodbus` pkg-config ile
`opccore`'a PRIVATE linklendi (`CMakeLists.txt:11,42`). DI: `main.cpp`'de
`manager.addSink("Modbus", std::make_unique<ModbusSink>(&manager.store(), modbusConverterPtr))`.

### Adım C — MQTT çıkışı — henüz yapılmadı
Adım B ile birebir aynı desen uygulanabilir: `MqttSink : IDataSink`, kendi thread'i, `onTagUpdate`
kuyruğa koyar. Aynı anda hem Modbus hem MQTT sink eklenebilir (manager hepsine dağıtır,
`m_sinks`/`m_sinkRuntimes` zaten isimli/çoklu sink'i destekliyor).

### Adım D — DataStore'u okuyan çıkış (opsiyonel) — henüz yapılmadı
Push yerine periyodik okuma isteyen bir çıkış `DataStore::snapshot()` ile tüm tabloyu tutarlı okur.

### Adım E — Modbus kontrol mantığının ayrıştırılması (OpcUaController → ModbusController) — ✅ TAMAMLANDI
`OpcUaController`'ın belgelenmiş rolü (§3: "Pipeline'ın SAHİBİ DEĞİLDİR") ile içindeki ~170 satırlık
Modbus mapping/doğrulama/sunucu-kontrol mantığı arasındaki tutarsızlık giderildi. `src/qt/
ModbusController.h/.cpp` eklendi: tüm Modbus'a özgü Q_PROPERTY/Q_INVOKABLE/state (mapping CRUD,
format/register-type katalogları, oversize-uyarısı, sunucu start/stop, `refreshModbusPreview`,
unsubscribe-sırasında-mapping-temizleme) buraya taşındı. `OpcUaController`, `WatchedValuesModel` ile
zaten var olan "referans-tutan alt-QObject" desenini tekrar kullanarak `ModbusController`'ı kendi
kurucusunda inşa eder (`m_watched`/`m_watchedIndex`'e referansla, `m_valuesModel`'e pointer'la erişir)
ve `Q_PROPERTY(QObject* modbus READ modbusController CONSTANT)` ile QML'e expose eder — QML çağrıları
`opc.startModbusServer(...)` → `opc.modbus.startModbusServer(...)` şekline taşındı (`Main.qml`).
`main.cpp` DEĞİŞMEDİ: `ModbusConverter`/`ModbusSink` somutları hâlâ yalnız composition root'ta kurulup
`GatewayManager`'a isimle kaydediliyor; `ModbusController` bunları (eskiden `OpcUaController`'ın
yaptığı gibi) isimle LOOKUP ediyor, sahiplenmiyor. **Not:** §8 kural 6 burada ihlal edilmiyor — o
kural değiştirilebilir strateji tipleri (`IOpcUaSource`/`IDataSink`/`IConverter` somutları) için
geçerli; `ModbusController`, `WatchedValuesModel` gibi salt `qt/` katmanı içi bir yardımcı nesnedir,
takas edilebilir bir arayüzü yoktur.

---

## 8. Değişmez kurallar (her PR'da uyulacak)

1. `src/core` içine open62541 veya Qt tipi/başlığı **sokma**. UA↔nötr dönüşümü `src/source` (UaUtils).
2. Tüm `UA_*` çağrıları yalnız **OPC worker thread'inde**. GUI/başka thread'den gelen istek kuyruğa konur.
3. Worker thread'inden GUI'ye dokunma; `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` ile marshal et.
4. Thread'i alt-sistem **yaratmaz**; `run()` sunar, thread'i `GatewayManager` verir.
5. `onTagUpdate` bloklamaz (kuyruğa koy, ağır işi kendi thread'inde yap).
6. Somut tipler yalnız `main.cpp`'de kurulur (DI); üst katmanlar arayüzlere bağlanır.
7. Interface'i şişirme: metodu ihtiyaç doğunca ekle.
8. Kod yorumsuz tutulur; açıklama bu belgede.

---

## 9. Bilinen sunucu kısıtı: PowerStudio ~72 paylaşımlı değer bütçesi

Gerçek Circutor PowerStudio testleriyle kanıtlandı: sunucu, kendisine bağlı **tüm client'lar
arasında paylaşımlı**, toplam ~72 monitored-item DEĞER bütçesi uyguluyor — session-başına değil,
cihaz/sunucu genelinde (iki ayrı fiziksel makineden, iki ayrı session'dan, iki farklı cihaza
~60'ar tag abone olunca ikisi birden başarısız oldu). Bütçe aşılınca en eski abone olunan item'lar
`BadNoCommunication`'a düşüyor; en yeni abone olunanlar sağlam kalıyor. Bu, istemci tarafında
kaç session/subscription açılırsa açılsın aşılamayan bir sınır.

`OpcUaSubscriber` bu yüzden **rotasyonlu abonelik** kullanır: toplam istenen tag sayısı
`kWindowSize`'ı (varsayılan 60, `OpcUaSubscriber.h`) aşarsa, tag listesi `buildWindows()` ile TEK
SEFERDE, sabit boyutlu, birbirini örtmeyen, artan ID'li pencerelere bölünür (`OpcUaSubscriber::
Window { id; nodeIds }`, `m_windows`) — örn. 232 tag/60'lık pencere → ID 0,1,2 (60'ar tag) ve
ID 3 (52 tag). Aynı anda sadece BİR pencere gerçekten abone edilir (gerçek push); `m_activeWindowIndex`
hangi pencerenin canlı olduğunu tutar.

**Rotasyon tetikleyicisi (§9 önceki sürümde yanlış anlatılıyordu — kod ikisinden ERKEN olanı
kullanıyor, sadece sabit 5s değil):** `tick()` (`OpcUaSubscriber.cpp:226-243`, `OpcUaDataSource::
run()` döngüsünün her turunda çağrılır) iki koşuldan **birini** yeterli sayar:
- `elapsed >= kDwellMs` (tavan, varsayılan 5000ms) **VEYA**
- `m_pendingConfirmation.empty()` — aktif penceredeki TÜM tag'ler en az bir kez GOOD örnek verdi
  (her `dataChangeHandler` GOOD çağrısı kendi nodeId'sini bu set'ten siler, `OpcUaSubscriber.cpp:53`).

Yani bir pencere verisi hızlı gelirse `kDwellMs` dolmadan erken döner; pencerede tek bir tag bile
hiç GOOD olamıyorsa (kalıcı arızalı node vb.) o pencere HER ZAMAN tam `kDwellMs` bekler — ve bu,
o sırada diğer TÜM pencerelerin döngü hızını da yavaşlatır (dokümante edilmemiş ama gerçek bir
davranışsal sonuç). Tetiklenince `m_activeWindowIndex` bir sonraki pencereye ilerletilir ve o
pencerenin TAMAMI tek seferde, **senkron** `UA_Client_MonitoredItems_createDataChanges`/`_delete`
(zaten mevcut, değişmeyen `createMonitoredItems`/`deleteMonitoredItems`) ile devreye alınır
(`OpcUaSubscriber::rotateToWindow`). `rotateToWindow`, eski/yeni pencereyi ID bazlı küme farkıyla
karşılaştırır (kısmi create/delete hatalarına karşı dayanıklı olmak için) ama pencereler artık
sabit/örtüşmeyen olduğundan bu pratikte her rotasyonda tam takas anlamına gelir. Her monitored
item'ın hangi pencereye ait olduğu da ayrıca tutulur (`m_monIdToWindowId`) — loglarda `windowId=`
ile görünür. Toplam tag sayısı `kWindowSize`'ın altındaysa (tek pencere) rotasyon hiç devreye
girmez, tüm tag'ler her zaman canlıdır (ek yük yok). Böylece kendi payımız her zaman güvenli
sınırın altında kalır ve diğer client'lara (meslektaş, üçüncü taraf scriptler, SCADA) da bütçeden
pay bırakılmış olur.

**Not:** Daha önce, worker thread'i bloklamamak için `kBatchSize=5`/`kStepMs=150ms` ile hızlı/async
bir rotasyon (`_async` create/delete çağrıları) denendi; gerçek 232 tag'lik cihazda tamamen
BAŞARISIZ oldu çünkü bir tag'in pencerede kalma süresi (`(kWindowSize/kBatchSize)×kStepMs=900ms`)
sunucunun bir tag'e ilk örneği verme süresinden kısaydı — tag hiç GOOD olamadan sürekli rotasyondan
çıkıp giriyordu (`BadWaitingForInitialData` sonsuz döngüsü). Bu yüzden senkron/tam-pencere/5000ms
tasarımına dönüldü; gelecekte aynı hataya düşülmemesi için dwell süresi asla sunucunun gerçek
ilk-örnek gecikmesinden kısa tutulmamalı.

---

## 10. Bilinen eksikler (bilinçli olarak dokunulmadı)

Aşağıdakiler 2026-07-30 tarihli bir kod taramasında bulundu. Kullanıcı kararıyla düzeltilmediler —
küçük/izole değişiklikler her biri ayrı ayrı, ayrı onayla ele alınacak. Burada sadece kayıt altına
alınıyorlar:

1. **`chunkSizeCallback`/`serverCapacityText` hiçbir zaman tetiklenmiyor.** Zincir uçtan uca bağlı:
   `IOpcUaSource::setChunkSizeCallback` → `OpcUaDataSource::setChunkSizeCallback` →
   `OpcUaSubscriber::setChunkSizeCallback` (`m_chunkSizeCallback` alanına kaydedilir,
   `OpcUaSubscriber.h:23-24,59`) → `GatewayManager::setChunkSizeObserver`
   (`GatewayManager.cpp:18-20`) → `OpcUaController` (`OpcUaController.cpp:76-87`,
   `m_serverCapacityText`'i doldurur) → `Main.qml`'de gösterilir. Ama `OpcUaSubscriber.cpp`
   içinde `m_chunkSizeCallback` **hiçbir yerden çağrılmıyor** — muhtemelen sunucunun bildirdiği
   azami monitored-item sayısını (§9'daki ~72 bütçesiyle ilgili) raporlamak için düşünülmüş ama
   üretici (producer) tarafı hiç yazılmamış. Sonuç: GUI'deki "sunucu kapasitesi" alanı her zaman
   boş kalır; zararsız ama yarım kalmış bir özellik.
2. **`OpcUaSubscriber.cpp:174`** — `//req.maxNotificationsPerPublish = 5;` — devre dışı bırakılmış,
   ölü bir ayar satırı. Kaldırılmadı, açıklanmadı.
3. **`src/.DS_Store`** — macOS Finder'ın bıraktığı, kaynağa ait olmayan bir dosya; `.gitignore`'a
   eklenmedi.

## 11. Performans ve kaynak bütçesi

Bu bölümdeki rakamlar **analitik/teorik tahminlerdir** — sabitlerden, veri yapısı boyutlarından ve
kod yolunun karmaşıklığından türetilmiştir. Gerçek Circutor PowerStudio'ya karşı **ölçülmüş**
rakamlar değildir (bu ortamdan gerçek cihaza erişilemiyor); en alttaki tabloyu kullanıcı kendi
sahra testinde dolduracak.

### Rotasyon döngü süresi (formül)

`N` = toplam abone olunan tag sayısı, `kWindowSize = 60`, `kDwellMs = 5000`ms
(`OpcUaSubscriber.h:73-74`).

- Pencere sayısı: `pencere(N) = ⌈N / 60⌉`
- Bir tam döngünün (her tag'in tekrar canlı olmasının) **en kötü durum** süresi:
  `⌈N/60⌉ × 5000ms` — yalnızca hiçbir pencere erken rotasyon yapamazsa (bkz. §9, bir pencerede
  kalıcı GOOD-olamayan tag varsa) gerçekleşir.
- Erken rotasyon (`m_pendingConfirmation.empty()`) devreye girerse gerçek döngü süresi bundan
  **kısa** olur — alt sınır, pencerenin tüm tag'lerinin ilk örneği alma süreleri toplamına (paralel
  değil, tek `createDataChanges` çağrısı ile toplu istendiği için sunucunun bunları işleme hızına)
  bağlıdır ve kod tarafından ölçülmez, sadece loglanır (aşağıya bakın).

| N (tag) | Pencere sayısı | En kötü tam döngü |
|---|---|---|
| ≤60 | 1 (rotasyon yok) | 0 (hep canlı) |
| 72 | 2 | ~10 s |
| 120 | 2 | ~10 s |
| 180 | 3 | ~15 s |
| 232 | 4 | ~20 s |

### Tek rotasyonun (create+delete) maliyeti

`rotateToWindow` (`OpcUaSubscriber.cpp:245-274`) her tetiklendiğinde **senkron** iki UA servis
çağrısı yapar (`UA_Client_MonitoredItems_delete`, ardından `_createDataChanges`) ve bu süre boyunca
OPC worker thread'i (dolayısıyla `tick()`'in kendisi ve diğer tüm işler) bloke olur. Gerçek süre
ağ RTT'sine ve sunucunun batch işleme hızına bağlı olduğundan burada tahmini bir ms rakamı
**verilmiyor** — kod zaten bunu kendisi ölçüp logluyor:
```
[SUB][TIMING] pencere rotasyonu: windowId=<id> leaving=<n> entering=<n> deleteMs=<x> createMs=<y> toplamMs=<z>
```
(`OpcUaSubscriber.cpp:270-273`). İlk toplu abonelikte de benzer bir log var
(`[SUB][TIMING] toplu abone ...`, `OpcUaSubscriber.cpp:122-126`). Gerçek cihazda bu satırları
toplamak, "tek rotasyon kaç ms sürüyor" sorusunun doğrudan cevabıdır.

### Algoritmik karmaşıklık notu

`deleteMonitoredItems` (`OpcUaSubscriber.cpp:331-362`) her silinecek `nodeId` için
`m_monIdToNodeId` üzerinde doğrusal tarama yapıyor: O(silinecek × canlı). `kWindowSize=60`'ta bu en
kötü 60×60=3600 karşılaştırma — önemsiz. `kWindowSize` ileride büyütülürse bu yaklaşım
yeniden değerlendirilmeli (`std::unordered_map<string,uint32_t>` ters-indeks eklenebilir).

### RAM (analitik tahmin)

Kesin bir MB rakamı iddia edilmiyor; hangi bileşenin baskın olduğu önemli:

- **Tag-başı veri yapıları** (`DataStore`'daki `Tag`/`TagValue`, `WatchedValuesModel`'deki
  `WatchRow`, `OpcUaSubscriber`'ın `m_monIdToNodeId`/`m_monIdToWindowId` — bu ikisi zaten sadece
  canlı pencere kadar, yani ≤60 girdi tutar): her tag birkaç yüz bayt (iki `std::string` + bir
  `std::variant` + map/hash düğüm overhead'i). Yüzlerce tag'te bile toplam düşük yüzlerce KB
  mertebesinde — **önemsiz**.
- **`ModbusSink`'in register + bit tabloları** (`modbus_mapping_new_start_address`, dört tablonun
  tümü tam 0–65535 adres aralığı: 2×65536 word register + 2×65536 bit coil/discrete-input) — tag
  sayısından bağımsız, sabit boyutlu, yüzlerce KB mertebesinde.
- **Baskın bileşen: Qt6/QML runtime + open62541 istemci + OpenSSL kütüphaneleri.** Bir Qt Quick
  uygulamasının taban bellek ayak izi (QML motoru, sahne grafiği, dinamik kütüphaneler) tag/register
  veri yapılarını kıyaslanamayacak ölçüde geçer. Gerçek rakam platforma/Qt sürümüne göre değişir —
  burada tahmin edilmiyor, aşağıdaki tabloyla ölçülmesi öneriliyor.

### Gerçek cihazda ölçüm (kullanıcı dolduracak)

Loglama dosyaya yazmıyor (bkz. §12) — çıktıyı kalıcı hale getirmek için elle yönlendirme gerekir.
Circutor'a bağlıyken ölçüm için:
```sh
python3 tools/monitor_resources.py --name OPC --log ölçüm.csv
./build/OPC 2>&1 | tee opc-oturum.log
grep '\[SUB\]\[TIMING\]' opc-oturum.log
```
Ayrıca §12 sayesinde `[SUB][TIMING]` gibi rotasyon süre logları ve tüm hata satırları artık
GUI'nin log paneline de düşüyor — canlı bir oturumu izlerken dosyaya bakmaya gerek kalmadan
ekrandan da takip edilebilir.

| Senaryo | RAM (RSS) | Not |
|---|---|---|
| Boşta (bağlı değil) | _ölçülmedi_ | |
| Bağlı, N tag abone (N=___) | _ölçülmedi_ | |

| Ölçüm | Değer | Kaynak |
|---|---|---|
| Tipik rotasyon deleteMs+createMs | _ölçülmedi_ | `[SUB][TIMING] pencere rotasyonu` logu |
| Gözlenen tam döngü süresi (N=___) | _ölçülmedi_ | ardışık `windowId=0` loglarının zaman farkı |

---

## 12. Loglama: `LOG_ERROR()`/`LOG_TIMING()`

Proje genelinde diagnostik loglar hâlâ çoğunlukla ham `std::cout`/`std::cerr` — her dosya kendi
`[TAG]` önekini (`[SUB]`, `[OPC UA][TIMING]`, `[IP]`, `[BROWSE]`, `[-]`, `[STATE]`, `[SELFTEST]`)
literal metin olarak taşır, hiç seviye/kategori sınıflandırması yok. Bu **bilinçli bir tercih**:
2026-07-31'de önce seviyeli/kategorili/sink-mimarili (glog/spdlog benzeri) kapsamlı bir `Logger`
sınıfı + dosyaya kalıcı yazma (`logs/*.log`) denendi, ama proje **ileride bağımsız bir uygulamaya
dönüştürülmesi** düşünülürken bu karmaşıklık (kaynak-ağacına bağlı dosya yolu, sınıf hiyerarşisi)
gereksiz görüldü ve geri alındı. Kalan tek ihtiyaç: (a) hata satırlarının ve (b) süre-ölçüm
(`[TIMING]`) satırlarının **GUI'nin log paneline de** düşmesiydi (öncesinde sadece konsoldaydı,
GUI paneli yalnızca kullanıcı işlemlerini — `appendLog()` — gösteriyordu).

### `src/core/Logger.h/.cpp` — çok küçük, sınıf hiyerarşisi yok

```cpp
namespace logging {
using Sink = std::function<void(bool isError, const std::string& line)>;
void setSink(Sink sink);   // TEK, global, opsiyonel callback — sink LİSTESİ/ILogSink YOK

class LogStream {           // RAII stream-proxy
public:
    explicit LogStream(bool isError);
    ~LogStream();            // yıkılırken: duvar-saati damgası ekler, cout/cerr'e basar, sink'i çağırır
    template <typename T> LogStream& operator<<(const T& v) { ...; return *this; }
};
}
#define LOG_ERROR()  ::logging::LogStream(true)
#define LOG_TIMING() ::logging::LogStream(false)
```

Çağrı yeri, eski `[TAG]` metnini AYNEN korur — sadece `std::cerr`/`[TIMING]`-etiketli `std::cout`
çağrıları bu makrolara geçer, geri kalan HER ŞEY (Info seviyeli, orijinalde `std::cout` olan
satırlar — "abonelik kuruldu", "izlemeye alindi", `[STATE] ...` vb.) düz `std::cout` olarak kalır:
```cpp
std::cout << "[SUB] abonelik kuruldu (id=" << m_subscriptionId << ")..." << std::endl;   // değişmedi
LOG_ERROR() << "[SUB] abonelik kurulamadi: " << UA_StatusCode_name(...);                  // yeni
LOG_TIMING() << "[SUB][TIMING] pencere rotasyonu: windowId=" << id << " toplamMs=" << ms; // yeni
```
`LogStream`'de `std::endl`/manipülatör overload'u yok (yarım kalmış bir satır derleme hatası verir).
`ua::nowMs()`'e dokunulmadı — süre ölçümü mesaj gövdesinde aynen kullanılmaya devam ediyor;
`LOG_ERROR()`/`LOG_TIMING()` sadece satır başına AYRICA gerçek duvar-saati (`HH:MM:SS.mmm`) ekliyor.

### Sink: sadece konsol + GUI paneli, dosya YOK

`LogStream::~LogStream()` koşulsuz `std::cerr`(error)/`std::cout`(timing)'e basar, ardından
`setSink`'le kayıtlıysa (varsa) tek callback'i çağırır — **dosyaya hiç yazılmaz**, `logs/` dizini
yok, `OPC_LOG_DIR` yok. `OpcUaController` kurucusunda `logging::setSink([this](...){ ... })` ile
kendi panel-güncelleme lambda'sını kaydeder (yıkıcıda `setSink(nullptr)` — Logger'ın global/statik
durumu process ömürlü olduğu için ZORUNLU, yoksa dangling `this`). Bu lambda **her zaman**
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` ile marshal eder — `LOG_ERROR()`/
`LOG_TIMING()` çoğunlukla OPC worker thread'inden çağrılır (`OpcUaSubscriber`/`OpcUaDataSource`
içinde), `appendLog()`'un aksine GUI thread'inde değildir, §8 kural 3 gereği doğrudan `m_log`'a
dokunulamaz.

**Panelde artık hem hata hem timing satırları görünür** (`m_log`, QML'deki kayan `TextArea`) —
`appendLog()`'tan gelen kullanıcı-işlemi mesajlarına ek olarak. `appendLog(QString)`'in kendisi
değişmedi: hâlâ doğrudan `std::cout` + `m_log`'a ekleme + 8000 karakter kırpma yapıyor, Logger'a
hiç uğramıyor — iki mekanizma (kullanıcı-mesajları vs. hata/timing) bağımsız, panelde aynı
`QString m_log`'u paylaşıyorlar.

---

## 13. Bilinen Modbus eksikleri (bilinçli olarak dokunulmadı)

Aşağıdakiler 2026-07-31 tarihli, Modbus tarafına odaklı bir kod taramasında bulundu (§10 ile aynı
usul). Modbus, projede OPC UA'nın simetrik bir eşi DEĞİL: OPC UA tek **kaynak** (`IOpcUaSource` →
`OpcUaDataSource`), Modbus ise yalnızca tek yönlü bir **çıkış**tır (`IDataSink` → `ModbusSink`):
önceden alınmış OPC UA tag değerlerini dışarıdaki bir Modbus master'ın (SCADA/PLC) sorgulaması için
bir Modbus TCP **sunucusu** (slave) olarak yeniden yayınlar. Modbus tarafı hiçbir zaman veri
*okumaz*. "Modbus zayıf kaldı" hissinin yapısal kök nedeni budur — eksik bir ikiz değil, dar kapsamlı
bir bileşendir.

Kullanıcı kararıyla henüz düzeltilmediler — küçük/izole değişiklikler her biri ayrı ayrı, ayrı
onayla ele alınacak. Liste kasıtlı olarak öncelik sırasında: 1 en somut/izole hata, 10 en büyük
mimari karar. İleride bir madde ele alınırken doğrulama **gerçek bir Modbus master'a bağlanarak**
yapılacak; `tools/modbus_mock_master.py` yalnızca geliştirme aşaması yardımcısıdır, nihai doğrulama
değil.

1. **✅ ÇÖZÜLDÜ (2026-07-31) — Coil register tipi artık çalışıyor.** Eskiden `ModbusSink.cpp`'teki
   `writeWords`/`clearWords` `RegisterType::Coil` için sessizce erken `return` ediyordu ve
   `ModbusSink::run()`'daki `modbus_mapping_new_start_address(0, 0, 0, 0, 0, 65536, 0, 65536)` çağrısı
   `nb_bits=0`/`nb_input_bits=0` ile bit tablolarına hiç yer ayırmıyordu — bir Coil mapping'i işlevsiz
   kalıyordu. Düzeltme: tahsis `modbus_mapping_new_start_address(0, 65536, 0, 65536, 0, 65536, 0, 65536)`
   yapıldı (coil + discrete-input bit tabloları da 0–65535 ayrıldı); `ModbusSink.cpp`'e `writeBit`/
   `clearBit` + `coilBitOf()` (TagValue → truthiness biti; format bit tiplerinde yok sayılır) yardımcıları
   eklendi; `syncAllMappings`/`drainQueue` bit tabanlı tiplerde bit yoluna dallanıyor. **Not/düzeltme:** bu
   maddenin ilk kaydında "Coil GUI'den seçilebilir" denmişti; aslında `Main.qml`'deki register-tipi
   combo'su `enabled: false` ile **Input Register'a kilitli** (hiçbir zaman GUI'den seçilemiyordu). Bugün
   de öyle: tüm register tipleri **arka planda** çalışıyor ama GUI seçimi kasıtlı olarak hâlâ kapalı
   (default Input Register); ileride combo açıldığında hepsi hazır olacak.
2. **✅ ÇÖZÜLDÜ (2026-07-31) — Discrete Input tablosu artık modellendi.** `ModbusMapping::RegisterType`'a
   `DiscreteInput` eklendi (`ModbusMapping.h`), `modbusRegisterTypeNames()`'e "Discrete Input" girdisi
   eklendi (`ModbusFormatNames.cpp`) ve sink onu `tab_input_bits` tablosuna yazıyor — böylece standart 4
   Modbus tablosunun (Coil/Discrete-Input/Holding/Input) tümü arka planda işlevsel. Madde 1'deki gibi GUI
   seçimi henüz açık değil.
3. **✅ ÇÖZÜLDÜ (2026-07-31) — Kalite (Quality) artık eşlik eden sağlık bitiyle yayınlanıyor.**
   Eskiden Modbus çıkışı `TagValue::quality`'yi tamamen yok sayıyordu; `Bad`/`Uncertain` kaliteli bir
   değer de Good gibi register'a yazılıyor, SCADA verinin güvenilmez olduğunu anlayamıyordu (Modbus'ta
   OPC'deki gibi bir "quality" alanı yoktur). **Çözüm — sağlık biti konvansiyonu (sıfır konfig):** bir
   word mapping'inin (Holding/Input Register) sağlığı, **aynı numaralı Discrete Input** bitinde
   yayınlanır → `ModbusSink::writeQualityBit()` `tab_input_bits[registerAddress] = (quality==Good)?1:0`
   yazar; `syncAllMappings`/`drainQueue` word yolunda çağrılır, mapping silinince `clearQualityBit` ile
   sıfırlanır. SCADA kuralı: **"register N'deki değerin sağlığını görmek için discrete-input N'i (FC02)
   oku"** — `1`=güvenilir, `0`=Good değil. Cihaz koparsa değer register'ı son iyi değerde donuk kalır
   (kaynak zaten öyle tutuyor) ve DI#N → 0'a düşer; sinyali bit taşır, ayrı dondurma mantığı gerekmez.
   `ModbusConverter::convert()` saf `value → words` olarak **değişmedi** (kalite tamamen sink
   seviyesinde). `Main.qml`'e dokunulmadı — konvansiyon otomatik çalışır. (İlk kayıtta silinen
   `tests/converter/test_modbus_converter.cpp`'de `KNOWN_LIMITATION_quality_ignored` olarak
   işaretlenmişti; `fa3d1b5` ile test altyapısıyla birlikte kayıttan düşmüştü.)
   **✅ ÇÖZÜLDÜ (2026-08-03) — GUI register-tipi combo'su acildi, DI[N] çakışması artık tespit ediliyor.**
   `Main.qml`'deki `typeCombo` artık `enabled: false` ile Input Register'a kilitli degil (madde 1'de
   bahsedilen gelecekteki adim gerceklesti). Bu, yukarida bahsedilen (a)/(b) çakışma senaryolarını
   gerçek hale getirdi: iki farklı registerType'taki mapping'ler `tab_input_bits[registerAddress]`'i
   paylaşabilir. `ModbusConverter::collidingNodeId()` artık `usesInputBitsSlot()` yardımcısıyla
   HoldingRegister/InputRegister/DiscreteInput mapping'leri arasında, registerType farklı olsa bile
   aynı `registerAddress`'i paylaşanları da çakışma olarak işaretliyor (Coil bu tabloya hiç dokunmadığı
   için kapsam dışı).
4. **BCD encode 10⁸ ve üzeri değerde sessizce sarmalanıyor (wrap).** `bcdEncode32()`
   (`ModbusConverter.cpp:112-120`) `value %= 100000000ULL` yapar; taşma clamp'lenmez, hata/log verilmez.
   Aynı silinmiş test dosyasında `KNOWN_LIMITATION_bcd_overflow_wraps` (~satır 250-258).
5. **Metin→sayı parse hatası sessizce 0 dönüyor.** `parseOrInt64()` (`ModbusConverter.cpp:53-58`)
   `std::stoll` başarısız olursa (`catch (...)`) hatayı yutup 0 döndürür; hiçbir yere loglanmaz. Aynı
   silinmiş test dosyasında `KNOWN_LIMITATION_text_parse_failure_is_silent_zero` (~satır 283-288).
6. **Modbus mapping'leri kalıcı değil.** `ModbusConverter::m_mappings` (`ModbusConverter.h:24`) yalnızca
   RAM'de tutulan bir `unordered_map`'tir; projede hiçbir kalıcılık mekanizması (`QSettings`, JSON, vb.)
   yoktur (`grep -rn "QSettings" src/` → hiç sonuç). Uygulama her yeniden başladığında tüm
   tag→register eşlemeleri kaybolur, GUI'den elle yeniden girilmesi gerekir.
7. **Tek istemci sınırı.** `ModbusSink::run()` aynı anda tek bir `clientSocket` tutar
   (`ModbusSink.cpp:132-165`); bir master bağlıyken ikinci bir master `accept()` edilip hemen
   kapatılır (`ModbusSink.cpp:153-154`) — çoklu master/poller senaryosu desteklenmez.
8. **Yazma yolu korumasız.** `modbus_reply()` (`ModbusSink.cpp:160`) mapping tablosuna karşı tüm standart
   fonksiyon kodlarını (okuma + yazma) servis eder; bağlanan herhangi bir master FC06/FC16 ile
   "salt-okunur ayna" olması gereken holding register'lara yazabilir. Uygulama bunu ne engeller ne fark
   eder — bir sonraki `writeWords()`'e kadar dışarıdan yazılan değer tabloda kalır.
9. **Modbus RTU/seri yok.** Yalnızca `modbus_new_tcp` kullanılır (`ModbusSink.cpp:103`); baud/parity/
   framing gibi seri parametreler için hiçbir yapı yoktur. Bu bir yön kararıdır, izole bir hata değil.
10. **Modbus bir veri kaynağı (master) değil, yalnızca çıkış.** `IDataSource`/`IOpcUaSource`'ı implemente
    eden tek sınıf `OpcUaDataSource`'tur; `GatewayManager` da mimari olarak tek kaynak varsayar
    (`std::unique_ptr<IOpcUaSource> m_source`, tekil, `GatewayManager.h:87`). Bir PLC/sayaçtan Modbus
    üzerinden okuma istenirse hem yeni bir kaynak sınıfı hem `GatewayManager`'da mimari değişiklik
    gerekir — §7'deki yol haritasının kapsamı dışındadır. En büyük yön kararı budur.
11. **✅ ÇÖZÜLDÜ (2026-07-31) — Dinleme IP'si artık yapılandırılabilir.** Eskiden `ModbusSink::run()`
    sabit `modbus_new_tcp(nullptr, m_port)` çağırıyordu — libmodbus bunu tüm ağ arayüzlerinde dinleme
    (`0.0.0.0`/`INADDR_ANY`) olarak yorumlar; birden fazla ağ arayüzü olan bir makinede kullanıcı
    sunucuyu belirli bir arayüze kısıtlayamıyordu. Düzeltme: `ModbusSink::setBindAddress(std::string)`
    eklendi (`ModbusSink.h/.cpp`); boş string eski davranışı (`nullptr` → tüm arayüzler) korur, dolu
    string `modbus_new_tcp`'ye geçilir. `ModbusController::startModbusServer` artık
    `(bindAddress, port, unitId)` alıyor; `Main.qml`'de "Dinleme IP" alanı eklendi (varsayılan boş).
12. **✅ ÇÖZÜLDÜ (2026-07-31) — Unit ID (Slave ID) artık yapılandırılabilir ve filtreleniyor.**
    Kod hiçbir yerde `modbus_set_slave()` çağırmıyordu; libmodbus varsayılanı `MODBUS_TCP_SLAVE`
    (`0xFF`, "her unit ID'yi kabul et") olduğundan sunucu MBAP header'daki unit ID'yi hiç
    denetlemeden her isteğe cevap veriyordu. Düzeltme: `ModbusSink::setUnitId(int)` eklendi
    (varsayılan `1`); `run()` içinde `modbus_new_tcp`'den hemen sonra `modbus_set_slave(ctx, m_unitId)`
    çağrılıyor — bundan sonra `modbus_reply()` yalnızca eşleşen unit ID'li istekleri yanıtlıyor,
    eşleşmeyenler sessizce yanıtsız bırakılıyor (standart libmodbus davranışı). `Main.qml`'de
    "Unit ID" `SpinBox`'ı eklendi (aralık 1-247, varsayılan 1).
13. **✅ ÇÖZÜLDÜ (2026-07-31) — Sunucu artık boş/dönüştürülmemiş mapping'le başlamıyor.** Eskiden
    `ModbusController::startModbusServer` hiçbir kontrol yapmadan sink'i başlatıyordu; hiç tag
    eşlenmemişken veya bir mapping var ama arkasındaki tag henüz OPC'den ilk değerini almamışken
    (`WatchRow::hasValue == false`) bile sunucu ayağa kalkıp tabloları sessizce sıfırlarla servis
    ediyordu — SCADA bunu "gerçek sıfır" ile "hiçbir şey eşlenmemiş"ten ayırt edemiyordu. Düzeltme:
    başlatmadan önce `m_watched` üzerinde en az bir satırın hem `hasModbusMapping` hem `hasValue`
    olduğu kontrol ediliyor; değilse başlatma reddediliyor ve `logMessage` ile iki alt-durumu ayırt
    eden bir hata mesajı basılıyor (hiç mapping yok / mapping var ama veri yok).
