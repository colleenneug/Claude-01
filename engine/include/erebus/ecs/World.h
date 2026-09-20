#pragma once
// =============================================================================
//  erebus/ecs/World.h — entity lifetime, component pools, and queries.
//
//  DEFERRED STRUCTURAL CHANGES. create() and destroy() called from inside a
//  system that is mid-iteration would reorder the pool it is walking (see the
//  swap-and-pop note in ComponentStorage.h). Systems therefore queue
//  structural changes and World::flush() applies them at a sync point, once
//  per fixed step, after every system has run. This is also what makes the
//  eventual job-system parallelisation tractable: systems only ever read and
//  write component data during a step, never topology.
//
//  QUERIES. view<A, B>() walks the smaller pool and probes the larger. For a
//  two-pool join that is one linear walk plus one sparse lookup per element —
//  a predictable L1 hit, because the sparse array of a pool that is mostly
//  populated is itself hot.
// =============================================================================

#include "erebus/core/Types.h"
#include "erebus/ecs/ComponentStorage.h"
#include "erebus/ecs/Entity.h"

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace erebus::ecs {

class World {
 public:
  // ---------------------------------------------------------------- lifetime
  [[nodiscard]] Entity create();

  // Queued, not immediate. Safe to call from inside a system.
  void destroy(Entity e);

  // Two conditions, and both matter. The generation check catches a handle to
  // an entity whose slot has been recycled; the free-flag check catches a
  // handle to a slot that has been destroyed but not yet reused, whose
  // generation has already been bumped but which is nobody's entity yet.
  [[nodiscard]] bool alive(Entity e) const noexcept {
    const u32 i = e.index();
    if (i >= generations_.size()) return false;
    return generations_[i] == e.generation() && !free_[i];
  }

  // Applies every queued destroy. Call once per fixed step, after all systems.
  void flush();

  // ---------------------------------------------------------------- components
  template <Component T>
  ComponentPool<T>& pool() {
    const std::type_index key{typeid(T)};
    auto it = pools_.find(key);
    if (it == pools_.end()) {
      auto owned = std::make_unique<ComponentPool<T>>();
      auto* raw = owned.get();
      pools_.emplace(key, std::move(owned));
      order_.push_back(raw);
      return *raw;
    }
    return *static_cast<ComponentPool<T>*>(it->second.get());
  }

  template <Component T>
  T& add(Entity e, const T& value = T{}) { return pool<T>().add(e, value); }

  template <Component T>
  [[nodiscard]] T* tryGet(Entity e) { return pool<T>().tryGet(e); }

  template <Component T>
  [[nodiscard]] bool has(Entity e) { return pool<T>().contains(e); }

  // ---------------------------------------------------------------- queries
  // Single-component: hand the caller the packed arrays and let it walk them.
  // Nothing beats a raw loop over contiguous memory, and wrapping it in an
  // iterator abstraction only makes it harder for the optimiser to vectorise.
  template <Component T, typename Fn>
  void each(Fn&& fn) {
    auto& p = pool<T>();
    const Entity* ents = p.entities();
    T* data = p.data();
    const std::size_t n = p.size();
    for (std::size_t i = 0; i < n; ++i) fn(ents[i], data[i]);
  }

  // Two-component join. Walks A and probes B, so pass the rarer component
  // first: view<PlayerTag, Transform> is one iteration, view<Transform,
  // PlayerTag> is thousands.
  template <Component A, Component B, typename Fn>
  void each(Fn&& fn) {
    auto& pa = pool<A>();
    auto& pb = pool<B>();
    const Entity* ents = pa.entities();
    A* da = pa.data();
    const std::size_t n = pa.size();
    for (std::size_t i = 0; i < n; ++i) {
      if (B* b = pb.tryGet(ents[i])) fn(ents[i], da[i], *b);
    }
  }

  [[nodiscard]] std::size_t entityCount() const noexcept {
    return generations_.size() - freeIndices_.size();
  }

  [[nodiscard]] std::size_t pendingDestroyCount() const noexcept { return pendingDestroy_.size(); }

 private:
  // Generation per slot; index into this is Entity::index().
  std::vector<u32> generations_;
  std::vector<u32> freeIndices_;
  std::vector<Entity> pendingDestroy_;

  // Index-parallel with generations_: is this slot currently unowned? A
  // linear scan of freeIndices_ would make alive() O(free slots), and alive()
  // is called per projectile per step.
  std::vector<bool> free_;

  std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools_;
  std::vector<IComponentPool*> order_;   // stable iteration for destroy
};

}  // namespace erebus::ecs
