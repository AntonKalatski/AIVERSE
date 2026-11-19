// Experimental AIVERSE Studio: real WKWebView + native Metal Renderer3D preview.
// Render surface is a sibling NSView over the DOM viewport bounds (no CPU frame streaming).
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include "apps/studio-native/StudioSession.h"
#include "apps/studio-native/StudioViewportLayout.h"
#include "apps/studio-native/mac/StudioMetalScene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>

#ifndef AIVERSE_STUDIO_SOURCE_DIR
#define AIVERSE_STUDIO_SOURCE_DIR "."
#endif

@class StudioHost;
@interface StudioMetalView : NSView
@property(nonatomic, assign) StudioHost* host;
@end

@interface StudioHost : NSObject <NSApplicationDelegate, WKScriptMessageHandler, WKNavigationDelegate> {
    NSWindow* window_;
    WKWebView* web_;
    StudioMetalView* viewport_;
    NSTimer* timer_;
    NSString* dist_;
    NSURL* devURL_;
    std::unique_ptr<aiverse::studio::StudioSession> session_;
    std::unique_ptr<aiverse::studio::StudioMetalScene> preview_;
    aiverse::studio::NativeViewportPlacement placement_;
}
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev;
- (void)orbitX:(CGFloat)dx y:(CGFloat)dy;
- (void)zoom:(CGFloat)amount;
@end

@implementation StudioMetalView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)wantsUpdateLayer { return YES; }
- (CALayer*)makeBackingLayer { return [CAMetalLayer layer]; }
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
    }
    return self;
}
- (void)mouseDown:(NSEvent*)event { (void)event; [self.window makeFirstResponder:self]; }
- (void)mouseDragged:(NSEvent*)event { [self.host orbitX:event.deltaX y:event.deltaY]; }
- (void)rightMouseDragged:(NSEvent*)event { [self.host orbitX:event.deltaX y:event.deltaY]; }
- (void)scrollWheel:(NSEvent*)event {
    const CGFloat wheel = event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 0.12 : 1.0);
    [self.host zoom:wheel];
}
@end

