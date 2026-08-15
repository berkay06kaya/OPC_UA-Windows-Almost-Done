#include "source/OpcUaDataSource.h"
#include "source/OpcUaSubscriber.h"
#include "source/UaUtils.h"
#include "core/Logger.h"
#include <chrono>
#include <iostream>
#include <thread>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/securitypolicy_default.h>
#include <open62541/plugin/pki_default.h>

#ifndef OPC_CERT_DIR
#define OPC_CERT_DIR "certs"
#endif

#ifdef OPC_HAVE_EMBEDDED_CERTS
#include "EmbeddedCerts.h"
#endif

OpcUaDataSource::OpcUaDataSource(DataStore* dataStore)
    : m_autoReconnect(true),
      m_lastReconnectAttempt(std::chrono::steady_clock::now()),
      m_client(nullptr),
      m_dataStore(dataStore),
      m_subscriber(std::make_unique<OpcUaSubscriber>(dataStore)) {
}

struct OpcUaDataSourceInternal {
    static void stateCallback(UA_Client* client,
                              UA_SecureChannelState channelState,
                              UA_SessionState sessionState,
                              UA_StatusCode connectStatus) {
        void* ctx = UA_Client_getContext(client);
        if (ctx == nullptr) return;
        auto* self = static_cast<OpcUaDataSource*>(ctx);
        self->m_lastStatusCode = connectStatus;

        if (sessionState == UA_SESSIONSTATE_ACTIVATED) {
            if (self->m_phase.load() != OpcUaDataSource::Connected)
                std::cout << "[OPC UA] Oturum aktif (Session ACTIVATED)." << std::endl;
            self->setPhase(OpcUaDataSource::Connected);
            return;
        }

        if (connectStatus != UA_STATUSCODE_GOOD) {
            if (self->m_phase.load() == OpcUaDataSource::Idle) return;

            const bool identityErr =
                connectStatus == UA_STATUSCODE_BADIDENTITYTOKENREJECTED ||
                connectStatus == UA_STATUSCODE_BADIDENTITYTOKENINVALID  ||
                connectStatus == UA_STATUSCODE_BADUSERACCESSDENIED;

            if (self->m_autoReconnect && !identityErr) {
                self->m_lastReconnectAttempt = std::chrono::steady_clock::now();
                self->setPhase(OpcUaDataSource::Reconnecting);
                LOG_ERROR() << "[OPC UA][TIMING] t=" << ua::nowMs() << "ms Baglanti hatasi ("
                            << UA_StatusCode_name(connectStatus)
                            << "). Yeniden baglanma moduna gecildi.";
            } else {
                self->setPhase(OpcUaDataSource::Failed);
            }
            return;
        }

        if (channelState == UA_SECURECHANNELSTATE_CLOSED &&
            self->m_phase.load() == OpcUaDataSource::Connected) {
            if (self->m_autoReconnect) {
                self->m_lastReconnectAttempt = std::chrono::steady_clock::now();
                self->setPhase(OpcUaDataSource::Reconnecting);
                LOG_ERROR() << "[OPC UA][TIMING] t=" << ua::nowMs()
                            << "ms BAGLANTI KOPTU! Otomatik yeniden baglanma moduna gecildi.";
            } else {
                self->setPhase(OpcUaDataSource::Idle);
            }
        }
    }

    static void subscriptionInactivityCallback(UA_Client* , UA_UInt32 subId, void* ) {
        LOG_ERROR() << "[OPC UA][TIMING] t=" << ua::nowMs() << "ms subscriptionInactivityCallback "
                       "tetiklendi (subId=" << subId << ") -- istemcinin KENDI publish hatti "
                       "beklenen surede PublishResponse almadi (sunucudan bagimsiz istemci-tarafi sinyal).";
    }
};

OpcUaDataSource::~OpcUaDataSource() {}

void OpcUaDataSource::setConnectionConfig(const EndpointInfo& endpoint, const std::string& user, const std::string& pass) {
    m_targetEndpoint = endpoint;
    m_username = user;
    m_password = pass;
}

