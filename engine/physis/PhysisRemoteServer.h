// PHYSIS Stage 25: authenticated server-side reliable edit ingress.
// Actual Conn authentication is the host's responsibility, not Command.player.
// Compatible with any net::Conn (UDP, WebSocket or in-process loopback).
#pragma once
#include "PhysisRemoteProtocol.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <exception>
#include <map>
#include <string>
#include <tuple>
#include <utility>

namespace aiverse::physis::remote {

class ServerGateway {
public:
    static constexpr std::size_t kMaxEntries=256;
    struct Key {
        std::string principal;
        std::uint64_t instance=0, request=0;
        bool operator<(const Key& rhs) const {
            return std::tie(principal,instance,request) <
                   std::tie(rhs.principal,rhs.instance,rhs.request);
        }
    };
    struct Entry {
        net::Bytes originalFrame;
        Receipt receipt{};
        std::uint64_t expectedRevision=0;
        std::map<std::string,std::pair<AuthoredFieldKind,std::string>> desired;
        std::uint32_t observedTick=0;
        std::uint64_t lastTouchedMs=0;
    };

    const std::map<Key,Entry>& entries() const { return entries_; }

    // The host can install a durable journal writer BEFORE opening its admin
    // endpoint. Every new submission is journaled as intent BEFORE enqueue;
    // writer failures fail closed. Snapshot persistence for statuses is
    // best-effort AFTER enqueue (intent remains on disk if post-write fails).
    using JournalWriter=std::function<bool(const std::map<Key,Entry>&,
                                            std::string*)>;
    void setJournalWriter(JournalWriter writer) {journalWriter_=std::move(writer);}
    bool restoreJournal(std::map<Key,Entry> saved,std::string* error=nullptr) {
        if(saved.size()>kMaxEntries) {
            if(error)*error="PHYSIS journal has too many records";
            return false;
        }
        for(const auto& [key,entry]:saved) {
            Packet packet;
            if(key.principal.empty() || !key.instance || !key.request ||
               !decode(entry.originalFrame,packet) ||
               packet.kind!=Message::Submit ||
               packet.clientInstance!=key.instance ||
               packet.requestId!=key.request)
            {
                if(error)*error="PHYSIS journal record has invalid identity";
                return false;
            }
        }
        entries_=std::move(saved);
        if(error)error->clear();
        return true;
    }
    bool persistJournal(std::string* error=nullptr) const {
        if(!journalWriter_)return true; // legacy in-memory mode
        try{return journalWriter_(entries_,error);}
        catch(const std::exception& e) {
            if(error)*error=e.what();
            return false;
        }
    }


    // Must be called after each authoritative Simulation::step, independent
    // of connection liveness. Pending queue state is not gameplay authority.
    void observe(const Simulation& sim) {
        const auto* state=sim.world().resourceConst<FieldRuntimeState>();
        bool changed=false;
        for(auto& [key,entry]:entries_) {
            const auto before=entry.receipt;
            // Previously applied receipts are historical, but a rollback
            // can invalidate that history. Recheck at reconnect/poll time.
            if(entry.receipt.result==Result::Applied) {
                if(!state || state->revision<=entry.expectedRevision ||
                   sim.tick()<entry.observedTick ||
                   (state->revision==entry.expectedRevision+1 &&
                    !matches(*state,entry.desired))) {
                    entry.receipt.result=Result::Conflict;
                    entry.receipt.revision=state?state->revision:0;
                    entry.receipt.detail="PHYSIS applied edit invalidated by rollback";
                }
                if(entry.receipt.result!=before.result ||
                   entry.receipt.revision!=before.revision)changed=true;
                continue;
            }
            if(entry.receipt.result!=Result::Queued) continue;
            if(!state) {
                entry.receipt.result=Result::Rejected;
                entry.receipt.detail="PHYSIS runtime registry absent";
                changed=true;
                continue;
            }
            // A restored or restarted Simulation may no longer have the
            // pending command: never claim an applied edit on a stale snapshot.
            if(sim.tick()<entry.observedTick ||
               state->revision<entry.expectedRevision) {
                entry.receipt.result=Result::Conflict;
                entry.receipt.detail="PHYSIS simulation rolled back";
                entry.receipt.revision=state->revision;
                changed=true;
                continue;
            }
            if(sim.tick()<=entry.receipt.scheduledTick) continue;
            entry.receipt.revision=state->revision;
            if(state->revision==entry.expectedRevision+1 &&
               matches(*state,entry.desired)) {
                entry.receipt.result=Result::Applied;
                entry.receipt.detail="PHYSIS edit committed at authoritative tick";
            } else if(state->revision>entry.expectedRevision) {
                entry.receipt.result=Result::Conflict;
                entry.receipt.detail="PHYSIS revision/content conflict";
            } else {
                entry.receipt.result=Result::Rejected;
                entry.receipt.detail="PHYSIS command was not applied";
            }
            changed=true;
        }
        if(changed) {
            std::string error;
            if(!persistJournal(&error))journalHealthy_=false;
        }
    }

