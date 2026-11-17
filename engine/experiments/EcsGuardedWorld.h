// Opt-in runtime access validation for experimental ECS wave scheduling.
// This facade checks calls routed through GuardedWorld; it does NOT prevent
// raw World access, retained pointers/references, or external global mutations.
#pragma once
#include "EcsWaveExecutor.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace aiverse::ecs_experiment {

class AccessViolation : public std::logic_error {
public:
    explicit AccessViolation(const char* message):std::logic_error(message){}
};

class AccessScope {
    const SystemAccess* previous_;
    static inline thread_local const SystemAccess* current_ = nullptr;
public:
    explicit AccessScope(const SystemAccess& access):previous_(current_) {current_=&access;}
    AccessScope(const AccessScope&)=delete;
    AccessScope& operator=(const AccessScope&)=delete;
    ~AccessScope(){current_=previous_;}
    static const SystemAccess* current(){return current_;}
};

inline bool containsAccess(const std::vector<AccessId>& ids, AccessId id) {
    return std::find(ids.begin(),ids.end(),id)!=ids.end();
}
inline void requireComponent(AccessId id,bool write) {
    const auto* scope=AccessScope::current();
    if(!scope)throw AccessViolation("component access outside system scope");
    if(write ? !containsAccess(scope->writes,id)
             : !containsAccess(scope->reads,id) && !containsAccess(scope->writes,id))
        throw AccessViolation("undeclared component access");
}
inline void requireResource(AccessId id,bool write) {
    const auto* scope=AccessScope::current();
    if(!scope)throw AccessViolation("resource access outside system scope");
    if(write ? !containsAccess(scope->resourceWrites,id)
             : !containsAccess(scope->resourceReads,id) && !containsAccess(scope->resourceWrites,id))
        throw AccessViolation("undeclared resource access");
}
inline void requireStructural() {
    const auto* scope=AccessScope::current();
    if(!scope || !scope->structural)
        throw AccessViolation("undeclared structural change");
}

template<class T> AccessId componentId() {return detail::componentTypeId<T>();}

class GuardedWorld {
    World& world_;
public:
    explicit GuardedWorld(World& world):world_(world){}
    template<class T> const T* read(Entity e) const {
        requireComponent(componentId<T>(),false);
        return const_cast<World&>(world_).tryGet<T>(e);
    }
    template<class T> T* write(Entity e) {
        requireComponent(componentId<T>(),true);
        return world_.tryGet<T>(e);
    }
    template<class T> bool has(Entity e) const {
        requireComponent(componentId<T>(),false);
        return world_.has<T>(e);
    }
    template<class T,class... Args> T& add(Entity e,Args&&... args) {
        requireStructural();
        requireComponent(componentId<T>(),true);
        return world_.add<T>(e,std::forward<Args>(args)...);
    }
    template<class T> void remove(Entity e) {
        requireStructural();
        requireComponent(componentId<T>(),true);
        world_.remove<T>(e);
    }
    Entity create() {
        requireStructural();
        return world_.create();
    }
    void destroyLater(Entity e) {
        requireStructural();
        world_.destroyLater(e);
    }
    template<class T> const T* readResource() const {
        requireResource(componentId<T>(),false);
        return const_cast<World&>(world_).resource<T>();
    }
    template<class T> T* writeResource() {
        requireResource(componentId<T>(),true);
        return world_.resource<T>();
    }
    // Only the declared access is allowed. Other types must be queried through
    // this facade. No direct World& escapes from this API.
    template<class T,class Fn> void eachRead(Fn&& fn) const {
        requireComponent(componentId<T>(),false);
        const_cast<World&>(world_).each<T>([&](Entity e,T& v){
            fn(e,static_cast<const T&>(v));
        });
    }
    template<class T,class Fn> void eachWrite(Fn&& fn) {
        requireComponent(componentId<T>(),true);
        world_.each<T>(std::forward<Fn>(fn));
    }
};

// Execute callbacks with thread-local declarations installed on the exact worker
// executing each system. Scope restoration also happens on exceptions.
inline void runChecked(WaveExecutor& executor,const DependencyPlan& plan,
    const std::vector<std::function<void()>>& jobs,
    const std::function<void(size_t)>& barrier={}) {
    if(jobs.size()!=plan.systems().size())
        throw std::invalid_argument("system/function count mismatch");
    std::vector<WaveExecutor::SystemFn> wrapped;
    wrapped.reserve(jobs.size());
    for(size_t i=0;i<jobs.size();++i) {
        wrapped.push_back([&,i] {
            AccessScope scope(plan.systems()[i]);
            jobs[i]();
        });
    }
    executor.run(plan,wrapped,barrier);
}
} // namespace aiverse::ecs_experiment
