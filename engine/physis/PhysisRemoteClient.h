// PHYSIS Stage 25: reliable remote editor request client.
// Uses existing net::Conn; connection identity/TLS credentials come from host.
// One outstanding edit at a time to preserve revision order and avoid races.
#pragma once
#include "PhysisRemoteProtocol.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace aiverse::physis::remote {

enum class ClientStatus : std::uint8_t {
    Idle, Offline, AwaitingReceipt, Queued, Applied,
    Rejected, Conflict, Denied, Uncertain
};

class RemoteClient {
public:
    // Host must supply a nonzero randomly generated client instance ID that
    // survives reconnect. Do NOT derive this token from std::hash or a name.
    explicit RemoteClient(std::uint64_t clientInstance)
        : instance_(clientInstance) {}
    std::uint64_t instance() const {return instance_;}
    ClientStatus status() const {return status_;}
    const Receipt& receipt() const {return receipt_;}
    std::uint64_t requestId() const {return active_?active_->requestId:0;}
    std::uint64_t nextRequestId() const {return nextId_;}
    bool waiting() const {
        return active_.has_value() && !terminal(receipt_.result) &&
               status_!=ClientStatus::Uncertain;
    }

    bool begin(const FieldEditBatch& edit,std::uint16_t hostCommandType,
               std::uint8_t authorizedClientSlot,
               std::string* error=nullptr) {
        if(!instance_ || nextId_==0 ||
           (active_ && !terminal(receipt_.result) && status_!=ClientStatus::Idle))
            return protocolError(error,"PHYSIS edit already outstanding or instance invalid");
        if(authorizedClientSlot==0 || hostCommandType==0)
            return protocolError(error,"PHYSIS remote edit needs nonzero player and command type");
        Command command;
        if(!encodeFieldEditCommand(edit,hostCommandType,command,error))return false;
        command.player=authorizedClientSlot;
        command.tick=0;command.seq=0;
        Packet packet;
        packet.kind=Message::Submit;
        packet.clientInstance=instance_;
        packet.requestId=nextId_++;
        packet.command=std::move(command);
        net::Bytes encoded;
        if(!encode(packet,encoded,error))return false;
        active_=std::move(packet);
        receipt_=Receipt{};
        status_=ClientStatus::Offline;
        sent_=false;
        query_=false;
        attempts_=0;
        nextSendMs_=0;
        if(error)error->clear();
        return true;
    }

    // Persist this exact request envelope from application-owned secure
    // storage; restoring it after process restart preserves the same IDs.
    net::Bytes exportOutstanding() const {
        net::Bytes raw;
        if(active_ && !terminal(receipt_.result))
            (void)encode(*active_,raw);
        return raw;
    }
    bool restoreOutstanding(const net::Bytes& raw,std::string* error=nullptr) {
        Packet restored;
        if(!decode(raw,restored,error))return false;
        if(restored.kind!=Message::Submit ||
           restored.clientInstance!=instance_ ||
           restored.requestId==std::numeric_limits<std::uint64_t>::max() ||
           restored.command.player==0)
            return protocolError(error,"PHYSIS invalid restored request identity");
        FieldEditBatch decoded;
        if(!decodeFieldEditCommand(restored.command,decoded,error))return false;
        active_=std::move(restored);
        nextId_=std::max(nextId_,active_->requestId+1);
        receipt_=Receipt{};
        status_=ClientStatus::Offline;
        sent_=true; // query first: an earlier process might have submitted
        query_=true;
        connected_=false;
        attempts_=0;
        nextSendMs_=0;
        if(error)error->clear();
        return true;
    }

    void disconnected() {
        connected_=false;
        if(active_ && !terminal(receipt_.result))
            status_=ClientStatus::Offline;
    }
    // Explicit user action after an indeterminate network timeout.
    // Original request ID/payload are preserved: retry is idempotent.
    void retry() {
        if(!active_ || terminal(receipt_.result))return;
        attempts_=0;
        nextSendMs_=0;
        query_=sent_;
        status_=connected_ ? ClientStatus::AwaitingReceipt : ClientStatus::Offline;
    }

    void poll(net::Conn& conn,std::uint64_t nowMs) {
        conn.update(nowMs);
        if(conn.state()!=net::ConnState::Connected) {
            disconnected();
            return;
        }
        if(!connected_) {
            connected_=true;
            attempts_=0;
            nextSendMs_=0;
            query_=sent_; // on reconnect first ask what happened
        }
        net::Bytes wire;
        int maxReceive=64;
        while(maxReceive-- > 0 && conn.recv(wire)) {
            Packet message;
            if(!decode(wire,message) || message.kind!=Message::Receipt ||
               !active_ || message.clientInstance!=instance_ ||
               message.requestId!=active_->requestId)
                continue;
            receipt_=message.receipt;
            if(receipt_.result==Result::Queued) {
                status_=ClientStatus::Queued;
                query_=true;
                attempts_=0; // received a positive receipt, keep polling
                nextSendMs_=nowMs+250;
            } else if(receipt_.result==Result::Unknown) {
                status_=ClientStatus::AwaitingReceipt;
                query_=false; // server lost the request; resubmit same ID
                attempts_=0;
                nextSendMs_=nowMs;
            } else if(receipt_.result==Result::Applied)
                status_=ClientStatus::Applied;
            else if(receipt_.result==Result::Conflict)
                status_=ClientStatus::Conflict;
            else if(receipt_.result==Result::Denied)
                status_=ClientStatus::Denied;
            else status_=ClientStatus::Rejected;
        }
        if(!active_ || terminal(receipt_.result) ||
           status_==ClientStatus::Uncertain || nowMs<nextSendMs_)
            return;
        if(attempts_>=12) {
            status_=ClientStatus::Uncertain;
            return;
        }
        Packet outgoing;
        if(query_) {
            outgoing.kind=Message::Query;
            outgoing.clientInstance=instance_;
            outgoing.requestId=active_->requestId;
        } else outgoing=*active_;
        net::Bytes bytes;
        if(!encode(outgoing,bytes)) {
            status_=ClientStatus::Rejected;
            receipt_.result=Result::Rejected;
            receipt_.detail="PHYSIS request could not be serialized";
            return;
        }
        conn.send(bytes);
        sent_=true;
        if(status_!=ClientStatus::Queued)status_=ClientStatus::AwaitingReceipt;
        ++attempts_;
        // Backoff is only network timing, never fed into deterministic ECS.
        const std::uint64_t delay=std::min<std::uint64_t>(200u*attempts_,2000u);
        nextSendMs_=nowMs+delay;
    }

private:
    std::uint64_t instance_=0,nextId_=1;
    std::optional<Packet> active_;
    Receipt receipt_{};
    ClientStatus status_=ClientStatus::Idle;
    bool connected_=false,sent_=false,query_=false;
    unsigned attempts_=0;
    std::uint64_t nextSendMs_=0;
};

} // namespace aiverse::physis::remote
