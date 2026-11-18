// SpectraFX graph-v2 typed value DAG.
// Pure, bounded, genre-neutral authoring/compiler evaluator. No RHI, ECS,
// filesystem, allocations in the particle update loop or user-defined code.
// Runtime-per-particle values are explicitly baked as bounded FxRange curves
// through the $curve authoring operator, never mislabeled as a GPU program.
#pragma once
#include "FxEffect.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aiverse::fx::vfx2 {

inline bool valueNodeType(std::string_view type) {
    for (const char* n : {
        "Float","Integer","Bool","Vec2","Vec3","Vec4","Color","String",
        "Range","Gradient","Curve","Parameter","AgeNormalized",
        "Add","Subtract","Multiply","Divide","Min","Max","Clamp","Lerp",
        "Sin","Cos","Abs","Negate","Sqrt","Pow","Floor","Ceil",
        "Length","Normalize","Dot","Cross","ComposeVec2","ComposeVec3",
        "ComposeVec4","Select","Greater","Less","Equal"
    }) if(type == n) return true;
    return false;
}

inline bool finiteValue(double x) { return std::isfinite(x) && std::abs(x) <= 10000000.0; }

class ValueGraph {
public:
    ValueGraph(const Json& nodes, const Json& defaults, const Json& overrides,
               std::vector<std::string>& errors)
        : nodes_(nodes), defaults_(defaults), overrides_(overrides), errors_(errors) {}

