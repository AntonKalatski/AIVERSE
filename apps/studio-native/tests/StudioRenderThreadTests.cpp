#include "apps/studio-native/StudioRenderThread.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace aiverse::studio;

static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }

int main() {
    try {
        {   // A kick runs the frame on the worker; flush waits for it.
            std::atomic<int> frames{0};
            std::thread::id worker;
            StudioRenderThread thread([&]{ worker=std::this_thread::get_id(); ++frames; });
            thread.kick();
            thread.flush();
            require(frames==1, "one kick, one frame");
            require(worker!=std::this_thread::get_id(), "frame ran on the owner thread");
            thread.flush();
            require(frames==1, "flush without kick renders nothing");
            // invoke runs on the same worker and returns the value.
            const auto id=thread.invoke([]{ return std::this_thread::get_id(); });
            require(id==worker, "invoke ran on a different thread than frames");
            thread.invoke([]{}); // void result
            require(thread.invoke([&]{ return thread.onWorker(); }), "onWorker inside invoke");
            require(thread.invoke([&]{ return thread.invoke([]{ return 7; }); })==7, "nested invoke on the worker");
        }
        {   // Kicks during a running frame coalesce; invoke waits for the frame.
            std::atomic<int> frames{0};
            std::atomic<bool> release{false}, started{false}, inFrame{false};
            StudioRenderThread thread([&]{
                inFrame=true; started=true;
                while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++frames; inFrame=false;
            });
            thread.kick();
            while (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            for (int i=0;i<50;++i) thread.kick(); // never blocks on the running frame
            std::thread releaser([&]{ std::this_thread::sleep_for(std::chrono::milliseconds(20)); release=true; });
            const bool overlapped=thread.invoke([&]{ return bool(inFrame); });
            releaser.join();
            require(!overlapped, "invoke overlapped a frame");
            thread.flush();
            require(frames==2, "kicks during a frame coalesce into one more frame");
        }
        {   // Exceptions: a throwing frame keeps the worker; invoke rethrows.
            std::atomic<int> calls{0};
            StudioRenderThread thread([&]{ if (++calls==1) throw std::runtime_error("frame"); });
            thread.kick(); thread.flush();
            thread.kick(); thread.flush();
            require(calls==2, "worker survives a throwing frame");
            bool caught=false;
            try { thread.invoke([]()->int{ throw std::runtime_error("call"); }); }
            catch (const std::runtime_error& e) { caught=std::string(e.what())=="call"; }
            require(caught, "invoke did not rethrow");
            require(thread.invoke([]{ return 3; })==3, "worker alive after a throwing call");
        }
        {   // stop() is idempotent and drops later kicks; invoke after stop throws.
            std::atomic<int> frames{0};
            StudioRenderThread thread([&]{ ++frames; });
            thread.stop();
            thread.kick();
            thread.flush();
            thread.stop();
            require(frames==0, "no frames after stop");
            bool threw=false;
            try { thread.invoke([]{}); } catch (const std::runtime_error&) { threw=true; }
            require(threw, "invoke after stop must throw");
        }
        {   // Unthreaded fallback: everything runs on the caller.
            std::thread::id seen;
            StudioRenderThread thread([&]{ seen=std::this_thread::get_id(); }, false);
            thread.kick();
            require(seen==std::this_thread::get_id(), "unthreaded kick off the caller");
            require(thread.invoke([]{ return 5; })==5, "unthreaded invoke");
            require(thread.framesRun()==1, "unthreaded frame count");
        }
        std::cout << "studio render thread PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "studio render thread FAIL: " << e.what() << "\n";
        return 1;
    }
}