@implementation StudioHost
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev {
    if ((self=[super init])) {
        std::string root=AIVERSE_STUDIO_SOURCE_DIR;
        session_=std::make_unique<aiverse::studio::StudioSession>(root,project);
        NSURL* bundled=[[NSBundle mainBundle] URLForResource:@"index" withExtension:@"html" subdirectory:@"studio-web"];
        dist_=bundled ? [[[bundled URLByDeletingLastPathComponent] path] stringByStandardizingPath]
                      : [[NSString stringWithUTF8String:(root+"/apps/studio-web/dist").c_str()] stringByStandardizingPath];
        if (dev) devURL_=[NSURL URLWithString:@"http://127.0.0.1:4173/"];
    }
    return self;
}
- (BOOL)trusted:(NSURL*)url {
    if (!url) return NO;
    if (url.isFileURL) {
        NSString* path=[url.path stringByStandardizingPath];
        return [path hasPrefix:[dist_ stringByAppendingString:@"/"]];
    }
    return devURL_ && [url.scheme isEqualToString:@"http"] &&
           [url.host isEqualToString:@"127.0.0.1"] && url.port.integerValue==4173;
}
- (void)applicationDidFinishLaunching:(NSNotification*)note {
    (void)note;
    window_=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1440,900)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    window_.title=@"AIVERSE Studio HTML5";
    [window_ center];

    WKWebViewConfiguration* cfg=[[WKWebViewConfiguration alloc] init];
    [cfg.userContentController addScriptMessageHandler:self name:@"aiverse"];
    web_=[[WKWebView alloc] initWithFrame:window_.contentView.bounds configuration:cfg];
    web_.navigationDelegate=self;
    web_.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
    [window_.contentView addSubview:web_];

    viewport_=[[StudioMetalView alloc] initWithFrame:NSZeroRect];
    viewport_.host=self;
    viewport_.hidden=YES;
    [window_.contentView addSubview:viewport_ positioned:NSWindowAbove relativeTo:web_];
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    if (devURL_) [web_ loadRequest:[NSURLRequest requestWithURL:devURL_]];
    else {
        NSURL* root=[NSURL fileURLWithPath:dist_ isDirectory:YES];
        [web_ loadFileURL:[root URLByAppendingPathComponent:@"index.html"] allowingReadAccessToURL:root];
    }
    timer_=[NSTimer timerWithTimeInterval:1.0/60 target:self selector:@selector(tick) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:timer_ forMode:NSRunLoopCommonModes];
}
- (void)tick {
    if (!placement_.visible || viewport_.hidden || window_.isMiniaturized || !window_.visible) return;
    @autoreleasepool {
        try {
            if (!preview_) preview_=std::make_unique<aiverse::studio::StudioMetalScene>((__bridge void*)viewport_.layer);
            // StudioSession/renderer are owned and called on the main thread.
            preview_->frame(placement_.pixelsW, placement_.pixelsH, session_->workspace().map.data);
        } catch (const std::exception& e) {
            NSLog(@"AIVERSE native Scene failed: %s",e.what());
            viewport_.hidden=YES;
            placement_.visible=false;
            preview_.reset();
        }
    }
}
- (void)orbitX:(CGFloat)dx y:(CGFloat)dy {
    if (preview_ && std::isfinite(double(dx)) && std::isfinite(double(dy)))
        preview_->orbit(float(dx),float(dy));
}
- (void)zoom:(CGFloat)amount {
    if (preview_ && std::isfinite(double(amount))) preview_->zoom(float(amount));
}
- (void)applyViewport:(const aiverse::Json&)params {
    aiverse::studio::ViewportRect rect;
    if (params.getStr("id") != "scene") { viewport_.hidden=YES; placement_={}; return; }
    rect.x=params["x"].toDouble(); rect.y=params["y"].toDouble();
    rect.width=params["width"].toDouble(); rect.height=params["height"].toDouble();
    rect.dpr=params["dpr"].toDouble(1);
    rect.visible=params.getBool("visible");
    NSRect webBounds=web_.bounds;
    placement_=aiverse::studio::viewportPlacement(rect,
        webBounds.size.width,webBounds.size.height,window_.backingScaleFactor);
    viewport_.hidden=!placement_.visible;
    if (placement_.visible) {
        viewport_.frame=NSMakeRect(placement_.x,placement_.y,placement_.width,placement_.height);
        CAMetalLayer* layer=(CAMetalLayer*)viewport_.layer;
        layer.contentsScale=window_.backingScaleFactor;
        layer.drawableSize=CGSizeMake(placement_.pixelsW,placement_.pixelsH);
    }
}
- (void)webView:(WKWebView*)view decidePolicyForNavigationAction:(WKNavigationAction*)action decisionHandler:(void (^)(WKNavigationActionPolicy))done {
    (void)view;
    done([self trusted:action.request.URL] ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)controller;
    if (![message.name isEqualToString:@"aiverse"] || !message.frameInfo.isMainFrame ||
        ![self trusted:message.frameInfo.request.URL] || ![NSJSONSerialization isValidJSONObject:message.body]) return;
    NSError* error=nil;
    NSData* data=[NSJSONSerialization dataWithJSONObject:message.body options:0 error:&error];
    if (error || !data || data.length>1024*1024) return;
    try {
        std::string bytes((const char*)data.bytes,(size_t)data.length);
        const aiverse::Json req=aiverse::Json::parse(bytes);
        aiverse::Json reply;
        if (req.getStr("method")=="studio.viewportRect" && req["id"].isNumber()) {
            [self applyViewport:req["params"]];
            reply=aiverse::Json::object();
            reply.set("id",req["id"]);
            aiverse::Json result=aiverse::Json::object();
            result.set("ok",aiverse::Json::fromBool(true));
            reply.set("result",std::move(result));
        } else {
            reply=session_->dispatch(req);
        }
        std::string output=reply.dump(-1);
        NSString* json=[[NSString alloc] initWithBytes:output.data() length:output.size() encoding:NSUTF8StringEncoding];
        if (json) [web_ evaluateJavaScript:[NSString stringWithFormat:@"window.__aiverseReceive && window.__aiverseReceive(%@);",json] completionHandler:nil];
    } catch (const std::exception& e) { NSLog(@"AIVERSE bridge exception: %s",e.what()); }
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { (void)app; return YES; }
- (void)applicationWillTerminate:(NSNotification*)note {
    (void)note;
    [timer_ invalidate];
    timer_=nil;
    preview_.reset(); // release GPU resources before CAMetalLayer
    [web_.configuration.userContentController removeScriptMessageHandlerForName:@"aiverse"];
}
@end

int main(int argc,char** argv) {
    @autoreleasepool {
        std::string project;
        bool dev=false;
        for(int i=1;i<argc;++i) {
            std::string arg(argv[i]);
            if(arg=="--dev")dev=true;
            else if(arg=="--project"&&i+1<argc)project=argv[++i];
            else { std::fprintf(stderr,"Usage: AiverseStudioHTML5 [--dev] [--project /path/to/project]\n"); return 2; }
        }
        try {
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            StudioHost* host=[[StudioHost alloc] initWithProject:project dev:dev];
            [NSApp setDelegate:host];
            [NSApp run];
        } catch(const std::exception& e) {
            std::fprintf(stderr,"Studio startup failed: %s\n",e.what()); return 1;
        }
    }
    return 0;
}
