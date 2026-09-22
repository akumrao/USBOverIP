#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

#include "signaling.h"
#include "webrtc_stats.h"

#include "api/create_peerconnection_factory.h"
#include "api/peer_connection_interface.h"
#include "rtc_base/thread.h"

#include "firebase/app.h"
#include "firebase/firestore.h"

rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection = nullptr;
rtc::scoped_refptr<webrtc::DataChannelInterface> native_data_channel = nullptr;
webrtc::DataChannelObserver* global_dc_observer = nullptr;

std::atomic<bool> keep_running{true};
std::string shared_input_buffer = "";
std::mutex input_mutex;

class CustomDataChannelObserver : public webrtc::DataChannelObserver {
public:
    void OnStateChange() override {
        if (native_data_channel) {
            SPDLOG_INFO("DataChannel state changed: {}", webrtc::DataChannelInterface::DataStateString(native_data_channel->state()));
            if (native_data_channel->state() == webrtc::DataChannelInterface::kOpen) {
                SPDLOG_INFO("DataChannel Open! You can now send messages.");
            }
        }
    }
    
    void OnMessage(const webrtc::DataBuffer& buffer) override {
        std::string inbound_text(reinterpret_cast<const char*>(buffer.data.data()), buffer.data.size());
        SPDLOG_INFO("PEER MESSAGE: {}", inbound_text);
    }
    
    void OnBufferedAmountChange(uint64_t previous_amount) override {}
};
CustomDataChannelObserver data_channel_observer;

class CustomPeerConnectionObserver : public webrtc::PeerConnectionObserver {
public:
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override {
        if (active_room_id.empty() || !db) return;

        std::string candidate_str;
        if (!candidate->ToString(&candidate_str)) return;

        std::string sdp_mid = candidate->sdp_mid();
        int sdp_mline_index = candidate->sdp_mline_index();
        std::string protocol = (candidate_str.find(" tcp ") != std::string::npos) ? "TCP" : "UDP";

        SPDLOG_INFO("[LOCAL ICE CANDIDATE GATHERED]");
        SPDLOG_INFO("  |--> Protocol:      {}", protocol);
        SPDLOG_INFO("  |--> sdpMid:        {}", sdp_mid);
        SPDLOG_INFO("  |--> sdpMLineIndex: {}", sdp_mline_index);
        SPDLOG_INFO("  |--> Candidate SDP: {}", candidate_str);

        if (active_tcp_only && protocol == "UDP") {
            SPDLOG_WARN("TCP_ONLY mode active: Filtered out local UDP ICE candidate.");
            return;
        }

        firebase::firestore::MapFieldValue cand_payload = {
            {"candidate", firebase::firestore::FieldValue::String(candidate_str)},
            {"sdpMid", firebase::firestore::FieldValue::String(sdp_mid)},
            {"sdpMLineIndex", firebase::firestore::FieldValue::Integer(sdp_mline_index)}
        };

        std::string sub_collection = (native_data_channel != nullptr) ? "callerCandidates" : "calleeCandidates";
        
        db->Collection("rooms").Document(active_room_id)
          .Collection(sub_collection).Add(cand_payload);
    }

    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override {
        std::string state_str;
        switch (new_state) {
            case webrtc::PeerConnectionInterface::kIceConnectionNew:          state_str = "NEW"; break;
            case webrtc::PeerConnectionInterface::kIceConnectionChecking:     state_str = "CHECKING (Pairing candidate checks in progress)"; break;
            case webrtc::PeerConnectionInterface::kIceConnectionConnected:    state_str = "CONNECTED (Pair Nominated & Selected)"; break;
            case webrtc::PeerConnectionInterface::kIceConnectionCompleted:    state_str = "COMPLETED (All ICE checks completed)"; break;
            case webrtc::PeerConnectionInterface::kIceConnectionFailed:       state_str = "FAILED (Candidate pairing timed out)"; break;
            case webrtc::PeerConnectionInterface::kIceConnectionDisconnected: state_str = "DISCONNECTED"; break;
            case webrtc::PeerConnectionInterface::kIceConnectionClosed:       state_str = "CLOSED"; break;
            default: state_str = "UNKNOWN"; break;
        }
        SPDLOG_INFO("==> [ICE CONNECTION STATE]: {}", state_str);

        // Fetch and print nominated candidate pair statistics when ICE connects
        if (new_state == webrtc::PeerConnectionInterface::kIceConnectionConnected && peer_connection) {
            peer_connection->GetStats(NominatedCandidatePairStatsCallback::Create());
        }
    }

    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override {
        std::string g_state = (new_state == webrtc::PeerConnectionInterface::kIceGatheringGathering) ? "GATHERING" : "COMPLETE";
        SPDLOG_INFO("==> [ICE GATHERING STATE]: {}", g_state);
    }

    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState new_state) override {
        std::string pc_state;
        switch (new_state) {
            case webrtc::PeerConnectionInterface::PeerConnectionState::kNew:          pc_state = "New"; break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:   pc_state = "Connecting (DTLS Handshake processing)"; break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:    pc_state = "Connected (DTLS Secure Handshake Complete, P2P Ready)"; break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected: pc_state = "Disconnected"; break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:       pc_state = "Failed"; break;
            case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:       pc_state = "Closed"; break;
        }
        SPDLOG_INFO("==> [PEER CONNECTION STATE]: {}", pc_state);
    }

    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override {
        SPDLOG_INFO("--> Remote DataChannel negotiated and attached.");
        native_data_channel = data_channel;
        native_data_channel->RegisterObserver(global_dc_observer);
    }

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override {}
    void OnIceCandidatesRemoved(const std::vector<cricket::Candidate>& candidates) override {}
    void OnRenegotiationNeeded() override {}
};
CustomPeerConnectionObserver peer_connection_observer;

