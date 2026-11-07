#include "RxState.h"

#include <algorithm>
#include <cctype>

namespace RxHighlight {

std::string NormaliseCallsign(const std::string& callsign)
{
    std::size_t start = 0;
    std::size_t end = callsign.size();
    while (start < end && std::isspace(static_cast<unsigned char>(callsign[start]))) {
        ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(callsign[end - 1]))) {
        --end;
    }
    std::string out = callsign.substr(start, end - start);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

void RxState::RxBegin(const std::string& callsign, std::int64_t frequencyHz, Clock::time_point now)
{
    const std::string cs = NormaliseCallsign(callsign);
    if (cs.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    active_[cs][frequencyHz] = now;
}

void RxState::RxEnd(const std::string& callsign, std::int64_t frequencyHz, Clock::time_point now)
{
    const std::string cs = NormaliseCallsign(callsign);
    if (cs.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_.find(cs);
    if (it != active_.end()) {
        it->second.erase(frequencyHz);
        if (it->second.empty()) {
            active_.erase(it);
        }
    }
    lastEnded_[cs] = now;
}

void RxState::ReplaceTransmitting(const std::set<std::string>& callsigns, Clock::time_point now)
{
    std::set<std::string> normalised;
    for (const auto& c : callsigns) {
        std::string cs = NormaliseCallsign(c);
        if (!cs.empty()) {
            normalised.insert(cs);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = active_.begin(); it != active_.end();) {
        if (normalised.count(it->first) == 0) {
            lastEnded_[it->first] = now;
            it = active_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& cs : normalised) {
        auto& freqs = active_[cs];
        if (freqs.empty()) {
            freqs[0] = now;
        }
    }
}

void RxState::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_.clear();
    lastEnded_.clear();
}

std::set<std::string> RxState::Snapshot(
    std::chrono::milliseconds linger, Clock::time_point now, std::chrono::seconds stuckTimeout)
{
    std::set<std::string> result;
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = active_.begin(); it != active_.end();) {
        auto& freqs = it->second;
        for (auto f = freqs.begin(); f != freqs.end();) {
            if (now - f->second > stuckTimeout) {
                f = freqs.erase(f);
            } else {
                ++f;
            }
        }
        if (freqs.empty()) {
            it = active_.erase(it); // stuck: drop without lingering
        } else {
            result.insert(it->first);
            ++it;
        }
    }

    for (auto it = lastEnded_.begin(); it != lastEnded_.end();) {
        if (now - it->second < linger) {
            result.insert(it->first);
            ++it;
        } else {
            it = lastEnded_.erase(it);
        }
    }
    return result;
}

} // namespace RxHighlight
