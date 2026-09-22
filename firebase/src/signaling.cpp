#include "signaling.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>

#include "firebase/app.h"
#include "firebase/firestore.h"
#include "firebase/util.h"

firebase::firestore::Firestore* db = nullptr;
std::string active_room_id = "";
std::string active_log_level = "info";
bool active_tcp_only = false;

void SetupLogging(const std::string& level_str) {
    std::string level = level_str;
    std::transform(level.begin(), level.end(), level.begin(), ::tolower);

    if (level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (level == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else if (level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (level == "err" || level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else if (level == "off") {
        spdlog::set_level(spdlog::level::off);
    } else {
        spdlog::set_level(spdlog::level::info);
        level = "info";
    }

    active_log_level = level;
    SPDLOG_INFO("Configured spdlog level to: [{}]", active_log_level);
}

void LogSdpDtlsRoles(const std::string& sdp_text, const std::string& origin) {
    SPDLOG_INFO("--- [DTLS ROLE ANALYSIS: {}] ---", origin);
    
    if (sdp_text.find("a=setup:actpass") != std::string::npos) {
        SPDLOG_INFO("  |--> DTLS Role Setup: 'actpass' (Flexible - Offerer permits Peer to select DTLS Role)");
    } else if (sdp_text.find("a=setup:active") != std::string::npos) {
        SPDLOG_INFO("  |--> DTLS Role Setup: 'active'  ==> Operates as DTLS CLIENT (Initiates Handshake)");
    } else if (sdp_text.find("a=setup:passive") != std::string::npos) {
        SPDLOG_INFO("  |--> DTLS Role Setup: 'passive' ==> Operates as DTLS SERVER (Accepts Handshake)");
    }

    size_t fp_pos = sdp_text.find("a=fingerprint:");
    if (fp_pos != std::string::npos) {
        size_t end_pos = sdp_text.find("\r\n", fp_pos);
        std::string fp_line = sdp_text.substr(fp_pos, end_pos - fp_pos);
        SPDLOG_INFO("  |--> Security Fingerprint: {}", fp_line);
    }
}

bool InitializeFirebaseFramework() {
#if defined(WEBRTC_WIN)
    firebase::App* app = firebase::App::Create(firebase::AppOptions());
#else
    firebase::App* app = firebase::App::Create();
#endif

    if (!app) {
        SPDLOG_ERROR("Failed to initialize Firebase App context.");
        return false;
    }

    db = firebase::firestore::Firestore::GetInstance(app);
    if (!db) {
        SPDLOG_ERROR("Failed to instantiate Firestore database pointer.");
        return false;
    }

    firebase::firestore::Settings settings = db->settings();
    settings.set_persistence_enabled(false);
    db->set_settings(settings);

    return true;
}

void ShutdownFirebaseFramework(firebase::App* app) {
    if (db) { db = nullptr; }
    if (app) { delete app; }
}

bool SaveConfigToDisk(const std::string& room_id, const std::string& log_level, bool tcp_only, const std::string& filename) {
    std::ofstream configFile(filename);
    if (!configFile.is_open()) {
        SPDLOG_WARN("Could not open {} for writing.", filename);
        return false;
    }
    configFile << "ROOM_ID=" << room_id << "\n";
    configFile << "LOG_LEVEL=" << log_level << "\n";
    configFile << "TCP_ONLY=" << (tcp_only ? "true" : "false") << "\n";
    return true;
}

void LoadConfigFromDisk(std::string& out_room_id, std::string& out_log_level, bool& out_tcp_only, const std::string& filename) {
    std::ifstream configFile(filename);
    out_room_id.clear();
    out_log_level = "info";
    out_tcp_only = false;

    if (!configFile.is_open()) return;

    std::string line;
    while (std::getline(configFile, line)) {
        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '=')) {
            std::string value;
            if (std::getline(is_line, value)) {
                if (key == "ROOM_ID") {
                    out_room_id = value;
                } else if (key == "LOG_LEVEL") {
                    out_log_level = value;
                } else if (key == "TCP_ONLY" || key == "TCPONLY") {
                    std::string val_lower = value;
                    std::transform(val_lower.begin(), val_lower.end(), val_lower.begin(), ::tolower);
                    out_tcp_only = (val_lower == "true" || val_lower == "1");
                }
            }
        }
    }
}

void ValidateAndInitializeCaller(const std::string& room_id) {
    db->Collection("rooms").Document(room_id).Get()
      .OnCompletion([room_id](const firebase::Future<firebase::firestore::DocumentSnapshot>& future) {
          if (future.status() == firebase::kFutureStatusComplete && !future.error()) {
              auto snapshot = future.result();
              if (snapshot->exists()) {
                  active_room_id = room_id;
                  SPDLOG_INFO("Reusing existing room ID: {}. Resetting SDP session...", active_room_id);

                  peer_connection->CreateOffer(
                      new rtc::RefCountedObject<CallerCreateSessionDescriptionObserver>(), 
                      webrtc::PeerConnectionInterface::RTCOfferAnswerOptions()
                  );
                  return;
              }
          }
          
          SPDLOG_WARN("Room ID [{}] is invalid or deleted on server. Clearing configuration...", room_id);
          active_room_id.clear();
          SaveConfigToDisk("", active_log_level, active_tcp_only);

          SPDLOG_INFO("Fallback: Formulating new room document...");
          peer_connection->CreateOffer(
              new rtc::RefCountedObject<CallerCreateSessionDescriptionObserver>(), 
              webrtc::PeerConnectionInterface::RTCOfferAnswerOptions()
          );
      });
}

void DummySetSessionDescriptionObserver::OnSuccess() {
    SPDLOG_DEBUG("Applied session description configuration context.");
}

void DummySetSessionDescriptionObserver::OnFailure(webrtc::RTCError error) {
    SPDLOG_ERROR("Failed assigning session layer: {}", error.message());
}

void CallerCreateSessionDescriptionObserver::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
    peer_connection->SetLocalDescription(DummySetSessionDescriptionObserver::Create(), desc);

    std::string sdp_text;
    desc->ToString(&sdp_text);
    
    LogSdpDtlsRoles(sdp_text, "CALLER LOCAL OFFER");

    firebase::firestore::FieldValue offer_payload = firebase::firestore::FieldValue::Map({
        {"type", firebase::firestore::FieldValue::String("offer")},
        {"sdp", firebase::firestore::FieldValue::String(sdp_text)}
    });

    if (!active_room_id.empty()) {
        db->Collection("rooms").Document(active_room_id).Set({{"offer", offer_payload}})
          .OnCompletion([](const firebase::Future<void>& future) {
              if (future.status() == firebase::kFutureStatusComplete && !future.error()) {
                  SPDLOG_INFO("Reset offer SDP in existing Room ID: {}", active_room_id);
                  InitializeCallerSignaling();
              }
          });
    } else {
        db->Collection("rooms").Add({{"offer", offer_payload}})
          .OnCompletion([](const firebase::Future<firebase::firestore::DocumentReference>& future) {
              if (future.status() == firebase::kFutureStatusComplete && !future.error()) {
                  active_room_id = future.result()->id();
                  SaveConfigToDisk(active_room_id, active_log_level, active_tcp_only);

                  SPDLOG_INFO("Room created in Firebase! Room ID: {}", active_room_id);
                  InitializeCallerSignaling();
              }
          });
    }
}

void CallerCreateSessionDescriptionObserver::OnFailure(webrtc::RTCError error) {
    SPDLOG_ERROR("Failed to formulate offer payload description.");
}

void InitializeCallerSignaling() {
    db->Collection("rooms").Document(active_room_id)
      .AddSnapshotListener([](const firebase::firestore::DocumentSnapshot& snapshot, firebase::firestore::Error err, const std::string& error_msg) {
          if (err != firebase::firestore::kErrorOk || !snapshot.exists()) return;
          
          auto data = snapshot.GetData();
          if (data.find("answer") != data.end() && !peer_connection->remote_description()) {
              auto answer_map = data["answer"].map_value();
              std::string sdp = answer_map["sdp"].string_value();
              
              SPDLOG_INFO("Received Callee SDP Answer from Firestore.");
              LogSdpDtlsRoles(sdp, "CALLER RECEIVED REMOTE ANSWER");

              webrtc::SdpParseError parse_err;
              std::unique_ptr<webrtc::SessionDescriptionInterface> remote_desc = 
                  webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp, &parse_err);
              
              if (remote_desc) {
                  peer_connection->SetRemoteDescription(DummySetSessionDescriptionObserver::Create(), remote_desc.release());
              } else {
                  SPDLOG_ERROR("SDP Parsing Failed: {}", parse_err.description);
              }
          }
      });

    db->Collection("rooms").Document(active_room_id).Collection("calleeCandidates")
      .AddSnapshotListener([](const firebase::firestore::QuerySnapshot& snapshot, firebase::firestore::Error err, const std::string& error_msg) {
          if (err != firebase::firestore::kErrorOk) return;
          
          for (const auto& change : snapshot.DocumentChanges()) {
              if (change.type() == firebase::firestore::DocumentChange::Type::kAdded) {
                  auto data = change.document().GetData();
                  std::string cand = data["candidate"].string_value();
                  std::string mid = data["sdpMid"].string_value();
                  int index = static_cast<int>(data["sdpMLineIndex"].integer_value());

                  SPDLOG_INFO("[REMOTE CANDIDATE RECEIVED - CALLEE]");
                  SPDLOG_INFO("  |--> sdpMid:        {}", mid);
                  SPDLOG_INFO("  |--> sdpMLineIndex: {}", index);
                  SPDLOG_INFO("  |--> Candidate SDP: {}", cand);

                  if (active_tcp_only && cand.find(" udp ") != std::string::npos) {
                      SPDLOG_WARN("TCP_ONLY active: Dropped incoming UDP ICE candidate.");
                      continue;
                  }
                  
                  webrtc::SdpParseError parse_err;
                  std::unique_ptr<webrtc::IceCandidateInterface> native_cand(
                      webrtc::CreateIceCandidate(mid, index, cand, &parse_err));
                      
                  if (native_cand) {
                      peer_connection->AddIceCandidate(native_cand.get());
                      SPDLOG_INFO("Successfully added remote ICE candidate to Caller PeerConnection.");
                  } else {
                      SPDLOG_ERROR("Failed to parse caller candidate: {}", parse_err.description);
                  }
              }
          }
      });
}

