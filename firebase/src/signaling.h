#ifndef SIGNALING_H
#define SIGNALING_H

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#include <string>
#include <fstream>
#include <cstdio>
#include <spdlog/spdlog.h>

#include "api/peer_connection_interface.h"
#include "api/data_channel_interface.h"

namespace firebase {
    class App;
    namespace firestore {
        class Firestore;
    }
}

extern firebase::firestore::Firestore* db;
extern std::string active_room_id;
extern std::string active_log_level;
extern bool active_tcp_only;
extern rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection;
extern rtc::scoped_refptr<webrtc::DataChannelInterface> native_data_channel;
extern webrtc::DataChannelObserver* global_dc_observer;

// Firebase initialization
bool InitializeFirebaseFramework();
void ShutdownFirebaseFramework(firebase::App* app);
void InitializeCallerSignaling();
void InitializeCalleeSignaling(const std::string& room_id);

// Persistence & validation helpers
void SetupLogging(const std::string& level_str);
bool SaveConfigToDisk(const std::string& room_id, const std::string& log_level = "info", bool tcp_only = false, const std::string& filename = "config.txt");
void LoadConfigFromDisk(std::string& out_room_id, std::string& out_log_level, bool& out_tcp_only, const std::string& filename = "config.txt");
void ValidateAndInitializeCaller(const std::string& room_id);
void LogSdpDtlsRoles(const std::string& sdp_text, const std::string& origin);

// WebRTC Observers
class DummySetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
public:
    static DummySetSessionDescriptionObserver* Create() {
        return new rtc::RefCountedObject<DummySetSessionDescriptionObserver>();
    }
    void OnSuccess() override;
    void OnFailure(webrtc::RTCError error) override;
};

class CallerCreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver {
public:
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
    void OnFailure(webrtc::RTCError error) override;
};

class CalleeCreateSessionDescriptionObserver : public webrtc::CreateSessionDescriptionObserver {
public:
    void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
    void OnFailure(webrtc::RTCError error) override;
};

#endif // SIGNALING_H