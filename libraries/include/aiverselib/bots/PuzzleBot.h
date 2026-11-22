#pragma once
// Puzzle-solving bot (puzzle-solvers): plays every puzzle family through the same command structs a player submits
// (Match3Swap, PuzzleFloodPick, PuzzlePipeRotate, PuzzleTilesSlide, PuzzleBlocksMove, PuzzlePackCommand,
// PuzzleCspAssign, PuzzleSokobanCommand), numbered lastMove + 1. The owner applies them with the ordinary *Apply
// function, which re-validates everything. Difficulty = search budget for hints/solvers + a mistake chance that
// swaps the hinted move for a random legal one; randomness only from the passed-in Random (sim.rng() or an RNG
// saved with the simulation). The bot is stateless: nothing to persist beyond the puzzle state and the RNG.
#include "../mechanics/PuzzleSolvers.h"
#include "engine/core/Random.h"
namespace aiverse::lib::bots {
struct PuzzleBotSkill {
    uint64_t budget=20000;                                         // node budget per hint/solver call
    int64_t mistakeMilli=0;                                        // 0..1000 chance to play a random legal move
    mechanics::Match3HintMode match3=mechanics::Match3HintMode::Best;      // First = shallow play, Best = previewed value
};
inline bool valid(const PuzzleBotSkill& s){return s.budget>=1&&s.budget<=mechanics::kPuzzleMaxBudget&&s.mistakeMilli>=0&&s.mistakeMilli<=1000&&s.match3<=mechanics::Match3HintMode::Best;}
// 0 easy, 1 normal, 2 hard.
inline std::optional<PuzzleBotSkill> puzzleBotSkillPreset(int level){
    switch(level){
    case 0:return PuzzleBotSkill{500,250,mechanics::Match3HintMode::First};
    case 1:return PuzzleBotSkill{5000,80,mechanics::Match3HintMode::Best};
    case 2:return PuzzleBotSkill{100000,0,mechanics::Match3HintMode::Best};
    default:return std::nullopt;
    }
}
namespace detail_puzzlebot {
// Consumes RNG only for chances strictly between 0 and 1000, so a perfect bot never touches the stream.
inline bool mistake(const PuzzleBotSkill& s,Random& rng){if(s.mistakeMilli<=0)return false;if(s.mistakeMilli>=1000)return true;return int64_t(rng.below(1000))<s.mistakeMilli;}
template<class T> std::optional<T> any(const std::vector<T>& v,Random& rng){if(v.empty())return std::nullopt;return v[rng.below(uint32_t(v.size()))];}
inline mechanics::PuzzleBudget budget(const PuzzleBotSkill& s){return mechanics::PuzzleBudget{s.budget,0};}
}
inline std::optional<mechanics::Match3Swap> puzzleBotMatch3(const mechanics::Match3Level& l,const mechanics::Match3State& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.status!=mechanics::Match3Status::Playing)return std::nullopt;
    std::optional<std::pair<int32_t,int32_t>> pick;
    if(detail_puzzlebot::mistake(skill,rng))pick=detail_puzzlebot::any(mechanics::match3Moves(l.rules,s.tiles),rng);
    if(!pick)pick=mechanics::match3Hint(l,s,skill.match3);
    if(!pick)return std::nullopt;
    return mechanics::Match3Swap{s.lastMove+1,pick->first,pick->second};
}
inline std::optional<mechanics::PuzzleFloodPick> puzzleBotFlood(const mechanics::PuzzleFloodLevel& l,const mechanics::PuzzleFloodState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.status!=mechanics::PuzzleFloodStatus::Playing||!mechanics::puzzleFloodCompatible(l,s))return std::nullopt;
    std::optional<uint8_t> color;
    if(detail_puzzlebot::mistake(skill,rng)){std::vector<uint8_t> all;for(uint8_t c=1;c<=l.colors;++c)if(c!=s.cells[size_t(l.origin)])all.push_back(c);color=detail_puzzlebot::any(all,rng);}
    if(!color)color=mechanics::puzzleFloodHint(l,s,detail_puzzlebot::budget(skill));
    if(!color)return std::nullopt;
    return mechanics::PuzzleFloodPick{s.lastMove+1,*color};
}
inline std::optional<mechanics::PuzzlePipeRotate> puzzleBotPipes(const mechanics::PuzzlePipeLevel& l,const mechanics::PuzzlePipeState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!mechanics::puzzlePipeCompatible(l,s))return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)){std::vector<int32_t> cells;for(int32_t c=0;c<int32_t(l.flags.size());++c)if(l.flags[size_t(c)]&mechanics::kPuzzlePipeRotatable)cells.push_back(c);if(auto c=detail_puzzlebot::any(cells,rng))return mechanics::PuzzlePipeRotate{s.lastMove+1,*c,1};}
    return mechanics::puzzlePipeHint(l,s,detail_puzzlebot::budget(skill));
}
inline std::optional<mechanics::PuzzleTilesSlide> puzzleBotTiles(const mechanics::PuzzleTilesState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!valid(s))return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)){std::vector<int32_t> cells;const auto blank=mechanics::puzzleTilesBlank(s);for(int32_t d=0;d<4;++d){auto c=mechanics::puzzleStep(s.width,s.height,blank,d);if(c>=0)cells.push_back(c);}if(auto c=detail_puzzlebot::any(cells,rng))return mechanics::PuzzleTilesSlide{s.lastMove+1,*c};}
    auto cell=mechanics::puzzleTilesHint(s,detail_puzzlebot::budget(skill));if(!cell)return std::nullopt;return mechanics::PuzzleTilesSlide{s.lastMove+1,*cell};
}
inline std::optional<mechanics::PuzzleBlocksMove> puzzleBotBlocks(const mechanics::PuzzleBlocksLevel& l,const mechanics::PuzzleBlocksState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!valid(l)||s.positions.size()!=l.blocks.size())return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)){
        std::vector<std::pair<int32_t,int32_t>> moves;for(int32_t b=0;b<int32_t(l.blocks.size());++b)for(int32_t d=0;d<4;++d){auto p=s.positions;if(mechanics::puzzleBlocksShift(l,p,b,d))moves.push_back({b,d});}
        if(auto m=detail_puzzlebot::any(moves,rng))return mechanics::PuzzleBlocksMove{s.lastMove+1,m->first,m->second};
    }
    auto r=mechanics::puzzleBlocksSolve(l,s.positions,detail_puzzlebot::budget(skill));
    if(r.status!=mechanics::PuzzleSearch::Solved||r.moves.empty())return std::nullopt;
    return mechanics::PuzzleBlocksMove{s.lastMove+1,r.moves.front().first,r.moves.front().second};
}
inline std::optional<mechanics::PuzzlePackCommand> puzzleBotPack(const mechanics::PuzzlePackLevel& l,const mechanics::PuzzlePackState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!valid(l)||!valid(s))return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)){
        // A random piece/orientation that fits at the first uncovered target cell.
        std::vector<mechanics::PuzzlePackCommand> options;
        for(int32_t p=0;p<int32_t(l.pieces.size());++p){
            if(std::any_of(s.placed.begin(),s.placed.end(),[&](const mechanics::PuzzlePlacement& x){return x.piece==p;}))continue;
            auto orient=mechanics::puzzlePieceOrientations(l.pieces[size_t(p)]);
            for(int32_t y=0;y<l.height;++y)for(int32_t x=0;x<l.width;++x)for(int32_t o=0;o<int32_t(orient.size());++o){
                auto copy=s;mechanics::PuzzlePackCommand c{s.lastMove+1,mechanics::PuzzlePackAction::Place,{p,o,x,y}};if(mechanics::puzzlePackApply(l,copy,c))options.push_back(c);
            }
        }
        if(auto c=detail_puzzlebot::any(options,rng))return c;
    }
    return mechanics::puzzlePackHint(l,s,detail_puzzlebot::budget(skill));
}
inline std::optional<mechanics::PuzzleCspAssign> puzzleBotCsp(const mechanics::PuzzleCsp& p,const mechanics::PuzzleCspState& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved)return std::nullopt;
    auto hint=mechanics::puzzleCspHint(p,s,detail_puzzlebot::budget(skill));if(!hint)return std::nullopt;
    if(detail_puzzlebot::mistake(skill,rng)&&s.values[size_t(hint->cell)]<0){
        // Right cell, guessed value: the hint later repairs a wrong guess.
        std::vector<int8_t> values;for(int32_t v=0;v<=mechanics::kPuzzleCspMaxValue;++v)if(p.domains[size_t(hint->cell)]&(1u<<v))values.push_back(int8_t(v));
        if(auto v=detail_puzzlebot::any(values,rng))hint->value=*v;
    }
    return hint;
}
inline std::optional<mechanics::PuzzleSokobanCommand> puzzleBotSokoban(const mechanics::PuzzleSokobanLevel& l,const mechanics::PuzzleSokobanSession& s,const PuzzleBotSkill& skill,Random& rng){
    if(!valid(skill)||s.solved||!mechanics::puzzleSokobanCompatible(l,s))return std::nullopt;
    if(!mechanics::puzzleSokobanDeadlocked(l,s)&&detail_puzzlebot::mistake(skill,rng)){
        std::vector<int32_t> dirs;for(int32_t d=0;d<4;++d){auto copy=s.history.current;if(strategy::pushMove(l.board,copy,d))dirs.push_back(d);}
        if(auto d=detail_puzzlebot::any(dirs,rng))return mechanics::PuzzleSokobanCommand{s.lastMove+1,mechanics::PuzzleSokobanAction::Move,*d};
    }
    if(auto hint=mechanics::puzzleSokobanHint(l,s,detail_puzzlebot::budget(skill)))return hint;
    // No solution known from here (budget or proof): back out of the last move and try again.
    if(!s.history.past.empty())return mechanics::PuzzleSokobanCommand{s.lastMove+1,mechanics::PuzzleSokobanAction::Undo,0};
    return std::nullopt;
}
}
