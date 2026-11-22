// traffic.services.<id>.custody: the crew detains an unarmed offender, walks him to the vehicle and delivers him to
// the station (state: CustodyCar on the vehicle, InCustody / ServicePassenger / StandIn on the prisoner,
// ServiceLedger per player; ServiceState.h). Included in Traffic::services (TrafficCity.cpp).
if(sv.answers==kIncidentCrime && inc && (c.task==kTaskCrewOut||c.task==kTaskWorking||(w.has<CustodyCar>(c.e)&&w.get<CustodyCar>(c.e).owned))){
 auto&ledger=serviceLedger(w);
 auto custody=[&]()->CustodyCar&{if(!w.has<CustodyCar>(c.e))w.add<CustodyCar>(c.e);return w.get<CustodyCar>(c.e);};
 auto phase=[&](uint8_t value){auto&cc=custody();cc.phase=value;cc.since=sim.tick();};
 auto prisoner=inc->offender;
 auto alive=[&](Entity e){return living(w,e);};
 const int state=w.has<CustodyCar>(c.e)?w.get<CustodyCar>(c.e).phase:0;
 const bool dead=alive(prisoner)&&w.has<StandIn>(prisoner);
 auto restoreCrew=[&](){if(auto*cc=w.tryGet<CustodyCar>(c.e)){for(auto [e,was]:cc->crewHeld)if(alive(e)&&was==1)w.get<Unit>(e).flags&=~kUnitDisabled;cc->crewHeld.clear();}};
 auto passenger=[&]()->ServicePassenger&{if(!w.has<ServicePassenger>(prisoner))w.add<ServicePassenger>(prisoner);return w.get<ServicePassenger>(prisoner);};
 auto release=[&](){
  const auto*cc=w.tryGet<CustodyCar>(c.e);
  if(alive(prisoner)){auto&u=w.get<Unit>(prisoner);u.flags&=~kUnitInCargo;if(cc&&cc->wasPossessed)u.flags|=kUnitPossessed;if(!(cc&&cc->wasDisabled)&&!dead)u.flags&=~kUnitDisabled;
   w.remove<InCustody>(prisoner);if(auto*sp=w.tryGet<ServicePassenger>(prisoner)){sp->claim=0;sp->aboard=false;}}
  for(auto&pl:ledger.players)if(pl.car==c.e){pl.phase=kHeldNone;}
  restoreCrew();w.remove<CustodyCar>(c.e);};
 if(!alive(prisoner)||c.crew.empty()) {release();finish(sim,st,c,false);continue;}
 if(!state&&!dead&&offenderArmed(sim,prisoner)){
  restoreCrew();
  // Armed suspects with ammunition cannot be handcuffed. Keep the combat call open.
  inc->assigned=sim.tick();inc->pos=v2(w.get<Transform>(prisoner).pos);c.taskUntil=sim.tick()+sim.tickRate()*3;
  if(sim.tick()%sim.tickRate()==0)for(auto e:c.crew){Order order;order.type=OrderType::Attack;order.target=prisoner;order.point=w.get<Transform>(prisoner).pos;m_->issue(sim,e,order,false);}
 }else{
  if(const auto*claim=w.tryGet<InCustody>(prisoner);claim&&claim->car!=c.e){resolve(sim,st,*inc);finish(sim,st,c,true);continue;}
  inc->assigned=sim.tick();inc->born=sim.tick();inc->state=2;
  auto lead=c.crew.front();auto at=w.get<Transform>(prisoner).pos;
  auto close=[&](Entity e,Vec3 q,Fixed r){return (w.get<Transform>(e).pos-q).lengthSq()<=r*r;};
  auto move=[&](Entity e,Vec3 q){if(auto*o=w.tryGet<Orders>(e))o->queue.clear();if(sim.tick()%10==0||!w.get<Mobile>(e).moving)m_->moveTo(sim,e,q,Fixed::fromRatio(12,100));};
  for(auto e:c.crew){auto&u=w.get<Unit>(e);auto&held=custody().crewHeld;bool known=false;for(auto&h:held)known=known||h.first==e;
   if(!known)held.push_back({e,uint8_t((u.flags&kUnitDisabled)?2:1)});u.flags|=kUnitDisabled;if(auto*a=w.tryGet<Combat>(e)){a->target=kNullEntity;a->chargeTarget=kNullEntity;}if(auto*o=w.tryGet<Orders>(e))o->queue.clear();}
  uint8_t player=0;if(auto*td=m_->topDown())if(auto*store=td->store(sim))for(size_t p=1;p<store->pilots.size();++p)if(store->pilots[p].hero==prisoner)player=uint8_t(p);
  auto playerPhase=[&](uint8_t p){if(player){auto&pl=ledger.player(player);pl.phase=p;pl.car=c.e;}};
  if(!state){
   c.task=kTaskCrewOut;
   if(!close(lead,at,Fixed::fromRatio(9,10))){move(lead,at);continue;}
   if(!dead&&drv->isDriving(sim,prisoner)&&!drv->policeExit(sim,prisoner)){restoreCrew();continue;}
   if(!dead&&offenderArmed(sim,prisoner)){restoreCrew();continue;}
   auto&cc=custody();cc.owned=true;cc.prisoner=prisoner;cc.wasDisabled=(w.get<Unit>(prisoner).flags&kUnitDisabled)!=0;cc.wasPossessed=(w.get<Unit>(prisoner).flags&kUnitPossessed)!=0;
   if(!w.has<InCustody>(prisoner))w.add<InCustody>(prisoner);w.get<InCustody>(prisoner).car=c.e;passenger().claim=kClaimCustody;
   w.get<Unit>(prisoner).flags=(w.get<Unit>(prisoner).flags|kUnitDisabled)&~kUnitPossessed;m_->stopMoving(sim,prisoner);m_->stopMoving(sim,lead);phase(kCustodyCuffing);playerPhase(kHeldCuffed);continue;
  }
  if(auto*o=w.tryGet<Orders>(prisoner))o->queue.clear();
  if(auto*a=w.tryGet<Combat>(prisoner)){a->target=kNullEntity;a->chargeTarget=kNullEntity;}
  auto&cc=custody();
  const auto elapsed=int64_t(sim.tick())-int64_t(cc.since);
  if(state==kCustodyCuffing){c.task=kTaskWorking;m_->stopMoving(sim,lead);m_->stopMoving(sim,prisoner);
   if(elapsed>=sim.tickRate()*2){auto f=w.get<Unit>(c.e).facing;Vec3 side{-f.z,{},f.x};if((at-pos3).dot(side).raw<0)side=side*Fixed::fromInt(-1);
    Fixed length=Fixed::fromRatio(6,5),width=Fixed::fromRatio(1,2);for(const auto&v:drv->rules().vehicles)if(v.type==w.get<Unit>(c.e).type){length=v.length;width=v.width;}
    auto door=pos3+side*(width/2+Fixed::fromRatio(35,100))-f*(length/5);
    cc.doorX=door.x;cc.doorZ=door.z;phase(kCustodyToDoor);playerPhase(kHeldWalked);
   }
  }else if(state==kCustodyToDoor||state==kCustodyToStation){
   c.task=kTaskWorking;Vec3 goal=state==kCustodyToDoor?Vec3{cc.doorX,{},cc.doorZ}:Vec3{cc.entryX,{},cc.entryZ};
   move(lead,goal);auto leader=w.get<Transform>(lead).pos;
   if(dead){auto&tr=w.get<Transform>(prisoner);tr.prevPos=tr.pos;tr.pos=leader;w.get<Unit>(prisoner).facing=w.get<Unit>(lead).facing;}
   else move(prisoner,leader-w.get<Unit>(lead).facing*Fixed::fromRatio(2,5));
   for(size_t i=1;i<c.crew.size();++i)move(c.crew[i],leader-w.get<Unit>(lead).facing*Fixed::fromRatio(4,5));
   // a walk that cannot finish (someone in the way, the door cell off the walkable sidewalk) ends at the door anyway
   if(elapsed>=sim.tickRate()*12&&!dead){auto&lt=w.get<Transform>(lead);lt.prevPos=lt.pos=goal;auto&pt=w.get<Transform>(prisoner);pt.prevPos=pt.pos=goal-w.get<Unit>(lead).facing*Fixed::fromRatio(2,5);}
   if(close(lead,goal,Fixed::fromRatio(3,10))&&close(prisoner,goal,Fixed::fromRatio(9,10))){m_->stopMoving(sim,lead);m_->stopMoving(sim,prisoner);
    const bool toDoor=state==kCustodyToDoor;phase(toDoor?kCustodyBoarding:kCustodyHandover);playerPhase(toDoor?kHeldBoarding:kHeldDelivered);}
  }else if(state==kCustodyBoarding){
   c.task=kTaskWorking;cc.doorOpen=true;
   if(elapsed>=sim.tickRate()*2){
    if(c.homePiece<0)continue;
    c.destPiece=c.homePiece;c.destAlong=c.homeAlong;c.task=kTaskHome;auto f=w.get<Unit>(c.e).facing;
    if(!reroute(sim,c,pos,{f.x,f.z})){c.task=kTaskWorking;continue;}
    w.get<Unit>(prisoner).flags|=kUnitInCargo;passenger().aboard=true;
    for(auto e:c.crew){m_->stopMoving(sim,e);w.get<Unit>(e).flags|=kUnitInCargo;}
    custody().doorOpen=false;phase(kCustodyDriving);playerPhase(kHeldDriven);
   }
  }else if(state==kCustodyDriving){
   c.task=kTaskHome;w.get<Transform>(prisoner).pos=pos3;for(auto e:c.crew){w.get<Transform>(e).pos=pos3;w.get<Unit>(e).flags|=kUnitInCargo;}
   if(slowNow&&atAddress()){
    Vec3 station=pos3;Fixed nearest=Fixed::fromInt(1000000);
    for(const auto&base:stations_[size_t(c.service)]){auto distance=(base.pos-pos).lengthSq();if(distance<nearest){nearest=distance;station=v3(base.pos);}}
    Vec3 entry{};bool found=false;nearest=Fixed::fromInt(1000000);
    for(int dz=-6;dz<=6;++dz)for(int dx=-6;dx<=6;++dx){Vec3 q{Fixed::fromInt(pos.x.floorInt()+dx)+Fixed::fromRatio(1,2),{},Fixed::fromInt(pos.y.floorInt()+dz)+Fixed::fromRatio(1,2)};
     int cell=gs.grid.cellOf(q);if(cell<0||!side_[size_t(cell)]||!gs.grid.passableAt(q,kLayerGround))continue;
     if((station-pos3).lengthSq().raw&&(q-pos3).dot(station-pos3).raw<=0)continue;
     auto distance=(q-station).lengthSq()+(q-pos3).lengthSq();if(distance<nearest){nearest=distance;entry=q;found=true;}}
    if(!found)continue;cc.entryX=entry.x;cc.entryZ=entry.z;
    auto f=w.get<Unit>(c.e).facing;Vec3 side{-f.z,{},f.x};if((entry-pos3).dot(side).raw<0)side=side*Fixed::fromInt(-1);
    auto exit=pos3+side*(m_->rules().types[w.get<Unit>(c.e).type].radius+Fixed::fromRatio(2,5));if(!gs.grid.passableAt(exit,kLayerGround))continue;
    w.get<Unit>(prisoner).flags&=~kUnitInCargo;passenger().aboard=false;w.get<Transform>(prisoner).pos=exit;
    for(size_t i=0;i<c.crew.size();++i){auto e=c.crew[i];w.get<Unit>(e).flags&=~kUnitInCargo;w.get<Transform>(e).pos=exit+f*Fixed::fromRatio(int(i)+1,3);}
    custody().doorOpen=true;phase(kCustodyToStation);playerPhase(kHeldDelivered);
   }
  }else if(state==kCustodyHandover){
   c.task=kTaskWorking;
   if(elapsed>=sim.tickRate()*3){
    const auto exit=w.get<Transform>(prisoner).pos;
    const uint16_t original=dead?w.get<StandIn>(prisoner).original:0;
    release();
    if(player){
     if(dead){auto replacement=m_->spawn(sim,original,player,exit);if(replacement.valid()){w.get<Unit>(replacement).flags|=kUnitPossessed;auto&p=m_->topDown()->store(sim)->pilots[player];p.hero=replacement;p.heroType=original;p.home=exit;p.input={};p.vel={};silentRemove(sim,prisoner);}}
     auto&pl=ledger.player(player);pl.phase=kHeldNone;pl.car=kNullEntity;++pl.deliveries;
    }else silentRemove(sim,prisoner);
    if(alive(prisoner))w.remove<StandIn>(prisoner);++ledger.custodyDelivered;resolve(sim,st,*inc);finish(sim,st,c,true);
   }
  }
  continue;
 }
}
