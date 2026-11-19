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
@interface StudioMetalView : NSView {
    NSPoint press_;
    BOOL dragged_;
}
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
    BOOL readyEmitted_; // Objective-C object ivars are zero-initialized
}
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev;
- (void)orbitX:(CGFloat)dx y:(CGFloat)dy;
- (void)zoom:(CGFloat)amount;
- (void)selectAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height;
- (void)sendStudioEvent:(const char*)event message:(const char*)message;
- (void)sendStudioReply:(const aiverse::Json&)reply;
- (void)openProjectSheetForRequestId:(int64_t)requestId;
- (void)openProjectSheetForRequestId:(int64_t)requestId confirmedDiscard:(BOOL)confirmedDiscard;
- (void)sendProjectCancelledReply:(int64_t)requestId;
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
- (void)mouseDown:(NSEvent*)event {
    press_=[self convertPoint:event.locationInWindow fromView:nil];
    dragged_=NO;
    [self.window makeFirstResponder:self];
}
- (void)mouseDragged:(NSEvent*)event {
    dragged_=YES;
    [self.host orbitX:event.deltaX y:event.deltaY];
}
- (void)mouseUp:(NSEvent*)event {
    const NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
    if (!dragged_ && std::hypot(double(point.x-press_.x), double(point.y-press_.y)) < 5.0)
        [self.host selectAtX:point.x y:point.y width:self.bounds.size.width height:self.bounds.size.height];
}
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
        styleMask:(NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable)
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
            preview_->frame(placement_.pixelsW, placement_.pixelsH, session_->workspace().map.data, session_->selectedEntityIndex());
            if (!readyEmitted_) {
                [self sendStudioEvent:"viewport.ready" message:"Metal Renderer3D"];
                readyEmitted_=YES;
            }
        } catch (const std::exception& e) {
            NSLog(@"AIVERSE native Scene failed: %s",e.what());
            [self sendStudioEvent:"viewport.error" message:e.what()];
            readyEmitted_=NO;
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
- (void)selectAtX:(CGFloat)x y:(CGFloat)y width:(CGFloat)width height:(CGFloat)height {
    if (!preview_) return;
    const int index=preview_->pick(float(x),float(y),float(width),float(height),session_->workspace().map.data);
    aiverse::Json request=aiverse::Json::object();
    request.set("id",aiverse::Json::number(int64_t(0)));
    request.set("method",aiverse::Json::string("studio.select"));
    aiverse::Json params=aiverse::Json::object();
    if(index>=0) params.set("id",aiverse::Json::string("entity:"+std::to_string(index)));
    else params.set("id",aiverse::Json());
    request.set("params",std::move(params));
    const aiverse::Json outcome=session_->dispatch(request);
    if (!outcome.has("error")) [self sendStudioEvent:"selection.changed" message:""];
}
- (void)sendStudioReply:(const aiverse::Json&)reply {
    const std::string output=reply.dump(-1);
    NSString* json=[[NSString alloc] initWithBytes:output.data() length:output.size() encoding:NSUTF8StringEncoding];
    if (json) [web_ evaluateJavaScript:[NSString stringWithFormat:@"window.__aiverseReceive && window.__aiverseReceive(%@);",json] completionHandler:nil];
}
- (void)sendProjectCancelledReply:(int64_t)requestId {
    aiverse::Json reply=aiverse::Json::object();
    reply.set("id",aiverse::Json::number(requestId));
    aiverse::Json payload=aiverse::Json::object();
    payload.set("cancelled",aiverse::Json::fromBool(true));
    payload.set("snapshot",aiverse::Json());
    reply.set("result",std::move(payload));
    [self sendStudioReply:reply];
}
- (void)openProjectSheetForRequestId:(int64_t)requestId {
    [self openProjectSheetForRequestId:requestId confirmedDiscard:NO];
}
- (void)openProjectSheetForRequestId:(int64_t)requestId confirmedDiscard:(BOOL)confirmedDiscard {
    const auto& ws=session_->workspace();
    if (!confirmedDiscard && (ws.map.dirty || ws.project.rulesDirty || ws.project.manifestDirty)) {
        NSAlert* alert=[[NSAlert alloc] init];
        alert.messageText=@"Discard unsaved AIVERSE changes?";
        alert.informativeText=@"Opening another project discards unsaved map and project changes.";
        [alert addButtonWithTitle:@"Discard and Open"];
        [alert addButtonWithTitle:@"Cancel"];
        [alert beginSheetModalForWindow:window_ completionHandler:^(NSModalResponse code) {
            if (code == NSAlertFirstButtonReturn)
                [self openProjectSheetForRequestId:requestId confirmedDiscard:YES];
            else [self sendProjectCancelledReply:requestId];
        }];
        return;
    }
    NSOpenPanel* panel=[NSOpenPanel openPanel];
    panel.canChooseFiles=YES;
    panel.canChooseDirectories=YES;
    panel.allowsMultipleSelection=NO;
    panel.canCreateDirectories=NO;
    panel.prompt=@"Open Project";
    [panel beginSheetModalForWindow:window_ completionHandler:^(NSModalResponse result) {
        aiverse::Json reply=aiverse::Json::object();
        reply.set("id",aiverse::Json::number(requestId));
        aiverse::Json payload=aiverse::Json::object();
        if (result==NSModalResponseOK && panel.URL.path.length) {
            try {
                payload.set("cancelled",aiverse::Json::fromBool(false));
                payload.set("snapshot",session_->openProject(panel.URL.path.UTF8String));
                if (preview_) preview_->resetCamera();
                [self sendStudioEvent:"workspace.changed" message:""];
            } catch (const std::exception& ex) {
                reply.set("error",aiverse::Json::string(ex.what()));
            }
        } else {
            payload.set("cancelled",aiverse::Json::fromBool(true));
            payload.set("snapshot",aiverse::Json());
        }
        if (!reply.has("error")) reply.set("result",std::move(payload));
        [self sendStudioReply:reply];
    }];
}
- (void)sendStudioEvent:(const char*)event message:(const char*)message {
    aiverse::Json packet=aiverse::Json::object();
    packet.set("event",aiverse::Json::string(event));
    aiverse::Json payload=aiverse::Json::object();
    payload.set("message",aiverse::Json::string(message ? message : ""));
    packet.set("payload",std::move(payload));
    const std::string output=packet.dump(-1);
    NSString* json=[[NSString alloc] initWithBytes:output.data() length:output.size() encoding:NSUTF8StringEncoding];
    if (json) [web_ evaluateJavaScript:[NSString stringWithFormat:@"window.__aiverseReceive && window.__aiverseReceive(%@);",json] completionHandler:nil];
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
    if (!placement_.visible) readyEmitted_=NO;
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
        if (req.getStr("method")=="studio.openProject" && req["id"].isNumber()) {
            // Path is chosen by AppKit, never accepted from untrusted JavaScript.
            [self openProjectSheetForRequestId:req["id"].integer()];
            return;
        }
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
        [self sendStudioReply:reply];
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