    bool initialize() {
        if(!nodes_.isArray() || nodes_.size()>256) return fail("invalid v2 nodes");
        if(!defaults_.isObject() && !defaults_.isNull()) return fail("parameters must be an object");
        if(!overrides_.isObject()) return fail("overrides must be an object");
        for(const auto& [name,value]:overrides_.members()) {
            if(!defaults_.has(name)) return fail("unknown parameter override '"+name+"'");
            if(!sameType(defaults_[name],value))
                return fail("parameter override '"+name+"' has a different type");
        }
        for(const Json& node:nodes_.items()) {
            if(!node.isObject() || !node["id"].isString() || !node["type"].isString())
                return fail("v2 node requires string id and type");
            const std::string id=node.getStr("id");
            if(id.empty() || id.size()>64) return fail("node id length must be 1..64");
            for(const char ch:id)if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
                (ch>='0'&&ch<='9')||ch=='_'||ch=='-'))
                return fail("invalid node id '"+id+"'");
            if(!all_.emplace(id,&node).second)return fail("duplicate node '"+id+"'");
            if(valueNodeType(node.getStr("type"))) data_.insert(id);
        }
        return true;
    }
    // Validate ALL value branches, including disconnected authoring nodes.
    // Only checking evaluation from live outputs would miss dormant data
    // cycles that become dangerous after a user reconnects a graph.
    bool validateAll() {
        std::set<std::string> done,active;
        for(const auto& id:data_)
            if(!validateData(id,done,active))return false;
        return true;
    }

    bool isValue(const std::string& name) const {return data_.count(name)!=0;}
    bool hasNode(const std::string& name) const {return all_.count(name)!=0;}

    // Resolve every nested $node and $param reference. For an explicit $curve
    // reference, sample a scalar expression at 2..64 age points to an existing
    // CPU FxRange curve (no per-particle interpreter or shader recompilation).
    bool resolve(const Json& input,Json& out,unsigned depth=0,
                 bool haveAge=false,double age=0) {
        if(depth>40)return fail("value nesting or expression graph exceeds depth 40");
        if(input.isObject()) {
            if(input.has("$param")) {
                if(input.size()!=1 || !input["$param"].isString())
                    return fail("$param expects one string key");
                return parameter(input["$param"].str(),out);
            }
            if(input.has("$node")) {
                if(input.size()!=1 || !input["$node"].isString())
                    return fail("$node expects one data-node ID");
                return evaluate(input["$node"].str(),out,depth+1,haveAge,age);
            }
            if(input.has("$curve")) {
                if(!input["$curve"].isString())return fail("$curve must reference a scalar data node");
                const std::string ref=input["$curve"].str();
                const int64_t n=input.getInt("samples",33);
                if((input.size()!=1 && !(input.size()==2 && input.has("samples")))||
                   n<2 || n>64)return fail("$curve samples must be in [2,64]");
                Json keys=Json::array();
                for(int64_t i=0;i<n;++i) {
                    const double t=double(i)/double(n-1);
                    Json sample;
                    if(!evaluate(ref,sample,depth+1,true,t))return false;
                    if(!sample.isNumber()||!finiteValue(sample.toDouble()))
                        return fail("$curve expression must return a finite scalar");
                    Json key=Json::array();
                    key.push(Json::number(t,6));
                    key.push(sample);
                    keys.push(std::move(key));
                }
                out=std::move(keys);
                return true;
            }
            out=Json::object();
            for(const auto& [k,v]:input.members()) {
                Json r;
                if(!resolve(v,r,depth+1,haveAge,age))return false;
                out.set(k,std::move(r));
            }
            return true;
        }
        if(input.isArray()) {
            out=Json::array();
            for(const Json& v:input.items()){
                Json r;
                if(!resolve(v,r,depth+1,haveAge,age))return false;
                out.push(std::move(r));
            }
            return true;
        }
        if(input.isNumber() && !finiteValue(input.toDouble()))
            return fail("nonfinite or unbounded numeric literal");
        out=input;
        return true;
    }

    bool evaluate(const std::string& id,Json& out,unsigned depth=0,
                  bool haveAge=false,double age=0) {
        if(depth>40)return fail("value expression depth exceeds 40");
        if(!isValue(id))return fail("unknown data node '"+id+"'");
        if(!visiting_.insert(id).second)return fail("cycle in data graph at '"+id+"'");
        const Json& node=*all_.at(id);
        const std::string type=node.getStr("type");
        const Json& p=node["properties"];
        const auto done=[&](bool ok){visiting_.erase(id);return ok;};
        if(type=="AgeNormalized") {
            if(!haveAge)return done(fail("AgeNormalized needs a $curve sampling context"));
            out=Json::number(age,6);
            return done(true);
        }
        if(type=="Parameter") {
            const std::string name=node.getStr("name",p.getStr("name"));
            return done(parameter(name,out));
        }
        if(type=="Float"||type=="Integer"||type=="Bool"||type=="Vec2"||
           type=="Vec3"||type=="Vec4"||type=="Color"||type=="String"||
           type=="Range"||type=="Gradient"||type=="Curve") {
            if(!p.isObject() || !p.has("value"))
                return done(fail("literal node '"+id+"' needs properties.value"));
            if(!resolve(p["value"],out,depth+1,haveAge,age))return done(false);
            if(type=="Float"||type=="Integer") {
                if(!out.isNumber())return done(fail(id+" must contain a number"));
                if(type=="Integer" &&
                   out.numberLiteral().find_first_of(".eE")!=std::string::npos)
                    return done(fail(id+" must contain an integer literal"));
            } else if(type=="Bool" && out.type()!=Json::Type::Bool)
                return done(fail(id+" must contain a bool"));
            else if(type=="String" && !out.isString())
                return done(fail(id+" must contain a string"));
            else if(type=="Vec2"||type=="Vec3"||type=="Vec4") {
                const size_t width=type=="Vec2"?2:type=="Vec3"?3:4;
                if(!vectorOf(out,width))return done(fail(id+" has wrong vector width/type"));
            }
            return done(true);
        }
        if(!p.isObject())return done(fail("math node '"+id+"' needs properties"));
        const auto arg=[&](const char* key,Json& v)->bool {
            if(!p.has(key))return fail(id+" missing "+key);
            return resolve(p[key],v,depth+1,haveAge,age);
        };
        Json a,b,d,t;
        if(!arg("a",a))return done(false);
        const bool unary=type=="Sin"||type=="Cos"||type=="Abs"||
             type=="Negate"||type=="Sqrt"||type=="Floor"||type=="Ceil"||
             type=="Length"||type=="Normalize";
        if(!unary && type!="ComposeVec2"&&type!="ComposeVec3"&&type!="ComposeVec4" &&
           !arg("b",b))return done(false);
        const auto number=[&](const Json& v,double& val)->bool {
            if(!v.isNumber()||!finiteValue(v.toDouble()))return false;
            val=v.toDouble();return true;
        };
        double x=0,y=0,z=0;
        if(type=="ComposeVec2"||type=="ComposeVec3"||type=="ComposeVec4") {
            if(!number(a,x) || !arg("b",b) || !number(b,y))
                return done(fail(id+" needs numeric vector components"));
            out=Json::array();
            out.push(Json::number(x,6));out.push(Json::number(y,6));
            if(type=="ComposeVec3"||type=="ComposeVec4") {
                if(!arg("c",d)||!number(d,z))return done(fail(id+" missing numeric c"));
                out.push(Json::number(z,6));
            }
            if(type=="ComposeVec4") {
                if(!arg("d",d)||!number(d,z))return done(fail(id+" missing numeric d"));
                out.push(Json::number(z,6));
            }
            return done(true);
        }
        if(type=="Length"||type=="Normalize"||type=="Dot"||type=="Cross") {
            const size_t width=a.isArray()?a.size():0;
            if(width<2||width>4||!vectorOf(a,width))
                return done(fail(id+" needs a finite vector"));
            if(type=="Dot"||type=="Cross") {
                if(!vectorOf(b,width) || (type=="Cross" && width!=3))
                    return done(fail(id+" has incompatible vector operands"));
            }
            std::vector<double> av(width),bv(width);
            for(size_t i=0;i<width;++i)av[i]=a[i].toDouble();
            if(type=="Dot"||type=="Cross")for(size_t i=0;i<width;++i)bv[i]=b[i].toDouble();
            if(type=="Dot") {
                double sum=0;for(size_t i=0;i<width;++i)sum+=av[i]*bv[i];
                return done(safeNumber(sum,out,id));
            }
            if(type=="Cross") {
                const std::vector<double> result{av[1]*bv[2]-av[2]*bv[1],
                    av[2]*bv[0]-av[0]*bv[2],av[0]*bv[1]-av[1]*bv[0]};
                out=Json::array();
                for(double n:result){if(!finiteValue(n))return done(fail(id+" overflow"));out.push(Json::number(n,6));}
                return done(true);
            }
            double square=0;
            for(double v:av)square+=v*v;
            const double length=std::sqrt(square);
            if(type=="Length")return done(safeNumber(length,out,id));
            if(length<=0.00000001)return done(fail("Normalize of zero vector in "+id));
            out=Json::array();
            for(double v:av)out.push(Json::number(v/length,6));
            return done(true);
        }
        if(type=="Select") {
            if(a.type()!=Json::Type::Bool)return done(fail("Select.a must be a bool"));
            if(!arg("c",d))return done(false);
            if(!sameType(b,d))return done(fail("Select branches have different types"));
            out=a.boolean()?b:d;
            return done(true);
        }
        if(!number(a,x)) {
            // Component-wise operations on same-width vectors, or vector by
            // scalar scaling. Never silently broadcast arbitrary JSON.
            const size_t w=a.isArray()?a.size():0;
            if(w<2||w>4||!vectorOf(a,w))
                return done(fail(id+" expects a finite scalar or vector"));
            const bool scalar=b.isNumber()&&(type=="Multiply"||type=="Divide");
            if(!scalar && !vectorOf(b,w))
                return done(fail(id+" incompatible vector operands"));
            out=Json::array();
            for(size_t i=0;i<w;++i) {
                const double left=a[i].toDouble(),right=scalar?b.toDouble():b[i].toDouble();
                double result=0;
                if(type=="Add")result=left+right;
                else if(type=="Subtract")result=left-right;
                else if(type=="Multiply")result=left*right;
                else if(type=="Divide") {
                    if(std::abs(right)<1e-12)return done(fail(id+" division by zero"));
                    result=left/right;
                } else return done(fail(id+" does not support vector inputs"));
                if(!finiteValue(result))return done(fail(id+" result overflow"));
                out.push(Json::number(result,6));
            }
            return done(true);
        }
        if(!unary && !number(b,y))
            return done(fail(id+" expects numeric b"));
        if(type=="Add")z=x+y;
        else if(type=="Subtract")z=x-y;
        else if(type=="Multiply")z=x*y;
        else if(type=="Divide") {
            if(std::abs(y)<1e-12)return done(fail(id+" division by zero"));
            z=x/y;
        } else if(type=="Min")z=std::min(x,y);
        else if(type=="Max")z=std::max(x,y);
        else if(type=="Sin")z=std::sin(x);
        else if(type=="Cos")z=std::cos(x);
        else if(type=="Abs")z=std::abs(x);
        else if(type=="Negate")z=-x;
        else if(type=="Sqrt") {
            if(x<0)return done(fail(id+" negative square root"));z=std::sqrt(x);
        } else if(type=="Pow")z=std::pow(x,y);
        else if(type=="Floor")z=std::floor(x);
        else if(type=="Ceil")z=std::ceil(x);
        else if(type=="Greater"||type=="Less"||type=="Equal") {
            out=Json::fromBool(type=="Greater"?x>y:type=="Less"?x<y:x==y);
            return done(true);
        } else if(type=="Clamp") {
            if(!arg("c",d)||!number(d,z)||y>z)
                return done(fail(id+" Clamp requires min <= max"));
            z=std::clamp(x,y,z);
        } else if(type=="Lerp") {
            if(!arg("t",t)||!number(t,z))return done(fail(id+" Lerp.t must be numeric"));
            z=x+(y-x)*std::clamp(z,0.0,1.0);
        } else return done(fail("unknown math operation "+type));
        return done(safeNumber(z,out,id));
    }

