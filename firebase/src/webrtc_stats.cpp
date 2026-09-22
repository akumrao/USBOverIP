#include "webrtc_stats.h"

void NominatedCandidatePairStatsCallback::OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    SPDLOG_INFO("=================================================");
    SPDLOG_INFO(">>> NOMINATED ICE CANDIDATE PAIR DETAILS <<<");
    SPDLOG_INFO("=================================================");

    std::string selected_pair_id = "";
    
    // 1. Locate the selected Candidate Pair ID via Transport Stats
    for (const auto& stats : *report) {
        if (stats.type() == webrtc::RTCTransportStats::kType) {
            const auto& transport = static_cast<const webrtc::RTCTransportStats&>(stats);
            if (transport.selected_candidate_pair_id.has_value()) {
                selected_pair_id = *transport.selected_candidate_pair_id;
                break;
            }
        }
    }

    // 2. Iterate Candidate Pairs and print the active nominated pair
    for (const auto& stats : *report) {
        if (stats.type() == webrtc::RTCIceCandidatePairStats::kType) {
            const auto& pair = static_cast<const webrtc::RTCIceCandidatePairStats&>(stats);
            
            bool is_nominated = pair.nominated.has_value() && *pair.nominated;

            if ((!selected_pair_id.empty() && pair.id() == selected_pair_id) || is_nominated) {
                
                std::string local_cand_id  = pair.local_candidate_id.has_value() ? *pair.local_candidate_id : "N/A";
                std::string remote_cand_id = pair.remote_candidate_id.has_value() ? *pair.remote_candidate_id : "N/A";
                std::string pair_state     = pair.state.has_value() ? *pair.state : "N/A";
                uint64_t bytes_sent        = pair.bytes_sent.has_value() ? *pair.bytes_sent : 0;
                uint64_t bytes_recv        = pair.bytes_received.has_value() ? *pair.bytes_received : 0;

                SPDLOG_INFO("  |--> Pair ID:               {}", pair.id());
                SPDLOG_INFO("  |--> Pairing State:         {}", pair_state);
                SPDLOG_INFO("  |--> Local Candidate ID:    {}", local_cand_id);
                SPDLOG_INFO("  |--> Remote Candidate ID:   {}", remote_cand_id);
                SPDLOG_INFO("  |--> Bytes Sent / Recv:     {} / {} bytes", bytes_sent, bytes_recv);

                // 3. Resolve Local Candidate Stats Object
                const auto* local_cand = report->Get(local_cand_id);
                if (local_cand && local_cand->type() == webrtc::RTCLocalIceCandidateStats::kType) {
                    const auto& c = static_cast<const webrtc::RTCLocalIceCandidateStats&>(*local_cand);
                    SPDLOG_INFO("  |--> LOCAL  Endpoint:       {}:{} ({}, {})", 
                                c.ip.has_value() ? *c.ip : "N/A", 
                                c.port.has_value() ? *c.port : 0,
                                c.protocol.has_value() ? *c.protocol : "N/A",
                                c.candidate_type.has_value() ? *c.candidate_type : "N/A");
                }

                // 4. Resolve Remote Candidate Stats Object
                const auto* remote_cand = report->Get(remote_cand_id);
                if (remote_cand && remote_cand->type() == webrtc::RTCRemoteIceCandidateStats::kType) {
                    const auto& c = static_cast<const webrtc::RTCRemoteIceCandidateStats&>(*remote_cand);
                    SPDLOG_INFO("  |--> REMOTE Endpoint:       {}:{} ({}, {})", 
                                c.ip.has_value() ? *c.ip : "N/A", 
                                c.port.has_value() ? *c.port : 0,
                                c.protocol.has_value() ? *c.protocol : "N/A",
                                c.candidate_type.has_value() ? *c.candidate_type : "N/A");
                }
                SPDLOG_INFO("=================================================");
                break;
            }
        }
    }
}