int main() {
    std::string saved_room_id, saved_log_level;
    bool tcp_only_flag = false;
    
    LoadConfigFromDisk(saved_room_id, saved_log_level, tcp_only_flag);
    SetupLogging(saved_log_level);
    active_tcp_only = tcp_only_flag;

    SPDLOG_INFO("=== Cross-Platform WebRTC Native Signaling App ===");
    SPDLOG_INFO("TCP_ONLY Mode: [{}]", active_tcp_only ? "ENABLED" : "DISABLED");
    global_dc_observer = &data_channel_observer;

    if (!InitializeFirebaseFramework()) {
        SPDLOG_ERROR("Initialization Fault: Check google-services.json context.");
        return -1;
    }
    SPDLOG_INFO("Successfully authenticated with Firebase Database.");

    std::unique_ptr<rtc::Thread> network_thread = rtc::Thread::CreateWithSocketServer();
    std::unique_ptr<rtc::Thread> worker_thread = rtc::Thread::Create();
    std::unique_ptr<rtc::Thread> signaling_thread = rtc::Thread::Create();
    network_thread->Start(); worker_thread->Start(); signaling_thread->Start();

    webrtc::PeerConnectionInterface::RTCConfiguration config;
    webrtc::PeerConnectionInterface::IceServer server;
    
    // TURN/STUN Server assignment (use TCP port for STUN/TURN if TCP_ONLY is true)
    if (active_tcp_only) {
        server.urls.push_back("stun:stun.l.google.com:19302");
        //server.urls.push_back("stun:global.stun.twilio.com:3478?transport=tcp");
        config.tcp_candidate_policy = webrtc::PeerConnectionInterface::kTcpCandidatePolicyEnabled;
        
    } else {
        server.urls.push_back("stun:stun.l.google.com:19302");
    }
    config.servers.push_back(server);

    webrtc::PeerConnectionFactoryDependencies factory_deps;
    factory_deps.network_thread = network_thread.get();
    factory_deps.worker_thread = worker_thread.get();
    factory_deps.signaling_thread = signaling_thread.get();

    auto factory = webrtc::CreateModularPeerConnectionFactory(std::move(factory_deps));

    if (!factory) {
        SPDLOG_ERROR("Fatal Error: Failed to build modular WebRTC PC Factory Interface.");
        return -1;
    }

    webrtc::PeerConnectionDependencies dependencies(&peer_connection_observer);
    auto pc_result = factory->CreatePeerConnectionOrError(config, std::move(dependencies));
    if (!pc_result.ok()) return -1;
    peer_connection = pc_result.MoveValue();

    std::cout << "\nSelect Execution Profile:\n1. Host Room (Caller Mode)\n2. Connect Room (Callee Mode)\nChoice: ";
    int choice;
    std::cin >> choice;

    if (choice == 1) {
        webrtc::DataChannelInit dc_config;
        auto dc_result = peer_connection->CreateDataChannelOrError("chat-channel", &dc_config);
        if (dc_result.ok()) {
            native_data_channel = dc_result.MoveValue();
            native_data_channel->RegisterObserver(global_dc_observer);
        }

        if (!saved_room_id.empty()) {
            SPDLOG_INFO("Found cached room ID: {}. Validating with Firebase...", saved_room_id);
            ValidateAndInitializeCaller(saved_room_id);
        } else {
            SPDLOG_INFO("No saved room found. Creating new room...");
            peer_connection->CreateOffer(
                new rtc::RefCountedObject<CallerCreateSessionDescriptionObserver>(), 
                webrtc::PeerConnectionInterface::RTCOfferAnswerOptions()
            );
        }
    } else {
        std::string input_room;
        std::cout << "Enter target room identifier string token: ";
        std::cin >> input_room;
        InitializeCalleeSignaling(input_room);
    }

    std::cout << "\n>>> P2P Engine Active. Type messages below or use '/exit' to quit.\n" << std::endl;
    
    std::thread input_thread([&]() {
        std::cin.ignore();
        while (keep_running) {
            std::string temp_input;
            if (std::getline(std::cin, temp_input)) {
                if (temp_input == "/exit") {
                    keep_running = false;
                    break;
                }
                std::lock_guard<std::mutex> lock(input_mutex);
                shared_input_buffer = temp_input;
            }
        }
    });

    while (keep_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        
        std::string active_msg = "";
        {
            std::lock_guard<std::mutex> lock(input_mutex);
            if (!shared_input_buffer.empty()) {
                active_msg = shared_input_buffer;
                shared_input_buffer.clear();
            }
        }

        if (!active_msg.empty()) {
            if (native_data_channel && native_data_channel->state() == webrtc::DataChannelInterface::kOpen) {
                webrtc::DataBuffer buffer(active_msg);
                native_data_channel->Send(buffer);
                SPDLOG_INFO("[Sent]: {}", active_msg);
            } else {
                SPDLOG_WARN("Data pipeline is not open yet.");
            }
        }
    }

    if (input_thread.joinable()) {
        input_thread.join();
    }

    peer_connection = nullptr;
    native_data_channel = nullptr;
    ShutdownFirebaseFramework(nullptr); 
    return 0;
}
