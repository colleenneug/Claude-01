// ============================================================
//  Multicast delegates — the engine's one event primitive.
//
//  Actors and components expose these so gameplay, the editor and the
//  script runtime can all listen without any of them knowing about the
//  others. Binding returns a handle; unbinding takes it back.
// ============================================================
#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace forge {

template <typename... Args>
class Delegate {
public:
    using Fn = std::function<void(Args...)>;
    using Handle = int;

    Handle bind(Fn fn) {
        entries_.push_back({nextId_, std::move(fn)});
        return nextId_++;
    }

    void unbind(Handle h) {
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].id != h) continue;
            // During a broadcast the vector must not shift under the
            // iteration, so clear the slot and let the sweep remove it.
            entries_[i].fn = nullptr;
            if (broadcasting_ == 0) entries_.erase(entries_.begin() + (long)i);
            return;
        }
    }

    void clear() {
        if (broadcasting_ > 0) { for (auto& e : entries_) e.fn = nullptr; return; }
        entries_.clear();
    }

    bool bound() const {
        for (const auto& e : entries_) if (e.fn) return true;
        return false;
    }
    size_t count() const {
        size_t n = 0;
        for (const auto& e : entries_) if (e.fn) ++n;
        return n;
    }

    void broadcast(Args... args) const {
        ++broadcasting_;
        // Index rather than iterate: a listener is allowed to bind more
        // listeners, and the new ones simply run on the next broadcast.
        const size_t n = entries_.size();
        for (size_t i = 0; i < n && i < entries_.size(); ++i)
            if (entries_[i].fn) entries_[i].fn(args...);
        --broadcasting_;
        if (broadcasting_ == 0) sweep();
    }

private:
    struct Entry { Handle id; Fn fn; };

    void sweep() const {
        for (size_t i = entries_.size(); i-- > 0;)
            if (!entries_[i].fn) entries_.erase(entries_.begin() + (long)i);
    }

    mutable std::vector<Entry> entries_;
    mutable int broadcasting_ = 0;
    Handle nextId_ = 1;
};

} // namespace forge
