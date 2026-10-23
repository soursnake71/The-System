#include "Sky.h"

namespace sky
{
static constexpr double kmPerNm = 1.852, ftPerM = 3.28084, ktPerMs = 1.94384, pi = juce::MathConstants<double>::pi;
static double kmLon (double lat) { return 111.32 * std::cos (lat * pi / 180.0); }
static constexpr double kmLat = 110.574;
static double wrap (double d) { return std::fmod (std::fmod (d + 540.0, 360.0) + 360.0, 360.0) - 180.0; }
static juce::int64 nowMs() { return juce::Time::currentTimeMillis(); }

juce::StringArray sourceNames()
{
    return { "adsb.lol (live, every 2 s)", "airplanes.live (live, every 3 s)", "adsb.fi (live, every 3 s)",
             "OpenSky Network (live, every 10 s)", "My own receiver (readsb / dump1090)", "Demo sky (simulated)" };
}

juce::uint32 hashOf (const juce::String& s)
{
    juce::uint32 h = 2166136261u;
    for (auto c : s) { h ^= (juce::uint32) c; h *= 16777619u; }
    return h;
}

/* ---------------- parsing ---------------- */
static double num (const juce::var& v, double def = 0)
{
    if (v.isDouble() || v.isInt() || v.isInt64()) return (double) v;
    if (v.isString() && v.toString().containsAnyOf ("0123456789")) return v.toString().getDoubleValue();
    return def;
}
static bool has (const juce::var& o, const char* k) { auto* d = o.getDynamicObject(); return d != nullptr && d->hasProperty (k) && ! d->getProperty (k).isVoid(); }

std::vector<Report> parseReadsb (const juce::var& j)
{
    std::vector<Report> out;
    const auto now = has (j, "now") ? (juce::int64) (num (j["now"]) * 1000.0) : nowMs();
    auto* list = has (j, "ac") ? j["ac"].getArray() : j["aircraft"].getArray();
    if (list == nullptr) return out;
    for (auto& a : *list)
    {
        if (! has (a, "lat") || ! has (a, "lon")) continue;
        Report r;
        r.id = a["hex"].toString();
        r.cs = (has (a, "flight") ? a["flight"].toString() : has (a, "r") ? a["r"].toString() : r.id).trim();
        r.type = a["t"].toString();
        r.lat = num (a["lat"]); r.lon = num (a["lon"]);
        juce::var alt = has (a, "alt_baro") ? a["alt_baro"] : has (a, "altitude") ? a["altitude"] : a["alt_geom"];
        r.gnd = alt.isString() && alt.toString() == "ground";
        r.alt = r.gnd ? 0 : num (alt);
        r.gs = has (a, "gs") ? num (a["gs"]) : num (a["speed"]);
        r.trk = has (a, "track") ? num (a["track"]) : has (a, "true_heading") ? num (a["true_heading"]) : num (a["mag_heading"]);
        r.vr = has (a, "baro_rate") ? num (a["baro_rate"]) : has (a, "geom_rate") ? num (a["geom_rate"]) : num (a["vert_rate"]);
        r.hasTrkRate = has (a, "track_rate"); r.trkRate = num (a["track_rate"]);
        r.sq = a["squawk"].toString();
        const auto em = a["emergency"].toString();
        r.emg = (em.isNotEmpty() && em != "none") || r.sq == "7500" || r.sq == "7600" || r.sq == "7700";
        r.t = now - (juce::int64) (num (has (a, "seen_pos") ? a["seen_pos"] : a["seen"]) * 1000.0);
        out.push_back (std::move (r));
    }
    return out;
}

std::vector<Report> parseOpenSky (const juce::var& j)
{
    std::vector<Report> out;
    auto* list = j["states"].getArray();
    if (list == nullptr) return out;
    const double t0 = num (j["time"]);
    for (auto& s : *list)
    {
        auto* v = s.getArray();
        if (v == nullptr || v->size() < 15 || (*v)[5].isVoid() || (*v)[6].isVoid()) continue;
        Report r;
        r.id = (*v)[0].toString();
        r.cs = (*v)[1].toString().trim(); if (r.cs.isEmpty()) r.cs = r.id;
        r.lon = num ((*v)[5]); r.lat = num ((*v)[6]);
        r.gnd = (bool) (*v)[8];
        r.alt = r.gnd ? 0 : num ((*v)[7].isVoid() ? (*v)[13] : (*v)[7]) * ftPerM;
        r.gs = num ((*v)[9]) * ktPerMs; r.trk = num ((*v)[10]); r.vr = num ((*v)[11]) * ftPerM * 60.0;
        r.sq = (*v)[14].toString();
        r.emg = r.sq == "7500" || r.sq == "7600" || r.sq == "7700";
        r.t = (juce::int64) (num ((*v)[3], t0) * 1000.0);
        out.push_back (std::move (r));
    }
    return out;
}

/* ---------------- demo sky ---------------- */
static const char* airlines[] = { "BAW", "DLH", "AFR", "KLM", "UAL", "AAL", "EZY", "RYR", "VIR", "SWR", "IBE", "UAE" };
static const char* types[] = { "A320", "A20N", "B738", "B77W", "A359", "B789", "A388", "E190", "A21N", "B38M" };
enum { dOver, dDep, dArr, dHold, dHeli, dNum };

DemoSky::P DemoSky::spawn (int kind, double R)
{
    auto rnd = [this] (double a, double b) { return a + rng.nextDouble() * (b - a); };
    P p;
    p.kind = kind; p.id = "sim" + juce::String::toHexString (++n);
    p.cs = juce::String (airlines[rng.nextInt (12)]) + juce::String ((int) rnd (10, 999));
    p.type = types[rng.nextInt (10)]; p.sq = juce::String ((int) rnd (1000, 6999));
    auto edge = [R] (double deg, double& x, double& y) { const double a = deg * pi / 180; x = std::sin (a) * R * 0.98; y = std::cos (a) * R * 0.98; };
    if (kind == dOver) { const double b = rnd (0, 360); edge (b, p.x, p.y); p.hdg = std::fmod (b + 180 + rnd (-35, 35) + 360, 360); p.alt = std::round (rnd (28, 41)) * 1000; p.gs = rnd (420, 520); p.vr = 0; }
    else if (kind == dDep) { p.x = rnd (-2, 2); p.y = rnd (-2, 2); p.hdg = std::fmod ((rng.nextBool() ? 90 : 270) + rnd (-40, 40) + 360, 360); p.alt = 300; p.gs = 170; p.vr = rnd (1800, 3000); }
    else if (kind == dArr) { const double b = rnd (0, 360); edge (b, p.x, p.y); p.hdg = std::fmod (b + 180, 360); p.alt = rnd (9000, 16000); p.gs = 280; p.vr = -rnd (900, 1500); }
    else
    {
        p.orbit = true;
        if (kind == dHold) { const double a = rnd (0, 2 * pi), d = R * rnd (0.35, 0.7); p.cx = std::sin (a) * d; p.cy = std::cos (a) * d; p.r = rnd (4, 7); p.dir = rng.nextBool() ? 1 : -1; p.alt = std::round (rnd (7, 14)) * 1000; p.gs = 220; p.life = rnd (240, 600); }
        else { static const char* h[] = { "POL", "HEMS", "NEWS" }; static const char* ht[] = { "EC35", "A109", "H145" };
               p.cs = juce::String (h[rng.nextInt (3)]) + juce::String (rng.nextInt (98) + 1); p.type = ht[rng.nextInt (3)];
               p.cx = rnd (-R / 3, R / 3); p.cy = rnd (-R / 3, R / 3); p.r = rnd (1, 2); p.dir = 1; p.alt = rnd (1200, 2200); p.gs = 90; p.life = rnd (200, 400); }
        p.ang = rnd (0, 360); p.vr = 0;
        p.x = p.cx + std::sin (p.ang * pi / 180) * p.r; p.y = p.cy + std::cos (p.ang * pi / 180) * p.r;
        p.hdg = std::fmod (p.ang + 90 * p.dir + 360, 360);
    }
    return p;
}

std::vector<Report> DemoSky::step (const FeedSettings& cfg)
{
    const auto now = nowMs();
    const double dt = last ? juce::jmin (5.0, (now - last) / 1000.0) : 0, R = cfg.radius;
    last = now;
    static const int want[dNum] = { 5, 2, 3, 3, 1 };
    int have[dNum] = {};
    planes.erase (std::remove_if (planes.begin(), planes.end(), [&] (P& p) {
        if (p.life >= 0 && (p.life -= dt) < 0) return true;
        return std::hypot (p.x, p.y) > R * 1.02 || (p.kind == dArr && p.alt < 200) || (p.kind == dDep && p.alt > 24000);
    }), planes.end());
    for (auto& p : planes) have[p.kind]++;
    // a full sky straight away, then new traffic now and then
    for (int k = 0; k < dNum; ++k)
        for (int i = have[k]; i < want[k]; ++i)
            if (! started || rng.nextDouble() < 0.015) planes.push_back (spawn (k, R));
    started = true;

    const double kl = kmLon (cfg.lat);
    std::vector<Report> out;
    for (auto& p : planes)
    {
        const double km = p.gs * kmPerNm / 3600 * dt;
        p.rate = 0;
        if (p.orbit)
        {
            p.rate = p.dir * p.gs * kmPerNm / 3600 / p.r * 180 / pi;
            p.ang = std::fmod (p.ang + p.rate * dt + 360, 360);
            p.x = p.cx + std::sin (p.ang * pi / 180) * p.r; p.y = p.cy + std::cos (p.ang * pi / 180) * p.r;
            p.hdg = std::fmod (p.ang + 90 * p.dir + 360, 360);
        }
        else
        {
            p.x += std::sin (p.hdg * pi / 180) * km; p.y += std::cos (p.hdg * pi / 180) * km; p.alt += p.vr / 60 * dt;
            if (p.kind == dArr)
            {
                const double toC = std::fmod (std::atan2 (-p.x, -p.y) * 180 / pi + 360, 360), d = wrap (toC - p.hdg);
                p.hdg = std::fmod (p.hdg + juce::jlimit (-3 * dt, 3 * dt, d) + 360, 360); p.gs = juce::jmax (140.0, p.gs - dt * 0.6);
            }
            if (p.kind == dDep) p.gs = juce::jmin (450.0, p.gs + dt * 1.5);
        }
        Report r;
        r.id = p.id; r.cs = p.cs; r.type = p.type; r.sq = p.sq;
        r.lat = cfg.lat + p.y / kmLat; r.lon = cfg.lon + p.x / kl; r.alt = juce::jmax (0.0, p.alt);
        r.gs = p.gs; r.trk = p.hdg; r.vr = p.vr; r.trkRate = p.rate; r.hasTrkRate = true; r.t = now;
        // orbiting planes arrive with the last few minutes of their track, so the demo doesn't wait a whole lap to spot them circling
        if (p.orbit && ! p.sent)
        {
            p.sent = true;
            const double w = p.dir * p.gs * kmPerNm / 3600 / p.r * 180 / pi;
            for (int s = 360; s > 0; s -= 5)
            {
                const double ang = std::fmod (p.ang - w * s + 3600, 360), a = ang * pi / 180;
                r.past.push_back ({ now - s * 1000, (float) (p.cx + std::sin (a) * p.r), (float) (p.cy + std::cos (a) * p.r), (float) std::fmod (ang + 90 * p.dir + 360, 360), (float) p.alt });
            }
        }
        out.push_back (std::move (r));
    }
    return out;
}

/* ---------------- tracking ---------------- */
float turnRateOf (const Plane& p)
{
    if (p.a.hasTrkRate && ! juce::exactlyEqual (p.a.trkRate, 0.0)) return (float) p.a.trkRate;
    const auto& h = p.hist;
    if (h.size() < 2) return 0;
    size_t i = h.size() - 1;
    while (i > 0 && p.a.t - h[i - 1].t < 40000) --i;
    if (i >= h.size() - 1) return 0;
    const double dt = (h.back().t - h[i].t) / 1000.0;
    return dt > 0 ? (float) (wrap (h.back().trk - h[i].trk) / dt) : 0.0f;
}

int SkyModel::classify (Plane& p)
{
    if (p.a.gnd || p.a.alt < 50) return kGround;
    // a full circle within the last 6 minutes, staying in one area -> circling
    const auto& h = p.hist;
    const auto since = p.a.t - 6 * 60000;
    double turn = 0, cx = 0, cy = 0; int n = 0;
    for (size_t i = 1; i < h.size(); ++i)
    {
        if (h[i].t < since) continue;
        if (h[i].t - h[i - 1].t < 90000) turn += wrap (h[i].trk - h[i - 1].trk);
        cx += h[i].x; cy += h[i].y; ++n;
    }
    p.turn = (float) turn;
    if (n > 3 && std::abs (turn) >= 300)
    {
        cx /= n; cy /= n; double r = 0;
        for (auto& q : h) if (q.t >= since) r = juce::jmax (r, std::hypot (q.x - cx, q.y - cy));
        if (r < 16) { p.orbitR = (float) r; return kArp; }
    }
    if (p.a.vr > 400) return kRise;
    if (p.a.vr < -400) return kFall;
    if (p.a.alt < 4000 && p.dist < 10) return kHit;
    if (p.a.alt >= 18000) return kDrone;
    return kLevel;
}

void SkyModel::clear()
{
    const juce::ScopedLock sl (lock);
    planes.clear(); heat.fill (0); lastUpdate = 0; firstBatch = true;
    arrivalFifo.reset();
}

void SkyModel::ingest (const std::vector<Report>& list, const FeedSettings& fs, bool frozen)
{
    const juce::ScopedLock sl (lock);
    settings = fs;
    const auto now = nowMs();
    const double dt = lastUpdate ? juce::jmin (15.0, (now - lastUpdate) / 1000.0) : 0, R = fs.radius, kl = kmLon (fs.lat);
    lastUpdate = now;
    std::vector<juce::String> seen;
    for (auto& a : list)
    {
        const double x = (a.lon - fs.lon) * kl, y = (a.lat - fs.lat) * kmLat, dist = std::hypot (x, y);
        if (dist > R * 1.05) continue;
        seen.push_back (a.id);
        auto it = planes.find (a.id);
        if (it == planes.end())
        {
            Plane p; p.hash = hashOf (a.id);
            it = planes.emplace (a.id, std::move (p)).first;
            if (! firstBatch && ! a.gnd && dist > R * 0.6)
            {
                int s1, n1, s2, n2;
                arrivalFifo.prepareToWrite (1, s1, n1, s2, n2);
                if (n1 > 0) { arrX[(size_t) s1] = (float) juce::jlimit (-1.0, 1.0, x / R); arrB[(size_t) s1] = (float) std::fmod (std::atan2 (x, y) * 180 / pi + 360, 360); }
                arrivalFifo.finishedWrite (n1);
            }
        }
        auto& p = it->second;
        if (! a.past.empty() && p.hist.empty()) p.hist = a.past;
        p.a = a; p.a.past.clear();
        p.x = x; p.y = y; p.dist = dist; p.seenAt = now;
        if (p.hist.empty() || a.t > p.hist.back().t) p.hist.push_back ({ a.t, (float) x, (float) y, (float) a.trk, (float) a.alt });
        while (! p.hist.empty() && p.hist.front().t < a.t - 8 * 60000) p.hist.erase (p.hist.begin());
        p.kind = classify (p);
    }
    for (auto it = planes.begin(); it != planes.end();)
        it = now - it->second.seenAt > 60000 ? planes.erase (it) : std::next (it);
    firstBatch = false;
    // heat grid: time each airborne plane spends in each cell, fading over the chosen window
    if (! frozen && dt > 0)
    {
        const float k = (float) std::exp (-dt / (fs.windowMin * 60.0));
        for (auto& h : heat) h *= k;
        for (auto& id : seen)
        {
            auto& p = planes[id];
            if (p.kind == kGround) continue;
            const int c = (int) std::floor ((p.x + R) / (2 * R) * gridCols), r = (int) std::floor ((p.y + R) / (2 * R) * gridRows);
            if (c >= 0 && c < gridCols && r >= 0 && r < gridRows) heat[(size_t) (r * gridCols + c)] += (float) dt;
        }
    }
}

Snapshot SkyModel::makeSnapshot()
{
    const juce::ScopedLock sl (lock);
    Snapshot s;
    s.heat = heat; s.radius = settings.radius;
    std::vector<const Plane*> list;
    const auto now = nowMs();
    for (auto& [id, p] : planes)
    {
        s.emergency |= p.a.emg;
        if (now - p.seenAt < 30000 && p.kind != kGround) list.push_back (&p);
    }
    std::sort (list.begin(), list.end(), [] (auto* a, auto* b) { return a->dist < b->dist; });
    for (auto* p : list)
    {
        if (s.numPlanes >= (int) s.planes.size()) break;
        s.planes[(size_t) s.numPlanes++] = { p->hash, hashOf (p->a.type.isNotEmpty() ? p->a.type : p->a.id), p->kind, (int) (p->hash % 16),
                                             (float) juce::jlimit (-1.0, 1.0, p->x / settings.radius), (float) p->dist, (float) p->a.alt, (float) p->a.vr, turnRateOf (*p), p->orbitR,
                                             (float) std::fmod (std::atan2 (p->x, p->y) * 180 / pi + 360, 360) };
    }
    return s;
}

bool SkyModel::popArrival (float& x, float& bearing)
{
    int s1, n1, s2, n2;
    arrivalFifo.prepareToRead (1, s1, n1, s2, n2);
    if (n1 > 0) { x = arrX[(size_t) s1]; bearing = arrB[(size_t) s1]; }
    arrivalFifo.finishedRead (n1);
    return n1 > 0;
}

/* ---------------- feed thread ---------------- */
void FeedThread::setSettings (const FeedSettings& s)
{
    const juce::ScopedLock sl (setLock);
    if (! (s == wanted)) { changed = true; notify(); }
    wanted = s;
}

void FeedThread::run()
{
    while (! threadShouldExit())
    {
        const auto fs = getSettings();
        if (changed.exchange (false)) { model.clear(); demo.reset(); }
        int every = 2000;
        if (fs.source == srcDemo)
        {
            model.ingest (demo.step (fs), fs, frozen);
            every = 1000;
            const juce::ScopedLock sl (model.lock);
            model.status = "DEMO - simulated sky - " + juce::String ((int) model.planes.size()) + " aircraft"; model.statusOk = true;
        }
        else
        {
            const auto nm = juce::String (juce::jmin (250, (int) std::ceil (fs.radius / kmPerNm)));
            const auto la = juce::String (fs.lat, 4), lo = juce::String (fs.lon, 4);
            juce::String url;
            switch (fs.source)
            {
                case srcAdsbLol:       url = "https://api.adsb.lol/v2/point/" + la + "/" + lo + "/" + nm; every = 2000; break;
                case srcAirplanesLive: url = "https://api.airplanes.live/v2/point/" + la + "/" + lo + "/" + nm; every = 3000; break;
                case srcAdsbFi:        url = "https://opendata.adsb.fi/api/v2/lat/" + la + "/lon/" + lo + "/dist/" + nm; every = 3000; break;
                case srcOpenSky:
                {
                    const double dl = fs.radius / kmLat, dn = fs.radius / kmLon (fs.lat);
                    url = "https://opensky-network.org/api/states/all?lamin=" + juce::String (fs.lat - dl, 4) + "&lomin=" + juce::String (fs.lon - dn, 4)
                        + "&lamax=" + juce::String (fs.lat + dl, 4) + "&lomax=" + juce::String (fs.lon + dn, 4);
                    every = 10000; break;
                }
                default: url = fs.localUrl.trim(); every = 1000; break;
            }
            int status = 0;
            const auto text = url.isEmpty() ? juce::String() : httpGet (url, status);
            const auto j = text.isEmpty() ? juce::var() : juce::JSON::parse (text);
            if (threadShouldExit()) break;
            if (changed) continue;
            if (j.isObject())
            {
                model.ingest (fs.source == srcOpenSky ? parseOpenSky (j) : parseReadsb (j), fs, frozen);
                const juce::ScopedLock sl (model.lock);
                model.status = "LIVE - " + sourceNames()[fs.source].upToFirstOccurrenceOf (" (", false, false) + " - " + juce::String ((int) model.planes.size()) + " aircraft";
                model.statusOk = true;
            }
            else
            {
                model.setStatus (url.isEmpty() ? "No receiver address set (Sky tab)"
                                 : "Feed error" + (status ? " (HTTP " + juce::String (status) + (status == 429 ? ", too many requests" : "") + ")" : juce::String (": no answer"))
                                   + ". Check the connection or pick another feed.", false);
                every = juce::jmax (every, 5000);
            }
        }
        if (onSnapshot) onSnapshot (model.makeSnapshot());
        wait (every);
    }
}

/* ---------------- network helpers ---------------- */
juce::String httpGet (const juce::String& url, int& status, int timeoutMs)
{
    status = 0;
    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (timeoutMs)
                    .withExtraHeaders ("User-Agent: SkySequencer/1.0 (audio plugin; https://github.com/soursnake71/the-system)\r\nAccept: application/json")
                    .withStatusCode (&status);
    auto in = juce::URL (url).createInputStream (opts);
    if (in == nullptr || status >= 400) return {};
    return in->readEntireStreamAsString();
}

std::vector<Place> searchPlaces (const juce::String& query, juce::String& error)
{
    std::vector<Place> out;
    int status = 0;
    const auto text = httpGet ("https://nominatim.openstreetmap.org/search?format=jsonv2&limit=8&q=" + juce::URL::addEscapeChars (query.trim(), true), status);
    const auto j = juce::JSON::parse (text);
    if (auto* a = j.getArray())
        for (auto& r : *a) out.push_back ({ r["display_name"].toString(), num (r["lat"]), num (r["lon"]) });
    else error = "Search failed" + (status ? " (HTTP " + juce::String (status) + ")" : juce::String (": no answer"));
    if (out.empty() && error.isEmpty()) error = "Nothing found for \"" + query + "\"";
    return out;
}

bool locateByIp (Place& out, juce::String& error)
{
    int status = 0;
    auto j = juce::JSON::parse (httpGet ("https://ipwho.is/", status));
    if (j.isObject() && (bool) j["success"])
    {
        out = { juce::StringArray { j["city"].toString(), j["region"].toString(), j["country"].toString() }.joinIntoString (", "), num (j["latitude"]), num (j["longitude"]) };
        return true;
    }
    j = juce::JSON::parse (httpGet ("https://get.geojs.io/v1/ip/geo.json", status));
    if (j.isObject() && has (j, "latitude"))
    {
        out = { juce::StringArray { j["city"].toString(), j["country"].toString() }.joinIntoString (", "), num (j["latitude"]), num (j["longitude"]) };
        return true;
    }
    error = "Couldn't find you from your internet connection. Search for a place or type coordinates instead.";
    return false;
}

// the first two numbers in s, with N/S/E/W letters giving the sign
static bool twoNumbers (const juce::String& s, bool hemispheres, double& lat, double& lon)
{
    std::vector<double> v;
    int i = 0;
    const int len = s.length();
    while (i < len && v.size() < 2)
    {
        const auto c = s[i];
        const bool startsNumber = juce::CharacterFunctions::isDigit (c)
            || ((c == '-' || c == '+' || c == '.') && i + 1 < len && (juce::CharacterFunctions::isDigit (s[i + 1]) || s[i + 1] == '.'));
        if (! startsNumber)
        {
            if (! hemispheres && juce::CharacterFunctions::isLetter (c) && ! v.empty()) break;   // "…,12z" after a map link's coordinates
            ++i; continue;
        }
        int j = i + 1;
        while (j < len && (juce::CharacterFunctions::isDigit (s[j]) || s[j] == '.')) ++j;
        double d = s.substring (i, j).getDoubleValue();
        int k = j;
        while (k < len && (s[k] == ' ' || s[k] == 0x00b0)) ++k;
        if (hemispheres && k < len)
        {
            const auto h = juce::CharacterFunctions::toUpperCase (s[k]);
            if (h == 'S' || h == 'W') d = -std::abs (d);
            if (h == 'N' || h == 'S' || h == 'E' || h == 'W') j = k + 1;
        }
        v.push_back (d); i = j;
    }
    if (v.size() < 2 || std::abs (v[0]) > 90 || std::abs (v[1]) > 180) return false;
    lat = v[0]; lon = v[1];
    return true;
}

bool parseCoordinates (const juce::String& textIn, double& lat, double& lon)
{
    const auto s = textIn.trim();
    if (s.containsIgnoreCase ("http") || s.contains ("@") || s.containsIgnoreCase ("geo:"))
    {
        // map links: …/@51.47,-0.45,12z  …?ll=51.47,-0.45  …?q=51.47,-0.45  geo:51.47,-0.45
        if (s.contains ("@") && twoNumbers (s.fromFirstOccurrenceOf ("@", false, false), false, lat, lon)) return true;
        for (auto key : { "ll=", "center=", "query=", "q=", "geo:" })
            if (s.containsIgnoreCase (key) && twoNumbers (s.fromFirstOccurrenceOf (key, false, true).replace ("%2C", ",", true), false, lat, lon)) return true;
        return false;
    }
    if (! s.containsOnly (juce::String::fromUTF8 ("0123456789.-+ ,;:/\t\xc2\xb0NSEWnsew")))
        return false;   // has words in it: it's a place name
    return twoNumbers (s, true, lat, lon);
}

std::vector<Place> airports()
{
    return { { "LHR London Heathrow", 51.4700, -0.4543 }, { "LGW London Gatwick", 51.1537, -0.1821 }, { "JFK New York", 40.6413, -73.7781 },
             { "EWR Newark", 40.6895, -74.1745 }, { "LAX Los Angeles", 33.9416, -118.4085 }, { "ATL Atlanta", 33.6407, -84.4277 },
             { "ORD Chicago O'Hare", 41.9742, -87.9073 }, { "DFW Dallas/Fort Worth", 32.8998, -97.0403 }, { "DEN Denver", 39.8561, -104.6737 },
             { "SFO San Francisco", 37.6213, -122.3790 }, { "SEA Seattle", 47.4502, -122.3088 }, { "MIA Miami", 25.7959, -80.2870 },
             { "BOS Boston", 42.3656, -71.0096 }, { "LAS Las Vegas", 36.0840, -115.1537 }, { "PHX Phoenix", 33.4352, -112.0101 },
             { "YYZ Toronto", 43.6777, -79.6248 }, { "AMS Amsterdam", 52.3105, 4.7683 }, { "CDG Paris", 49.0097, 2.5479 },
             { "FRA Frankfurt", 50.0379, 8.5622 }, { "MAD Madrid", 40.4983, -3.5676 }, { "IST Istanbul", 41.2753, 28.7519 },
             { "DXB Dubai", 25.2532, 55.3657 }, { "SIN Singapore", 1.3644, 103.9915 }, { "HND Tokyo Haneda", 35.5494, 139.7798 },
             { "SYD Sydney", -33.9399, 151.1753 } };
}
} // namespace sky
