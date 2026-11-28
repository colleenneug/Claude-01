#pragma once
// =============================================================================
//  playground/ecs/World.h — entities, their components, and queries.
//
//    ecs::World world;
//    ecs::Entity ship = world.create();
//    world.add<Transform>(ship, Transform{.position = {0, 0}});
//    world.add<Velocity>(ship, Velocity{.linear = {40, 0}});
//
//    world.each<Transform, Velocity>([dt](ecs::Entity, Transform& t, Velocity& v) {
//      t.position += v.linear * dt;
//    });
//
//  QUERIES walk the SMALLEST of the requested pools and look the entity up
//  in the others, so each<PlayerTag, Transform> touches one entity, not every
//  Transform. A single-component query walks the packed array directly.
//
//  CHANGING STRUCTURE DURING A QUERY
//    destroy() is always DEFERRED: the entity stays alive, with its
//    components, until flush(). Call flush() once per frame after the
//    systems, so a system can destroy what it is iterating over.
//    add() and remove() are NOT allowed during each(): they can reorder or
//    reallocate the array being walked. Debug builds assert on it.
//    create() is allowed at any time (it touches no component array).
//
//  Single-threaded: one thread owns a World.
// =============================================================================

#include "playground/core/Assert.h"
#include "playground/core/Types.h"
#include "playground/ecs/ComponentPool.h"
#include "playground/ecs/Entity.h"

#include <array>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace playground::ecs {

namespace detail {

// A small number per component type, used to index the World's pool list:
// one array lookup instead of a hash map per add/get/query. Assigned on
// first use, so the numbers differ between runs: never save them to disk.
u32 nextComponentTypeId() noexcept;

template <Component T>
u32 componentTypeId() noexcept {
  static const u32 id = nextComponentTypeId();
  return id;
}

template <class... Ts>
inline constexpr bool kAllDistinct = true;
template <class T, class... Rest>
inline constexpr bool kAllDistinct<T, Rest...> = (!std::is_same_v<T, Rest> && ...) && kAllDistinct<Rest...>;

}  // namespace detail

class World {
 public:
  World() = default;
  World(const World&) = delete;  // copying every pool by accident would be slow and surprising
  World& operator=(const World&) = delete;
  World(World&&) noexcept = default;
  World& operator=(World&&) noexcept = default;

  // ---------------------------------------------------------------- entities

  [[nodiscard]] Entity create();

  // Deferred: takes effect at flush(). Safe inside each(). Destroying an
  // entity that is already dead or already queued does nothing.
  void destroy(Entity e);

  // Applies queued destroys: removes every component, frees the slot, and
  // bumps its generation so old handles stop matching.
  void flush();

  [[nodiscard]] bool alive(Entity e) const noexcept {
    return e.index < slots_.size() && slots_[e.index].generation == e.generation && slots_[e.index].inUse;
  }

  [[nodiscard]] usize entityCount() const noexcept { return liveCount_; }
  // Every live entity, in index order (editors, saving a scene).
  [[nodiscard]] std::vector<Entity> entities() const;
  // Destroys every entity at once (not deferred like destroy()); handles to
  // them all go stale. Not during each().
  void clear();
  [[nodiscard]] usize pendingDestroyCount() const noexcept { return pendingDestroy_.size(); }

  // -------------------------------------------------------------- components

  // Adds (or replaces) a T on a live entity. The reference is valid until
  // the next add or remove of a T.
  template <Component T, class... Args>
  T& add(Entity e, Args&&... args) {
    PG_ASSERT(alive(e), "add() on a dead or null entity");
    PG_ASSERT(iterating_ == 0, "add() during each(): it can reallocate the array being walked");
    return pool<T>().emplace(e, std::forward<Args>(args)...);
  }

  template <Component T>
  void remove(Entity e) {
    PG_ASSERT(iterating_ == 0, "remove() during each(): it reorders the array being walked");
    if (ComponentPool<T>* p = findPool<T>()) p->remove(e);
  }

  // Null if the entity is dead or has no T.
  template <Component T>
  [[nodiscard]] T* tryGet(Entity e) noexcept {
    ComponentPool<T>* p = findPool<T>();
    return p ? p->tryGet(e) : nullptr;
  }