private:
    bool collectDependencies(const Json& tree,std::vector<std::string>& deps,unsigned depth=0) {
        if(depth>40)return fail("data dependency nesting exceeds 40");
        if(tree.isObject()) {
            if(tree.has("$node")) {
                if(!tree["$node"].isString())return fail("$node reference must be a string");
                deps.push_back(tree["$node"].str());
                return true;
            }
            if(tree.has("$curve")) {
                if(!tree["$curve"].isString())return fail("$curve reference must be a string");
                deps.push_back(tree["$curve"].str());
                return true;
            }
            for(const auto& [key,value]:tree.members()) {
                (void)key;
                if(!collectDependencies(value,deps,depth+1))return false;
            }
        } else if(tree.isArray()) {
            for(const auto& value:tree.items())
                if(!collectDependencies(value,deps,depth+1))return false;
        }
        return true;
    }
    bool validateData(const std::string& id,std::set<std::string>& done,
                      std::set<std::string>& active) {
        if(done.count(id))return true;
        if(!isValue(id))return fail("unknown data dependency '"+id+"'");
        if(!active.insert(id).second)return fail("cycle in data graph involving '"+id+"'");
        std::vector<std::string> deps;
        const Json& node=*all_.at(id);
        if(!collectDependencies(node["properties"],deps))return false;
        for(const auto& ref:deps) {
            if(!validateData(ref,done,active))return false;
        }
        active.erase(id);
        done.insert(id);
        return true;
    }

    static bool sameType(const Json& a,const Json& b) {
        if(a.type()!=b.type())return false;
        if(a.isArray()) {
            if(a.size()!=b.size())return false;
            for(size_t i=0;i<a.size();++i)
                if(!sameType(a[i],b[i]))return false;
        } else if(a.isObject()) {
            if(a.size()!=b.size())return false;
            for(const auto& [key,value]:a.members())
                if(!b.has(key)||!sameType(value,b[key]))return false;
        }
        return true;
    }
    static bool vectorOf(const Json& j,size_t width) {
        if(!j.isArray()||j.size()!=width)return false;
        for(const auto& v:j.items())
            if(!v.isNumber()||!finiteValue(v.toDouble()))return false;
        return true;
    }
    bool parameter(const std::string& name,Json& out) {
        if(!defaults_.has(name))return fail("unknown parameter '"+name+"'");
        out=overrides_.has(name)?overrides_[name]:defaults_[name];
        return true;
    }
    bool safeNumber(double n,Json& out,const std::string& context) {
        if(!finiteValue(n))return fail(context+" nonfinite/unbounded result");
        out=Json::number(n,6);
        return true;
    }
    bool fail(const std::string& reason) {
        errors_.push_back("vfx graph v2: "+reason);
        return false;
    }
    const Json& nodes_;
    const Json& defaults_;
    const Json& overrides_;
    std::vector<std::string>& errors_;
    std::map<std::string,const Json*> all_;
    std::set<std::string> data_,visiting_;
};

} // namespace aiverse::fx::vfx2
