// Unit tests for the platform-independent parts (message parsing, RX state).
// Build:  g++ -std=c++17 -I src test/test_main.cpp src/RxState.cpp src/VoiceMessage.cpp src/WebSocketCodec.cpp -o rxh_tests && ./rxh_tests
#include <cstdio>
#include <cstdlib>

#include "RxState.h"
#include "VoiceMessage.h"
#include "WebSocketCodec.h"

using namespace RxHighlight;
using namespace std::chrono;

static int failures = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static void TestParse()
{
    auto e = ParseVoiceMessage(R"({"type":"kRxBegin","value":{"callsign":"BAW123","pFrequencyHz":118500000}})");
    CHECK(e.type == VoiceEventType::RxBegin);
    CHECK(e.callsign == "BAW123");
    CHECK(e.frequencyHz == 118500000);

    e = ParseVoiceMessage("{\n  \"type\" : \"kRxEnd\",\n  \"value\": {\"activeTransmitters\": [\"EZY1\"], "
                          "\"callsign\": \"BAW123\", \"pFrequencyHz\": 1.185e8 }\n}");
    CHECK(e.type == VoiceEventType::RxEnd);
    CHECK(e.callsign == "BAW123");
    CHECK(e.frequencyHz == 118500000);

    e = ParseVoiceMessage(R"({"type":"kVoiceConnectedState","value":{"connected":false}})");
    CHECK(e.type == VoiceEventType::VoiceDisconnected);
    e = ParseVoiceMessage(R"({"type":"kVoiceConnectedState","value":{"connected":true}})");
    CHECK(e.type == VoiceEventType::VoiceConnected);

    CHECK(ParseVoiceMessage(R"({"type":"kTxBegin","value":{}})").type == VoiceEventType::Unknown);
    CHECK(ParseVoiceMessage(R"({"type":"kRxBegin","value":{}})").type == VoiceEventType::Unknown);
    CHECK(ParseVoiceMessage("garbage").type == VoiceEventType::Unknown);
    CHECK(ParseVoiceMessage("").type == VoiceEventType::Unknown);
    CHECK(ParseVoiceMessage(R"({"type":"kRxBegin","value":{"callsign":"unterminated)").type == VoiceEventType::Unknown);

    auto list = ParseTransmittingList("EJU1652,LSGG_TWR");
    CHECK(list.size() == 2 && list.count("EJU1652") && list.count("LSGG_TWR"));
    CHECK(ParseTransmittingList("").empty());
    CHECK(ParseTransmittingList(" AFR007 \r\n").count("AFR007") == 1);
}

static void TestState()
{
    RxState s;
    const auto t0 = Clock::now();
    const milliseconds linger(1500);

    CHECK(s.Snapshot(linger, t0).empty());

    s.RxBegin("baw123 ", 118500000, t0);
    CHECK(s.Snapshot(linger, t0).count("BAW123") == 1);

    // Same pilot heard on two frequencies (e.g. cross-coupled); ending one keeps it lit.
    s.RxBegin("BAW123", 121200000, t0);
    s.RxEnd("BAW123", 118500000, t0 + seconds(1));
    CHECK(s.Snapshot(linger, t0 + seconds(5)).count("BAW123") == 1);

    // Ending the last frequency: still lit during linger, then not.
    s.RxEnd("BAW123", 121200000, t0 + seconds(6));
    CHECK(s.Snapshot(linger, t0 + milliseconds(7000)).count("BAW123") == 1);
    CHECK(s.Snapshot(linger, t0 + milliseconds(7600)).empty());

    // A very short transmission between two timer ticks is still shown thanks to linger.
    s.RxBegin("EZY45", 1, t0 + seconds(10));
    s.RxEnd("EZY45", 1, t0 + milliseconds(10300));
    CHECK(s.Snapshot(linger, t0 + seconds(11)).count("EZY45") == 1);

    // Missed RxEnd: dropped after the stuck timeout.
    s.RxBegin("RYR1", 1, t0);
    CHECK(s.Snapshot(milliseconds(0), t0 + seconds(119)).count("RYR1") == 1);
    CHECK(s.Snapshot(milliseconds(0), t0 + seconds(121)).count("RYR1") == 0);

    // HTTP polling replacement.
    RxState p;
    p.ReplaceTransmitting({"AFR007", "EGLL_N_TWR"}, t0);
    CHECK(p.Snapshot(milliseconds(0), t0).size() == 2);
    p.ReplaceTransmitting({"EGLL_N_TWR"}, t0 + seconds(1));
    CHECK(p.Snapshot(milliseconds(500), t0 + milliseconds(1200)).count("AFR007") == 1);
    CHECK(p.Snapshot(milliseconds(500), t0 + seconds(2)).count("AFR007") == 0);
    CHECK(p.Snapshot(milliseconds(500), t0 + seconds(2)).count("EGLL_N_TWR") == 1);

    p.Clear();
    CHECK(p.Snapshot(seconds(10), t0 + seconds(2)).empty());
}

static std::string ServerFrame(unsigned char first, const std::string& payload)
{
    std::string f(1, static_cast<char>(first));
    if (payload.size() < 126) {
        f.push_back(static_cast<char>(payload.size()));
    } else {
        f.push_back(static_cast<char>(126));
        f.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
        f.push_back(static_cast<char>(payload.size() & 0xFF));
    }
    return f + payload;
}

static void TestWebSocketCodec()
{
    WsReader r;
    WsFrame f;
    bool err = false;

    // Byte-by-byte delivery of a small text frame.
    const std::string small = ServerFrame(0x81, "hello");
    for (std::size_t i = 0; i < small.size(); ++i) {
        CHECK(!r.Next(f, err));
        r.Feed(&small[i], 1);
    }
    CHECK(r.Next(f, err) && f.fin && f.opcode == WsOpcode::Text && f.payload == "hello");
    CHECK(!r.Next(f, err));

    // 16-bit length frame followed by a ping in the same chunk.
    const std::string big(300, 'x');
    const std::string two = ServerFrame(0x81, big) + ServerFrame(0x89, "p");
    r.Feed(two.data(), two.size());
    CHECK(r.Next(f, err) && f.payload == big);
    CHECK(r.Next(f, err) && f.opcode == WsOpcode::Ping && f.payload == "p");

    // Fragmented message.
    const std::string frags = ServerFrame(0x01, "{\"type\":") + ServerFrame(0x80, "\"x\"}");
    r.Feed(frags.data(), frags.size());
    CHECK(r.Next(f, err) && !f.fin && f.opcode == WsOpcode::Text);
    CHECK(r.Next(f, err) && f.fin && f.opcode == WsOpcode::Continuation);

    // Client frames are masked and round-trip through the reader.
    const std::string enc = EncodeClientFrame(WsOpcode::Pong, "abc", 0x12345678u);
    CHECK((static_cast<unsigned char>(enc[1]) & 0x80) != 0);
    WsReader r2;
    r2.Feed(enc.data(), enc.size());
    CHECK(r2.Next(f, err) && f.opcode == WsOpcode::Pong && f.payload == "abc");

    // Absurd 64-bit length is rejected.
    WsReader r3;
    const std::string huge("\x81\x7f\x7f\xff\xff\xff\xff\xff\xff\xff", 10);
    r3.Feed(huge.data(), huge.size());
    CHECK(!r3.Next(f, err) && err);

    // HTTP responses.
    int status = 0;
    std::string body;
    CHECK(ParseHttpResponse("HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nAFR007", status, body));
    CHECK(status == 200 && body == "AFR007");
    CHECK(!ParseHttpResponse("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nAFR", status, body));
    CHECK(ParseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nEZY\r\n2\r\n45\r\n0\r\n\r\n", status, body));
    CHECK(body == "EZY45");
    CHECK(ParseHttpResponse("HTTP/1.1 404 Not Found\r\n\r\n", status, body) && status == 404 && body.empty());
    CHECK(!ParseHttpResponse("garbage", status, body));
}

int main()
{
    TestParse();
    TestState();
    TestWebSocketCodec();
    if (failures == 0) {
        std::printf("All tests passed\n");
        return EXIT_SUCCESS;
    }
    std::printf("%d failure(s)\n", failures);
    return EXIT_FAILURE;
}