    bool journalHealthy() const {return journalHealthy_;}


    template<class ReadAcl,class WriteAcl>
    void poll(Simulation& simulation,net::Conn& authenticatedConn,
              const std::string& authenticatedPrincipal,
              ReadAcl&& canRead,WriteAcl&& canWrite,
              std::uint64_t nowMs) {
        authenticatedConn.update(nowMs);
        observe(simulation);
        net::Bytes wire;
        int processed=0;
        // Bound unauthenticated flood cost, even if Conn has many frames.
        while(processed++<64 && authenticatedConn.recv(wire)) {
            Packet request;
            if(!decode(wire,request)) continue;
            if(request.kind==Message::Receipt) continue;
            Packet reply;
            reply.kind=Message::Receipt;
            reply.clientInstance=request.clientInstance;
            reply.requestId=request.requestId;
            auto answer=[&](Result result,const char* why) {
                reply.receipt.result=result;
                reply.receipt.detail=why;
            };
            if(authenticatedPrincipal.empty() ||
               !canRead(authenticatedPrincipal)) {
                answer(Result::Denied,"PHYSIS remote status access denied");
            } else {
                const Key key{authenticatedPrincipal,request.clientInstance,request.requestId};
                const auto existing=entries_.find(key);
                if(request.kind==Message::Query) {
                    if(existing!=entries_.end()) {
                        existing->second.lastTouchedMs=nowMs;
                        reply.receipt=existing->second.receipt;
                    } else {
                        answer(Result::Unknown,"PHYSIS request not found; safe to retry original");
                        if(const auto* state=simulation.world().resourceConst<FieldRuntimeState>())
                            reply.receipt.revision=state->revision;
                    }
                } else if(existing!=entries_.end()) {
                    if(!canWrite(authenticatedPrincipal,request.command))
                        answer(Result::Denied,"PHYSIS editor permission revoked");
                    else if(existing->second.originalFrame!=wire)
                        answer(Result::Rejected,"PHYSIS reused request ID with different payload");
                    else {
                        existing->second.lastTouchedMs=nowMs;
                        reply.receipt=existing->second.receipt;
                    }
                } else if(!canWrite(authenticatedPrincipal,request.command)) {
                    answer(Result::Denied,"PHYSIS server editor ACL denied");
                } else if(hasPendingEdit()) {
                    // There is no per-request correlation in the Stage 23
                    // ECS field registry. Serializing admissions prevents
                    // two equal desired sets from both receiving false ACKs
                    // when only the first command actually committed.
                    answer(Result::Conflict,"PHYSIS another field edit is pending");
                } else if(!journalHealthy_) {
                    answer(Result::Rejected,"PHYSIS journal unavailable; server edits suspended");
                } else if(!makeRoom(nowMs)) {
                    answer(Result::Rejected,"PHYSIS server dedupe cache is full");
                } else {
                    FieldEditBatch edit;
                    std::string error;
                    if(!decodeFieldEditCommand(request.command,edit,&error)) {
                        answer(Result::Rejected,"PHYSIS invalid typed command payload");
                    } else {
                        Entry entry;
                        entry.originalFrame=wire;
                        entry.expectedRevision=edit.expectedRevision;
                        entry.observedTick=simulation.tick();
                        entry.lastTouchedMs=nowMs;
                        for(const auto& field:edit.desired)
                            entry.desired.emplace(field.id,std::make_pair(
                                field.kind,authoredFingerprint(field)));
                        std::uint32_t tick=0;
                        // Persist an INTENT before allowing any Simulation
                        // submission. A crash before enqueue then leaves a
                        // recoverable unresolved request, not a missing ID.
                        entry.receipt.result=Result::Unknown;
                        entry.receipt.revision=edit.expectedRevision;
                        entry.receipt.detail="PHYSIS durable submission intent";
                        entries_.emplace(key,entry);
                        std::string journalError;
                        if(!persistJournal(&journalError)) {
                            entries_.erase(key);
                            journalHealthy_=false;
                            answer(Result::Rejected,"PHYSIS journal intent write failed");
                        } else {
                        // Explicitly refuse client-controlled CommandSinks on
                        // authoritative server; this must enter the server's
                        // own Simulation command queue, not prediction.
                        if(simulation.hasCommandSink()) {
                            answer(Result::Rejected,"PHYSIS server requires authoritative Simulation");
                        } else if(!admitAuthorizedFieldEdit(simulation,
                            request.command,request.command.type,
                            authenticatedPrincipal,canWrite,tick,&error)) {
                            answer(error.find("revision")!=std::string::npos ?
                                Result::Conflict : Result::Rejected,
                                "PHYSIS command rejected at authenticated ingress");
                        } else {
                            reply.receipt.result=Result::Queued;
                            reply.receipt.scheduledTick=tick;
                            reply.receipt.revision=edit.expectedRevision;
                            reply.receipt.detail="PHYSIS command queued for authoritative tick";
                            auto saved=entries_.find(key);
                            if(saved!=entries_.end()) {
                                saved->second.receipt=reply.receipt;
                                if(!persistJournal(&journalError))
                                    journalHealthy_=false;
                            }
                        }
                        if(reply.receipt.result!=Result::Queued) {
                            // The intent exists but no command was accepted:
                            // store terminal rejection durably if possible.
                            auto saved=entries_.find(key);
                            if(saved!=entries_.end()) {
                                saved->second.receipt=reply.receipt;
                                if(!persistJournal(&journalError))
                                    journalHealthy_=false;
                            }
                        }
                        }
                    }
                }
            }
            net::Bytes response;
            if(encode(reply,response))authenticatedConn.send(response);
        }
    }

private:
    bool hasPendingEdit() const {
        return std::any_of(entries_.begin(),entries_.end(),
            [](const auto& pair){
                return pair.second.receipt.result==Result::Queued;
            });
    }
    static bool matches(
      const FieldRuntimeState& state,
      const std::map<std::string,std::pair<AuthoredFieldKind,std::string>>& desired) {
        if(state.byId.size()!=desired.size())return false;
        for(const auto& [id,value]:desired) {
            const auto it=state.byId.find(id);
            if(it==state.byId.end() || it->second.kind!=value.first ||
               it->second.fingerprint!=value.second)return false;
        }
        return true;
    }
    bool makeRoom(std::uint64_t nowMs) {
        if(entries_.size()<kMaxEntries)return true;
        // Terminal receipts remain available for a bounded reconnect window;
        // never evict in-flight requests just to accept new work.
        for(auto it=entries_.begin();it!=entries_.end();++it) {
            const auto age=nowMs>=it->second.lastTouchedMs ?
                nowMs-it->second.lastTouchedMs:0;
            if(terminal(it->second.receipt.result) && age>=600000) {
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }
    std::map<Key,Entry> entries_;
    JournalWriter journalWriter_;
    bool journalHealthy_=true;
};

} // namespace aiverse::physis::remote