  // For when the entity must have a T: a missing one is a bug.
  template <Component T>
  [[nodiscard]] const T* tryGet(Entity e) const noexcept {
    const ComponentPool<T>* p = findPool<T>();
    return p ? p->tryGet(e) : nullptr;
  }

  template <Component T>
  [[nodiscard]] T& get(Entity e) noexcept {
    T* component = tryGet<T>(e);
    PG_VERIFY(component != nullptr, "get<T>() on an entity without that component");
    return *component;
  }

  template <Component T>
  [[nodiscard]] bool has(Entity e) const noexcept {
    const ComponentPool<T>* p = findPool<T>();
    return p != nullptr && p->contains(e);
  }

  template <Component T>
  [[nodiscard]] usize count() const noexcept {
    const ComponentPool<T>* p = findPool<T>();
    return p ? p->size() : 0;
  }

  // ------------------------------------------------------------------ queries

  // Calls fn(Entity, Ts&...) for every entity that has all of Ts.
  template <Component... Ts, class Fn>
  void each(Fn&& fn) {
    static_assert(sizeof...(Ts) >= 1, "each<>() needs at least one component type");
    static_assert(detail::kAllDistinct<Ts...>, "each<>() lists the same component type twice");

    const std::tuple<ComponentPool<Ts>*...> pools{findPool<Ts>()...};
    if (((std::get<ComponentPool<Ts>*>(pools) == nullptr) || ...)) return;  // a type nobody has

    IterationGuard guard(iterating_);

    if constexpr (sizeof...(Ts) == 1) {
      // One type: walk the packed arrays directly. The fastest loop there is.
      auto* p = std::get<0>(pools);
      const std::span<const Entity> entities = p->entities();
      const auto components = p->components();
      for (usize i = 0; i < entities.size(); ++i) fn(entities[i], components[i]);
    } else {
      // Several: walk the smallest pool, probe the rest.
      const std::array<const PoolBase*, sizeof...(Ts)> bases{std::get<ComponentPool<Ts>*>(pools)...};
      const PoolBase* smallest = bases[0];
      for (const PoolBase* b : bases) {
        if (b->size() < smallest->size()) smallest = b;
      }
      const std::span<const Entity> entities = smallest->entities();
      for (usize i = 0; i < entities.size(); ++i) {
        const Entity e = entities[i];
        if ((std::get<ComponentPool<Ts>*>(pools)->contains(e) && ...)) {
          fn(e, std::get<ComponentPool<Ts>*>(pools)->getUnchecked(e)...);
        }
      }
    }
  }

 private:
  struct Slot {
    u32 generation = 0;
    bool inUse = false;
  };

  // Counts nested each() calls, so add/remove can refuse to run inside one.
  class IterationGuard {
   public:
    explicit IterationGuard(u32& counter) noexcept : counter_(counter) { ++counter_; }
    ~IterationGuard() { --counter_; }
    IterationGuard(const IterationGuard&) = delete;
    IterationGuard& operator=(const IterationGuard&) = delete;

   private:
    u32& counter_;
  };

  template <Component T>
  ComponentPool<T>& pool() {
    const u32 id = detail::componentTypeId<T>();
    if (id >= pools_.size()) pools_.resize(static_cast<usize>(id) + 1);
    if (!pools_[id]) pools_[id] = std::make_unique<ComponentPool<T>>();
    return static_cast<ComponentPool<T>&>(*pools_[id]);
  }

  template <Component T>
  [[nodiscard]] ComponentPool<T>* findPool() const noexcept {
    const u32 id = detail::componentTypeId<T>();
    return id < pools_.size() ? static_cast<ComponentPool<T>*>(pools_[id].get()) : nullptr;
  }

  std::vector<Slot> slots_;            // per entity index
  std::vector<u32> freeIndices_;       // slots ready for reuse
  std::vector<Entity> pendingDestroy_;
  std::vector<std::unique_ptr<PoolBase>> pools_;  // indexed by component type id
  usize liveCount_ = 0;
  u32 iterating_ = 0;
};

}  // namespace playground::ecs
