#ifndef WEBRTC_STATS_H
#define WEBRTC_STATS_H

#include <spdlog/spdlog.h>
#include "api/peer_connection_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtcstats_objects.h"

class NominatedCandidatePairStatsCallback : public webrtc::RTCStatsCollectorCallback {
public:
    static NominatedCandidatePairStatsCallback* Create() {
        return new rtc::RefCountedObject<NominatedCandidatePairStatsCallback>();
    }

    void OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override;
};

#endif // WEBRTC_STATS_H