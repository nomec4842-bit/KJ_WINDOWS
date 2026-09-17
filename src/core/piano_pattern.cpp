#include "core/piano_pattern.h"
#include "core/track_type_sample.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <iomanip>
#include <limits>
#include <locale>

namespace piano {
namespace {
struct Entry { Document doc; std::vector<Document> past, future; std::shared_ptr<Runtime> runtime; };
std::mutex guard;
std::map<int, Entry> entries;
uint32_t hash(uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; return static_cast<uint32_t>(x); }
int constrain(int pitch, const Settings& s) {
    pitch = std::clamp(pitch, 0, 127);
    if (!s.scale) return pitch;
    const int major[] = {0,2,4,5,7,9,11}, minor[] = {0,2,3,5,7,8,10};
    auto valid = [&](int n) {
        int pc = (n - s.key + 120) % 12;
        const int* scale = s.scale == 1 ? major : minor;
        return std::find(scale, scale + 7, pc) != scale + 7;
    };
    for (int d = 0; d < 12; ++d) {
        if (pitch - d >= 0 && valid(pitch-d)) return pitch-d;
        if (pitch + d <= 127 && valid(pitch+d)) return pitch+d;
    }
    return pitch;
}
Document import(int track) {
    Document d;
    d.pattern.length = std::max(1, trackGetStepCount(track)) * ticksPerStep;
    for (int step = 0; step < trackGetStepCount(track); ++step) {
        if (!trackGetStepState(track, step)) continue;
        for (const auto& n : trackGetStepNoteInfo(track, step)) {
            auto it = std::find_if(d.pattern.notes.rbegin(), d.pattern.notes.rend(), [&](const Note& p) {
                return p.pitch == n.midiNote && p.start + p.length == step * ticksPerStep;
            });
            if (n.sustain && it != d.pattern.notes.rend()) it->length += ticksPerStep;
            else d.pattern.notes.push_back({step*ticksPerStep, ticksPerStep, n.midiNote, n.velocity, 1.0f, false});
        }
    }
    d.original = d.pattern;
    return d;
}
Entry& entry(int track) {
    auto found = entries.find(track);
    if (found == entries.end()) found = entries.emplace(track, Entry{import(track)}).first;
    return found->second;
}
void validate(Document& d) {
    normalize(d.pattern); normalize(d.original);
    d.settings.every = std::clamp(d.settings.every, 1, 64);
    d.settings.key = std::clamp(d.settings.key, 0, 11);
    d.settings.scale = std::clamp(d.settings.scale, 0, 2);
    d.settings.hits = std::clamp(d.settings.hits, 1, 1024);
    d.settings.amount = std::isfinite(d.settings.amount) ? std::clamp(d.settings.amount, 0.0f, 1.0f) : 0.5f;
    if (static_cast<int>(d.settings.algorithm) < 0 || static_cast<int>(d.settings.algorithm) > 5) d.settings.algorithm = Algorithm::Scramble;
}
void history(Entry& e) { e.past.push_back(e.doc); if (e.past.size() > 64) e.past.erase(e.past.begin()); e.future.clear(); }
}
void normalize(Pattern& p) {
    p.length = std::clamp(p.length, 24, 1024 * ticksPerStep);
    if (p.notes.size() > 2048) p.notes.resize(2048);
    for (auto& n : p.notes) {
        n.start = std::clamp(n.start, 0, p.length-1);
        n.length = std::clamp(n.length, 1, p.length-n.start);
        n.pitch = std::max(n.pitch, 0);
        n.velocity = std::isfinite(n.velocity) ? std::clamp(n.velocity, 0.0f, 1.0f) : 0.8f;
        n.probability = std::isfinite(n.probability) ? std::clamp(n.probability, 0.0f, 1.0f) : 1.0f;
    }
    std::stable_sort(p.notes.begin(), p.notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
}
Pattern generate(const Pattern& source, const Settings& s, uint64_t iteration, bool drums, int bankSize) {
    Pattern p = source;
    std::mt19937 random(hash(uint64_t(s.seed) + iteration * 0x9e3779b97f4a7c15ULL));
    auto unit = [&] { return std::generate_canonical<float, 24>(random); };
    auto pitch = [&](int n) { return drums ? (bankSize > 0 ? kSampleDrumNoteBase + static_cast<int>(random() % bankSize) : kSampleDrumNoteBase) : constrain(n, s); };
    std::vector<int> pitches;
    for (const auto& n : p.notes) if (!n.locked) pitches.push_back(n.pitch);
    std::shuffle(pitches.begin(), pitches.end(), random);
    size_t nextPitch = 0;
    if (s.algorithm == Algorithm::Euclidean || s.algorithm == Algorithm::RandomWalk) {
        p.notes.erase(std::remove_if(p.notes.begin(), p.notes.end(), [](const Note& n) { return !n.locked; }), p.notes.end());
        int steps = std::max(1, p.length/ticksPerStep), hits = std::min(s.hits, steps), walk = 60 + s.key;
        for (int i = 0; i < steps; ++i) {
            bool hit = s.algorithm == Algorithm::Euclidean ? ((i * hits) % steps < hits) : unit() < s.amount;
            if (!hit || (drums && bankSize == 0)) continue;
            walk = std::clamp(walk + (static_cast<int>(random()%5)-2), 48, 83);
            Note n{i*ticksPerStep, ticksPerStep, pitch(walk), 0.8f, 1.0f, false};
            bool blocked = std::any_of(p.notes.begin(), p.notes.end(), [&](const Note& old) { return old.locked && old.pitch == n.pitch && old.start < n.start+n.length && n.start < old.start+old.length; });
            if (!blocked) p.notes.push_back(n);
        }
    } else for (auto& n : p.notes) {
        if (n.locked) continue;
        if (s.algorithm == Algorithm::Scramble) n.pitch = drums ? pitches[nextPitch++] : constrain(pitches[nextPitch++], s);
        else if (s.algorithm == Algorithm::Probability) n.probability = s.amount;
        else if (s.algorithm == Algorithm::Chaos || unit() < s.amount) {
            int span = std::max(1, static_cast<int>(s.amount*24));
            n.pitch = pitch(n.pitch + static_cast<int>(random()%(span*2+1))-span);
            int shift = static_cast<int>((unit()*2-1)*s.amount*ticksPerBeat);
            n.start = std::clamp(n.start+shift, 0, p.length-1);
            n.length = std::max(1, static_cast<int>(n.length * (1.0f + (unit()*2-1)*s.amount)));
            n.velocity = std::clamp(n.velocity + (unit()*2-1)*s.amount, 0.05f, 1.0f);
        }
    }
    normalize(p);
    // Locked notes reserve their pitch/time region, even when other notes move.
    p.notes.erase(std::remove_if(p.notes.begin(), p.notes.end(), [&](const Note& n) {
        if (n.locked) return false;
        return std::any_of(source.notes.begin(), source.notes.end(), [&](const Note& lock) {
            return lock.locked && lock.pitch == n.pitch && lock.start < n.start+n.length && n.start < lock.start+lock.length;
        });
    }), p.notes.end());
    return p;
}
Document document(int track) { std::lock_guard<std::mutex> lock(guard); return entry(track).doc; }
void commit(int track, Document d) {
    validate(d);
    std::lock_guard<std::mutex> lock(guard); auto& e = entry(track); history(e); if(d.pattern.length != e.doc.pattern.length) trackSetStepCount(track, (d.pattern.length+ticksPerStep-1)/ticksPerStep); e.doc = std::move(d); e.runtime.reset();
}
bool undo(int track, bool redo) {
    std::lock_guard<std::mutex> lock(guard); auto& e = entry(track);
    auto& from = redo ? e.future : e.past; auto& to = redo ? e.past : e.future;
    if (from.empty()) return false;
    to.push_back(e.doc); if(e.doc.pattern.length != from.back().pattern.length) trackSetStepCount(track, (from.back().pattern.length+ticksPerStep-1)/ticksPerStep); e.doc = from.back(); from.pop_back(); e.runtime.reset(); return true;
}
void reset() { std::lock_guard<std::mutex> lock(guard); entries.clear(); }
void generateOnce(int track) {
    auto d = document(track); if (!d.hasOriginal) { d.original = d.pattern; d.hasOriginal = true; }
    auto bank = sampleGetBankBuffers();
    d.pattern = generate(d.settings.evolve ? d.pattern : d.original, d.settings, 0, trackGetType(track)==TrackType::Sample && trackGetSampleDrumMode(track), bank ? static_cast<int>(bank->size()) : 0);
    commit(track, d);
}
Pattern displayed(int track) {
    std::lock_guard<std::mutex> lock(guard); auto& e = entry(track);
    if (e.runtime) { auto p = std::atomic_load(&e.runtime->displayed); if (p) return *p; }
    return e.doc.pattern;
}
void freeze(int track) { auto p = displayed(track); auto d = document(track); d.pattern = p; d.settings.continuous = false; commit(track, d); }
void restoreOriginal(int track) { auto d = document(track); if (d.hasOriginal) { d.pattern = d.original; d.settings.continuous = false; commit(track,d); } }
std::shared_ptr<Runtime> prepare(int track) {
    std::lock_guard<std::mutex> lock(guard);
    auto it = entries.find(track); if (it == entries.end()) return {};
    auto& e = it->second;
    if (!e.runtime) {
        e.runtime = std::make_shared<Runtime>(); e.runtime->settings = e.doc.settings;
        auto first = std::make_shared<Prepared>(); first->pattern = e.doc.pattern;
        e.runtime->current = first;
    }
    auto r = e.runtime;
    if (r->settings.continuous) {
        uint64_t target = r->cycle.load()/r->settings.every + 1;
        auto next = std::atomic_load(&r->next);
        if (!next || next->generation != target) {
            auto current = std::atomic_load(&r->current);
            auto bank = sampleGetBankBuffers();
            auto value = std::make_shared<Prepared>(); value->generation = target;
            // Each run starts from the current editable sequence. The original
            // remains a recovery snapshot, not a permanent generation source.
            value->pattern = generate(r->settings.evolve ? current->pattern : e.doc.pattern, r->settings, target,
                trackGetType(track)==TrackType::Sample && trackGetSampleDrumMode(track), bank ? static_cast<int>(bank->size()) : 0);
            std::atomic_store(&r->next, std::shared_ptr<const Prepared>(value));
        }
    }
    return r;
}
bool render(Playback& s, const std::shared_ptr<Runtime>& r, int tick, bool restart,
            std::vector<StepNoteInfo>& on, std::vector<int>& present, bool& gate) {
    if (!r) return false;
    bool edited = s.runtime && s.runtime != r && !restart;
    if (s.runtime != r || restart) {
        s.runtime = r; s.plan = std::atomic_load(&r->current);
        // Live edits replace the plan without restarting notes already held.
        if (!edited) s.lastTick = -1;
        s.cycle = 0; r->cycle.store(0);
    }
    if (s.lastTick >= 0 && tick < s.lastTick) {
        ++s.cycle;
        uint64_t generation = s.cycle / r->settings.every;
        auto next = std::atomic_load(&r->next);
        if (r->settings.continuous && next && next->generation == generation) { s.plan = next; std::atomic_store(&r->current, next); }
        r->cycle.store(s.cycle);
    }
    bool changed = edited || tick != s.lastTick;
    if (!changed) { on.clear(); present.clear(); gate = s.gate; return false; }
    std::vector<int> heldBeforeEdit;
    if (edited) for (const auto& n : s.notes) heldBeforeEdit.push_back(n.midiNote);
    s.notes.clear();
    on.clear(); present.clear(); gate = false;
    for (size_t i = 0; i < s.plan->pattern.notes.size(); ++i) {
        const auto& n = s.plan->pattern.notes[i];
        if (n.velocity <= 0.0f) continue;
        if (tick < n.start || tick >= n.start+n.length) continue;
        double draw = hash(uint64_t(r->settings.seed) + s.cycle*104729 + i*7919) / 4294967296.0;
        if (!n.locked && draw >= n.probability) continue;
        gate = true;
        bool starts = s.lastTick < n.start || tick < s.lastTick;
        if (edited && tick >= s.lastTick)
            starts = std::find(heldBeforeEdit.begin(), heldBeforeEdit.end(), n.pitch) == heldBeforeEdit.end();
        // The voices are keyed by pitch. Overlapping notes must not duplicate
        // the same voice or release it before the final held region ends.
        auto existing = std::find_if(s.notes.begin(), s.notes.end(), [&](const StepNoteInfo& note) { return note.midiNote == n.pitch; });
        if (existing == s.notes.end()) s.notes.push_back({n.pitch,n.velocity,!starts});
        else if (starts) { existing->sustain = false; existing->velocity = n.velocity; }
    }
    if (changed) {
        for (const auto& n : s.notes) { present.push_back(n.midiNote); if (!n.sustain) on.push_back(n); }
        std::sort(present.begin(), present.end()); present.erase(std::unique(present.begin(), present.end()), present.end());
        std::atomic_store(&r->displayed, std::shared_ptr<const Pattern>(s.plan, &s.plan->pattern));
    }
    s.gate = gate; s.lastTick = tick; return changed;
}
std::string serialize(int track) {
    std::lock_guard<std::mutex> lock(guard); auto it = entries.find(track); if (it == entries.end()) return {};
    auto d = it->second.doc;
    if (it->second.runtime) {
        auto audible = std::atomic_load(&it->second.runtime->displayed);
        if (audible) d.pattern = *audible;
    }
    auto& s = d.settings; std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << 1 << ' ' << static_cast<int>(s.algorithm) << ' ' << s.continuous << ' ' << s.evolve << ' ' << s.every << ' ' << s.key << ' ' << s.scale << ' ' << s.hits << ' ' << s.amount << ' ' << s.seed << ' ' << d.hasOriginal << ' ';
    for (const auto* p : {&d.pattern, &d.original}) { out << p->length << ' ' << p->notes.size() << ' '; for (auto n : p->notes) out << n.start << ' ' << n.length << ' ' << n.pitch << ' ' << n.velocity << ' ' << n.probability << ' ' << n.locked << ' '; }
    return out.str();
}
void deserialize(int track, const std::string& text) {
    if (text.empty()) return;
    std::istringstream in(text); Document d; int version, algorithm; auto& s = d.settings;
    in.imbue(std::locale::classic());
    if (!(in >> version >> algorithm >> s.continuous >> s.evolve >> s.every >> s.key >> s.scale >> s.hits >> s.amount >> s.seed >> d.hasOriginal) || version != 1) return;
    s.algorithm = static_cast<Algorithm>(algorithm);
    for (auto* p : {&d.pattern, &d.original}) { size_t count; if (!(in >> p->length >> count) || count > 2048) return;
        p->notes.resize(count); for (auto& n : p->notes) if (!(in >> n.start >> n.length >> n.pitch >> n.velocity >> n.probability >> n.locked)) return;
    }
    validate(d); std::lock_guard<std::mutex> lock(guard); entries[track] = Entry{d};
}
}