void CalleeCreateSessionDescriptionObserver::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
    peer_connection->SetLocalDescription(DummySetSessionDescriptionObserver::Create(), desc);

    std::string sdp_text;
    desc->ToString(&sdp_text);
    
    LogSdpDtlsRoles(sdp_text, "CALLEE LOCAL ANSWER");

    firebase::firestore::FieldValue answer_payload = firebase::firestore::FieldValue::Map({
        {"type", firebase::firestore::FieldValue::String("answer")},
        {"sdp", firebase::firestore::FieldValue::String(sdp_text)}
    });

    db->Collection("rooms").Document(active_room_id).Update({{"answer", answer_payload}});
    SPDLOG_INFO("Dispatched answer mapping. Building P2P path...");
}

void CalleeCreateSessionDescriptionObserver::OnFailure(webrtc::RTCError error) {
    SPDLOG_ERROR("Failed to formulate callee answer description.");
}

void InitializeCalleeSignaling(const std::string& room_id) {
    active_room_id = room_id;
    SPDLOG_INFO("Fetching Room Profile parameters: {}", active_room_id);

    db->Collection("rooms").Document(active_room_id).Get()
      .OnCompletion([](const firebase::Future<firebase::firestore::DocumentSnapshot>& future) {
          if (future.status() != firebase::kFutureStatusComplete || future.error()) return;
          
          auto snapshot = future.result();
          if (!snapshot->exists()) {
              SPDLOG_ERROR("Target room ID '{}' does not exist or was deleted on Firebase!", active_room_id);
              active_room_id.clear();
              return;
          }

          auto data = snapshot->GetData();
          auto offer_map = data["offer"].map_value();
          std::string sdp = offer_map["sdp"].string_value();

          SPDLOG_INFO("Callee fetched host SDP offer successfully.");
          LogSdpDtlsRoles(sdp, "CALLEE RECEIVED REMOTE OFFER");

          webrtc::SdpParseError parse_err;
          std::unique_ptr<webrtc::SessionDescriptionInterface> remote_offer = 
              webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp, &parse_err);
          
          if (remote_offer) {
              peer_connection->SetRemoteDescription(DummySetSessionDescriptionObserver::Create(), remote_offer.release());

              db->Collection("rooms").Document(active_room_id).Collection("callerCandidates")
                .AddSnapshotListener([](const firebase::firestore::QuerySnapshot& q_snapshot, firebase::firestore::Error err, const std::string& error_msg) {
                    if (err != firebase::firestore::kErrorOk) return;
                    
                    for (const auto& change : q_snapshot.DocumentChanges()) {
                        if (change.type() == firebase::firestore::DocumentChange::Type::kAdded) {
                            auto d = change.document().GetData();
                            std::string cand = d["candidate"].string_value();
                            std::string mid = d["sdpMid"].string_value();
                            int idx = static_cast<int>(d["sdpMLineIndex"].integer_value());

                            SPDLOG_INFO("[REMOTE CANDIDATE RECEIVED - CALLER]");
                            SPDLOG_INFO("  |--> sdpMid:        {}", mid);
                            SPDLOG_INFO("  |--> sdpMLineIndex: {}", idx);
                            SPDLOG_INFO("  |--> Candidate SDP: {}", cand);

                            if (active_tcp_only && cand.find(" udp ") != std::string::npos) {
                                SPDLOG_WARN("TCP_ONLY active: Dropped incoming UDP ICE candidate.");
                                continue;
                            }
                            
                            webrtc::SdpParseError c_parse_err;
                            std::unique_ptr<webrtc::IceCandidateInterface> native_cand(
                                webrtc::CreateIceCandidate(mid, idx, cand, &c_parse_err));
                                
                            if (native_cand) {
                                peer_connection->AddIceCandidate(native_cand.get());
                                SPDLOG_INFO("Successfully added remote ICE candidate to Callee PeerConnection.");
                            } else {
                                SPDLOG_ERROR("Failed to parse callee candidate: {}", c_parse_err.description);
                            }
                        }
                    }
                });

              peer_connection->CreateAnswer(
                  new rtc::RefCountedObject<CalleeCreateSessionDescriptionObserver>(), webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
          } else {
              SPDLOG_ERROR("SDP Parsing Failed: {}", parse_err.description);
          }
      });
}