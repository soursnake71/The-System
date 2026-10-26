#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <array>
#include <functional>
#include <map>
#include <vector>

/* Live air traffic: feeds, tracking, behaviours and the heat grid. Mirrors sky/feed.js and the tracking half of sky/app.js. */
namespace sky
{
enum Kind { kArp, kRise, kFall, kDrone, kHit, kLevel, kGround, kNumKinds };
inline const char* kindName (int k)
{
    static const char* n[] = { "circling", "climbing", "descending", "cruising", "low & close", "level", "on the ground" };
    return n[juce::jlimit (0, kNumKinds - 1, k)];
}

constexpr int gridCols = 16, gridRows = 8, gridCells = gridCols * gridRows;

struct HistPt { juce::int64 t; float x, y, trk, alt; };

// one position report, already in common units: feet, knots, degrees, feet per minute
struct Report
{
    juce::String id, cs, type, sq;
    double lat = 0, lon = 0, alt = 0, gs = 0, trk = 0, vr = 0, trkRate = 0;
    bool hasTrkRate = false, gnd = false, emg = false;
    juce::int64 t = 0;
    std::vector<HistPt> past;   // demo only: earlier track so orbits are spotted straight away
};

struct Plane
{
    Report a;
    std::vector<HistPt> hist;
    double x = 0, y = 0, dist = 0;
    float turn = 0, orbitR = 0;
    int kind = kLevel;
    juce::uint32 hash = 0;
    juce::int64 seenAt = 0;
};

// what the audio thread needs, fixed size so it can be copied without allocating
struct MusicPlane { juce::uint32 hash, typeHash; int kind, phase; float x, dist, alt, vr, turnRate, orbitR, bearing; };
struct Snapshot
{
    std::array<MusicPlane, 64> planes {};
    int numPlanes = 0;
    std::array<float, gridCells> heat {};
    bool emergency = false;
    double radius = 45;
};

enum Source { srcAdsbLol, srcAirplanesLive, srcAdsbFi, srcOpenSky, srcLocal, srcDemo, numSources };
juce::StringArray sourceNames();

struct FeedSettings
{
    int source = srcAdsbLol;
    double lat = 51.47, lon = -0.4543, radius = 45, windowMin = 5;
    juce::String localUrl = "http://192.168.1.50:8080/data/aircraft.json";
    bool operator== (const FeedSettings& o) const { return source == o.source && juce::exactlyEqual (lat, o.lat) && juce::exactlyEqual (lon, o.lon) && juce::exactlyEqual (radius, o.radius) && localUrl == o.localUrl; }
};

juce::uint32 hashOf (const juce::String& s);

class DemoSky
{
public:
    std::vector<Report> step (const FeedSettings&);
    void reset() { planes.clear(); last = 0; started = false; }
private:
    struct P { int kind; juce::String id, cs, type, sq; double x, y, hdg, alt, gs, vr, cx = 0, cy = 0, r = 0, ang = 0, life = -1, rate = 0; int dir = 1; bool orbit = false, sent = false; };
    P spawn (int kind, double R);
    std::vector<P> planes;
    juce::int64 last = 0; int n = 0; bool started = false;
    juce::Random rng;
};

/** Holds every tracked plane. The feed thread writes, the editor and processor read copies. */
class SkyModel
{
public:
    void ingest (const std::vector<Report>& list, const FeedSettings& fs, bool frozen);
    void clear();
    void setStatus (const juce::String& s, bool ok) { const juce::ScopedLock sl (lock); status = s; statusOk = ok; }
    Snapshot makeSnapshot();

    // arrivals for the percussion lane: x position (-1..1) and bearing, read by the audio thread
    bool popArrival (float& x, float& bearing);

    juce::CriticalSection lock;            // guards everything below
    std::map<juce::String, Plane> planes;
    std::array<float, gridCells> heat {};
    juce::String status { "Starting..." };
    bool statusOk = true;
    juce::int64 lastUpdate = 0;
    FeedSettings settings;

private:
    static int classify (Plane&);
    juce::AbstractFifo arrivalFifo { 32 };
    std::array<float, 32> arrX {}, arrB {};
    bool firstBatch = true;
};

float turnRateOf (const Plane&);

/** The web requests one plugin instance has open, so closing the plugin can cancel them instead of waiting them out. */
struct Requests
{
    juce::CriticalSection lock;
    juce::Array<juce::WebInputStream*> active;
    std::atomic<bool> closing { false };
    void cancelAll() { closing = true; const juce::ScopedLock sl (lock); for (auto* s : active) s->cancel(); }
};

/** Polls the chosen feed on its own thread and feeds the model. */
class FeedThread : public juce::Thread
{
public:
    explicit FeedThread (SkyModel& m) : juce::Thread ("Sky feed"), model (m) {}
    ~FeedThread() override { if (requests) requests->cancelAll(); stopThread (8000); }
    Requests* requests = nullptr;
    void setSettings (const FeedSettings&);
    FeedSettings getSettings() { const juce::ScopedLock sl (setLock); return wanted; }
    std::atomic<bool> frozen { false };
    std::function<void (const Snapshot&)> onSnapshot;   // called on the feed thread after every update
    void run() override;
private:
    SkyModel& model;
    juce::CriticalSection setLock;
    FeedSettings wanted;
    std::atomic<bool> changed { true };
    DemoSky demo;
};

// network helpers (blocking; call them off the message thread)
juce::String httpGet (const juce::String& url, int& status, int timeoutMs = 10000, Requests* requests = nullptr);
std::vector<Report> parseReadsb (const juce::var& j);
std::vector<Report> parseOpenSky (const juce::var& j);

struct Place { juce::String name; double lat, lon; };
std::vector<Place> searchPlaces (const juce::String& query, juce::String& error, Requests* = nullptr);   // OpenStreetMap Nominatim
bool locateByIp (Place& out, juce::String& error, Requests* = nullptr);                                   // ipwho.is, then geojs.io
bool parseCoordinates (const juce::String& text, double& lat, double& lon);          // "51.47, -0.45", "51.47N 0.45W", map links
std::vector<Place> airports();
} // namespace sky