void OpcUaDataSource::setPhaseCallback(std::function<void(int, std::uint32_t)> cb) {
    m_phaseCallback = std::move(cb);
}

void OpcUaDataSource::setPhase(Phase p) {
    if (m_phase.exchange(p) != p && m_phaseCallback) {
        m_phaseCallback(p, m_lastStatusCode.load());
    }
}

bool OpcUaDataSource::connectClient() {
    m_client = UA_Client_new();
    UA_ClientConfig* config = UA_Client_getConfig(m_client);

#ifdef OPC_HAVE_EMBEDDED_CERTS
    UA_ByteString cert = ua::byteStringFromBytes(ua::kEmbeddedClientCertDer, ua::kEmbeddedClientCertDerLen);
    UA_ByteString key  = ua::byteStringFromBytes(ua::kEmbeddedClientKeyDer, ua::kEmbeddedClientKeyDerLen);
#else
    UA_ByteString cert = ua::loadFile(OPC_CERT_DIR "/client_cert.der");
    UA_ByteString key  = ua::loadFile(OPC_CERT_DIR "/client_key.der");
#endif
    const bool certsReady = (cert.length > 0 && key.length > 0);

    const bool certRequired =
        (m_targetEndpoint.securityMode != UA_MESSAGESECURITYMODE_NONE) || !m_username.empty();

    if (certsReady) {
        UA_ClientConfig_setDefaultEncryption(config, cert, key, NULL, 0, NULL, 0);

        config->certificateVerification.clear(&config->certificateVerification);
        UA_CertificateVerification_AcceptAll(&config->certificateVerification);

        UA_String_clear(&config->clientDescription.applicationUri);
        config->clientDescription.applicationUri = UA_STRING_ALLOC("urn:YamanGateway:client");
        UA_LocalizedText_clear(&config->clientDescription.applicationName);
        config->clientDescription.applicationName =
            UA_LOCALIZEDTEXT_ALLOC("en", "Yaman Gateway Client");
    } else if (certRequired) {
        LOG_ERROR() << "[-] HATA: Sertifika dosyalari bulunamadi (" << OPC_CERT_DIR
                    << "/client_cert.der ve client_key.der).";
        UA_ByteString_clear(&cert);
        UA_ByteString_clear(&key);
        UA_Client_delete(m_client);
        m_client = nullptr;
        setPhase(Failed);
        return false;
    } else {
        UA_ClientConfig_setDefault(config);
    }

    UA_ByteString_clear(&cert);
    UA_ByteString_clear(&key);

    config->securityMode = (UA_MessageSecurityMode)m_targetEndpoint.securityMode;
    UA_String_clear(&config->securityPolicyUri);
    config->securityPolicyUri = UA_STRING_ALLOC(m_targetEndpoint.securityPolicyUri.c_str());

    if (!m_username.empty()) {
        UA_UserNameIdentityToken* identityToken = UA_UserNameIdentityToken_new();
        identityToken->userName = UA_STRING_ALLOC(m_username.c_str());
        identityToken->password = UA_STRING_ALLOC(m_password.c_str());
        UA_ExtensionObject_clear(&config->userIdentityToken);
        config->userIdentityToken.encoding = UA_EXTENSIONOBJECT_DECODED;
        config->userIdentityToken.content.decoded.type = &UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN];
        config->userIdentityToken.content.decoded.data = identityToken;
    }

    config->clientContext = this;
    config->stateCallback = &OpcUaDataSourceInternal::stateCallback;
    config->subscriptionInactivityCallback = &OpcUaDataSourceInternal::subscriptionInactivityCallback;

    m_lastStatusCode = 0;
    m_connectingSince = std::chrono::steady_clock::now();
    setPhase(Connecting);

    const std::string targetIp = ua::hostFromUrl(m_targetEndpoint.endpointUrl);
    std::cout << "\n---------- BAGLANTI ONCESI HEDEF ----------\n"
              << "  IP adresi     : " << targetIp << "\n"
              << "  Endpoint URL  : " << m_targetEndpoint.endpointUrl << "\n"
              << "  Guvenlik modu : " << m_targetEndpoint.securityModeString << "\n"
              << "  Politika      : " << m_targetEndpoint.getShortSecurityPolicy() << "\n"
              << "  Giris         : " << (m_username.empty() ? "Anonim" : ("Kullanici: " + m_username)) << "\n"
              << "-------------------------------------------" << std::endl;
    UA_StatusCode retval = UA_Client_connectAsync(m_client, m_targetEndpoint.endpointUrl.c_str());
    if (retval != UA_STATUSCODE_GOOD) {
        m_lastStatusCode = retval;
        LOG_ERROR() << "[-] Async baglanti baslatilamadi! Kod: " << UA_StatusCode_name(retval);
        cleanupClient();
        setPhase(Failed);
        return false;
    }
    return true;
}

