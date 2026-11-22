// traffic.services.<id>.carry: two of the crew treat the patient, carry him to the vehicle and deliver him to a ward
// (traffic.services.<id>.wards; none: the station it came from). State: CarryCar on the vehicle, the end of each
// carry in ServiceLedger::carried (RtsServiceState.h). Included in Traffic::responders (RtsTrafficCity.cpp).
if (sv.answers == kIncidentInjured && inc &&
    (c.task==kTaskCrewOut || c.task==kTaskWorking || (w.has<CarryCar>(c.e) && w.get<CarryCar>(c.e).owned))) {
    auto alive=[&](Entity e){return living(w,e);};
    if(!w.has<CarryCar>(c.e)){auto& n=w.add<CarryCar>(c.e);n.phase=kCarryToPatient;n.since=sim.tick();n.patient=inc->subject;n.owned=true;
        if(alive(inc->subject))w.get<Unit>(inc->subject).flags&=~kUnitInCargo;}
    auto& cc=w.get<CarryCar>(c.e);
    auto phase=[&](uint8_t value){cc.phase=value;cc.since=sim.tick();};
    const int state=cc.phase;
    inc->born=sim.tick();inc->assigned=sim.tick(); // A reserved patient never expires during transport.
    auto patient=inc->subject;
    auto ended=[&](bool delivered,Fixed x,Fixed z){auto& l=serviceLedger(w);
        while(!l.carried.empty()&&l.carried.front().tick+sim.tickRate()*60<sim.tick())l.carried.erase(l.carried.begin());
        l.carried.push_back({c.e,patient,delivered,x,z,sim.tick()});if(delivered)++l.carryDelivered;
        if(auto* d=w.tryGet<VehicleDoor>(c.e))d->open=0;w.remove<CarryCar>(c.e);};
    if(!alive(patient)||c.crew.size()<2){
        Fixed x{},z{};
        if(alive(patient)){w.get<Unit>(patient).flags&=~kUnitInCargo;inc->pos=v2(w.get<Transform>(patient).pos);x=w.get<Transform>(patient).pos.x;z=w.get<Transform>(patient).pos.z;}
        ended(false,x,z);finish(sim,st,c,false);continue;
    }
    auto medic=c.crew[0],other=c.crew[1];
    auto close=[&](Entity e,Vec3 goal,Fixed radius){return (w.get<Transform>(e).pos-goal).lengthSq()<=radius*radius;};
    auto move=[&](Entity e,Vec3 goal){if(sim.tick()%10==0||!w.get<Mobile>(e).moving)m_->moveTo(sim,e,goal,Fixed::fromRatio(12,100));};
    auto stop=[&](){for(auto e:c.crew)m_->stopMoving(sim,e);};
    const auto elapsed=int64_t(sim.tick())-int64_t(cc.since);
    if(state==kCarryToPatient){
        c.task=kTaskCrewOut;
        const auto at=w.get<Transform>(patient).pos;
        // Work from distinct sides of the patient, within arm reach rather than a full cell away.
        const auto facing=w.get<Unit>(patient).facing;Vec3 side{facing.z,{},-facing.x};
        const auto first=at+side*Fixed::fromRatio(8,25),second=at-side*Fixed::fromRatio(8,25);
        if(close(medic,first,Fixed::fromRatio(9,50))&&close(other,second,Fixed::fromRatio(9,50))){stop();phase(kCarryTreating);c.task=kTaskWorking;}
        else {move(medic,first);move(other,second);}
    }else if(state==kCarryTreating){
        c.task=kTaskWorking;stop();
        for(auto e:c.crew){auto toward=w.get<Transform>(patient).pos-w.get<Transform>(e).pos;if(toward.length().raw>0)w.get<Unit>(e).facing=toward/toward.length();}
        if(!close(medic,w.get<Transform>(patient).pos,Fixed::fromInt(1))||!close(other,w.get<Transform>(patient).pos,Fixed::fromInt(1))){phase(kCarryToPatient);continue;}
        if(elapsed>=std::max(sv.workTicks,sim.tickRate()*4u)){
            w.get<Unit>(patient).flags|=kUnitInCargo;phase(kCarryToVehicle);
        }
    }else if(state==kCarryToVehicle || state==kCarryToWard){
        c.task=kTaskWorking;
        Vec3 goal;
        if(state==kCarryToVehicle){
            Fixed length=Fixed::fromRatio(3,2);
            for(const auto& def:drv->rules().vehicles)if(def.type==w.get<Unit>(c.e).type)length=def.length;
            goal=pos3-w.get<Unit>(c.e).facing*(length/2+Fixed::fromRatio(2,5));
        }else goal={cc.entranceX,{},cc.entranceZ};
        move(medic,goal);
        const auto lead=w.get<Transform>(medic).pos;
        move(other,lead-w.get<Unit>(medic).facing*Fixed::fromRatio(45,100));
        auto& tr=w.get<Transform>(patient);tr.prevPos=tr.pos;tr.pos=lead-w.get<Unit>(medic).facing*Fixed::fromRatio(23,100);tr.pos.y+=Fixed::fromRatio(1,5);
        cc.carriedX=tr.pos.x;cc.carriedZ=tr.pos.z;
        auto direction=w.get<Unit>(medic).facing;cc.facingX=direction.x;cc.facingZ=direction.z;
        if(close(medic,goal,Fixed::fromRatio(3,10))&&close(other,goal,Fixed::fromRatio(9,10))){stop();phase(state==kCarryToVehicle?kCarryLoading:kCarryHandover);}
    }else if(state==kCarryLoading){
        c.task=kTaskWorking;stop();
        if(elapsed>=sim.tickRate()*3){
            // Prefer a ward (a station next to one of the service's ward types) over the station the crew came from.
            const Station* ward=nullptr;Fixed nearest=Fixed::fromInt(1000000);
            if(!sv.wards.empty())for(const auto& station:stations_[size_t(c.service)]){
                bool isWard=false;
                w.each<Unit,Transform>([&](Entity,const Unit& unit,const Transform& tr){
                    if(std::binary_search(sv.wards.begin(),sv.wards.end(),unit.type) && (v2(tr.pos)-station.pos).lengthSq()<Fixed::fromInt(1))isWard=true;
                });
                const auto distance=(station.pos-pos).lengthSq();if(isWard&&distance<nearest){nearest=distance;ward=&station;}
            }
            if(ward){c.homePiece=ward->piece;c.homeAlong=ward->along;}
            cc.wardFound=ward!=nullptr;
            if(c.homePiece>=0){auto goal=tl::trafficLanePoint(net_,net_.pieces[size_t(c.homePiece)],1,0,c.homeAlong);cc.destinationX=goal.x;cc.destinationZ=goal.y;}
            if(c.homePiece<0){phase(kCarryToVehicle);continue;} // A real medical facility is required.
            c.destPiece=c.homePiece;c.destAlong=c.homeAlong;c.task=kTaskHome;
            const auto facing=w.get<Unit>(c.e).facing;
            if(!reroute(sim,c,pos,{facing.x,facing.z})){c.task=kTaskWorking;continue;}
            for(auto e:c.crew)w.get<Unit>(e).flags|=kUnitInCargo;
            if(auto* d=w.tryGet<VehicleDoor>(c.e))d->open=0;phase(kCarryDriving);
        }
    }else if(state==kCarryDriving){
        c.task=kTaskHome;
        w.get<Transform>(patient).pos=pos3;
        for(auto e:c.crew){w.get<Transform>(e).pos=pos3;w.get<Unit>(e).flags|=kUnitInCargo;}
        if(slowNow&&atAddress()){
            Vec3 hospital=pos3;Fixed best=Fixed::fromInt(1000000);
            for(const auto& station:stations_[size_t(c.service)]){auto d=(station.pos-pos).lengthSq();if(d<best){best=d;hospital=v3(station.pos);}}
            Vec3 entrance{};bool found=false;best=Fixed::fromInt(1000000);
            for(int dz=-6;dz<=6;++dz)for(int dx=-6;dx<=6;++dx){
                Vec3 p{Fixed::fromInt(pos3.x.floorInt()+dx)+Fixed::fromRatio(1,2),{},Fixed::fromInt(pos3.z.floorInt()+dz)+Fixed::fromRatio(1,2)};
                int cell=gs.grid.cellOf(p);if(cell<0||!side_[size_t(cell)]||!gs.grid.passableAt(p,kLayerGround))continue;
                if((hospital-pos3).lengthSq().raw&&(p-pos3).dot(hospital-pos3).raw<=0)continue;
                auto d=(p-hospital).lengthSq()+(p-pos3).lengthSq();if(d<best){best=d;entrance=p;found=true;}
            }
            if(!found)continue;
            cc.entranceX=entrance.x;cc.entranceZ=entrance.z;
            auto f=w.get<Unit>(c.e).facing;Vec3 right{-f.z,{},f.x};if((entrance-pos3).dot(right).raw<0)right=right*Fixed::fromInt(-1);
            const auto clearance=m_->rules().types[w.get<Unit>(c.e).type].radius+Fixed::fromRatio(2,5);
            Vec3 exit=pos3+right*clearance;
            if(!gs.grid.passableAt(exit,kLayerGround))continue;
            for(size_t i=0;i<c.crew.size();++i){auto e=c.crew[i];w.get<Unit>(e).flags&=~kUnitInCargo;
                auto& tr=w.get<Transform>(e);tr.pos=exit+f*Fixed::fromRatio(int(i),3);tr.prevPos=tr.pos;}
            c.task=kTaskWorking;phase(kCarryToWard);
        }
    }else if(state==kCarryHandover){
        c.task=kTaskWorking;stop();
        if(elapsed>=sim.tickRate()*2){
            resolve(sim,st,*inc);ended(true,cc.entranceX,cc.entranceZ);finish(sim,st,c,true);
        }
    }
    continue;
}