void OpcUaDataSource::cleanupClient() {
    if (m_client == nullptr) return;

    m_phase = Idle;

    UA_Client_disconnect(m_client);
    UA_Client_delete(m_client);
    m_client = nullptr;
}

bool OpcUaDataSource::isConnected() const {
    return m_phase.load() == Connected;
}

bool OpcUaDataSource::isReconnecting() const {
    return m_phase.load() == Reconnecting;
}

std::string OpcUaDataSource::lastStatusName() const {
    return UA_StatusCode_name(static_cast<UA_StatusCode>(m_lastStatusCode.load()));
}

std::string OpcUaDataSource::connectionHint() const {
    const std::string code = lastStatusName();
    if (code.find("IdentityToken") != std::string::npos ||
        code.find("UserAccess") != std::string::npos) {
        return "Bu endpoint anonim girisi kabul etmiyor veya kimlik reddedildi; "
               "kullanici adi/sifre deneyin.";
    }
    return "";
}

void OpcUaDataSource::setAutoReconnect(bool enabled) {
    m_autoReconnect = enabled;
    if (!enabled && m_phase.load() == Reconnecting) m_phase = Idle;
}

void OpcUaDataSource::run(const std::atomic<bool>& running) {
    if (m_targetEndpoint.endpointUrl.empty()) {
        LOG_ERROR() << "[-] HATA: Hedef Endpoint ayarlanmamis. Lutfen once setConnectionConfig() cagiriniz.";
        setPhase(Failed);
        return;
    }

    connectClient();

    while (running.load()) {
        const int phase = m_phase.load();

        if (m_client != nullptr && (phase == Connecting || phase == Connected)) {
            UA_StatusCode rv = UA_Client_run_iterate(m_client, 150.0);

            if (rv != UA_STATUSCODE_GOOD && m_phase.load() == Connected) {
                m_lastStatusCode = rv;
                if (m_autoReconnect) {
                    m_lastReconnectAttempt = std::chrono::steady_clock::now();
                    setPhase(Reconnecting);
                    LOG_ERROR() << "[OPC UA][TIMING] t=" << ua::nowMs() << "ms Ag hatasi ("
                                << UA_StatusCode_name(rv)
                                << "). Yeniden baglanma moduna gecildi.";
                } else {
                    setPhase(Idle);
                }
            }

            if (m_phase.load() == Connecting &&
                std::chrono::steady_clock::now() - m_connectingSince >= CONNECT_TIMEOUT) {
                LOG_ERROR() << "[OPC UA][TIMING] t=" << ua::nowMs() << "ms Baglanma zaman asimina ugradi ("
                            << CONNECT_TIMEOUT.count()
                            << "sn) - sunucu yanit vermiyor. Vazgeciliyor.";
                cleanupClient();
                if (m_autoReconnect) {
                    m_lastReconnectAttempt = std::chrono::steady_clock::now();
                    setPhase(Reconnecting);
                } else {
                    setPhase(Failed);
                }
            }
        }
        else if (phase == Reconnecting && m_autoReconnect) {
            const auto now = std::chrono::steady_clock::now();
            if (now - m_lastReconnectAttempt >= RECONNECT_INTERVAL) {
                m_lastReconnectAttempt = now;
                LOG_TIMING() << "[OPC UA][TIMING] t=" << ua::nowMs() << "ms Yeniden baglanma denemesi...";
                cleanupClient();
                connectClient();
            }
        }

        if (m_client != nullptr && m_phase.load() == Connected) {
            if (!m_subscriber->isActive()) {
                const auto now = std::chrono::steady_clock::now();
                if (now - m_lastSubscribeAttempt >= SUBSCRIBE_RETRY_INTERVAL) {
                    m_lastSubscribeAttempt = now;
                    m_subscriber->onConnected(m_client);
                }
            } else {
                m_subscriber->tick(m_client);
            }
        } else if (m_subscriber->isActive()) {
            m_subscriber->onDisconnected();
        }

        while (true) {
            std::function<void()> task;
            {
                std::lock_guard<std::mutex> lk(m_taskMutex);
                if (m_taskQueue.empty()) break;
                task = std::move(m_taskQueue.front());
                m_taskQueue.pop_front();
            }
            const std::int64_t taskStart = ua::nowMs();
            task();
            const std::int64_t taskMs = ua::nowMs() - taskStart;
            if (taskMs > 50)
                LOG_TIMING() << "[OPC UA][TIMING] worker gorevi " << taskMs
                             << "ms surdu (run_iterate bu sure boyunca calismadi)";
        }

        while (true) {
            BrowseRequest req;
            {
                std::lock_guard<std::mutex> lk(m_browseMutex);
                if (m_browseQueue.empty()) break;
                req = std::move(m_browseQueue.front());
                m_browseQueue.pop_front();
            }
            if (m_client != nullptr && m_phase.load() == Connected)
                req.done(m_browser.browse(m_client, req.nodeId));
            else
                req.done({});
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (m_subscriber->isActive()) m_subscriber->shutdown(m_client);
    cleanupClient();
}

void OpcUaDataSource::browseChildrenAsync(const std::string& nodeId,
                                          std::function<void(std::vector<BrowseNode>)> cb) {
    std::lock_guard<std::mutex> lk(m_browseMutex);
    m_browseQueue.push_back(BrowseRequest{nodeId, std::move(cb)});
}

std::vector<BrowseNode> OpcUaDataSource::browseChildren(const std::string& nodeId) {
    if (m_phase.load() != Connected) return {};

    auto prom = std::make_shared<std::promise<std::vector<BrowseNode>>>();
    auto fut = prom->get_future();
    browseChildrenAsync(nodeId, [prom](std::vector<BrowseNode> res) {
        prom->set_value(std::move(res));
    });

    if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        LOG_ERROR() << "[BROWSE] zaman asimi (5s): " << nodeId;
        return {};
    }
    return fut.get();
}

void OpcUaDataSource::postToWorker(std::function<void()> task) {
    std::lock_guard<std::mutex> lk(m_taskMutex);
    m_taskQueue.push_back(std::move(task));
}

void OpcUaDataSource::setValueCallback(
    std::function<void(const std::string&, const TagValue&)> cb) {
    m_subscriber->setValueCallback(std::move(cb));
}

void OpcUaDataSource::setChunkSizeCallback(
    std::function<void(size_t, bool, std::uint32_t)> cb) {
    m_subscriber->setChunkSizeCallback(std::move(cb));
}

void OpcUaDataSource::subscribe(const std::string& nodeId, const std::string& logicalName) {
    postToWorker([this, nodeId, logicalName]() {
        m_subscriber->addNode(m_client, nodeId, logicalName);
    });
}

void OpcUaDataSource::subscribeMany(const std::vector<std::pair<std::string, std::string>>& nodes) {
    postToWorker([this, nodes]() {
        m_subscriber->addNodes(m_client, nodes);
    });
}

void OpcUaDataSource::unsubscribe(const std::string& nodeId) {
    postToWorker([this, nodeId]() {
        m_subscriber->removeNode(m_client, nodeId);
    });
}

void OpcUaDataSource::unsubscribeMany(const std::vector<std::string>& nodeIds) {
    postToWorker([this, nodeIds]() {
        m_subscriber->removeNodes(m_client, nodeIds);
    });
}
