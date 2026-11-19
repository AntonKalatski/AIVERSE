// Experimental standalone WKWebView shell; real Workspace, no ImGui, no native GPU viewport yet.
#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>
#include "apps/studio-native/StudioSession.h"
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#ifndef AIVERSE_STUDIO_SOURCE_DIR
#define AIVERSE_STUDIO_SOURCE_DIR "."
#endif

@interface StudioHost : NSObject <NSApplicationDelegate, WKScriptMessageHandler, WKNavigationDelegate> {
    NSWindow* window_;
    WKWebView* web_;
    NSString* dist_;
    NSURL* devURL_;
    std::unique_ptr<aiverse::studio::StudioSession> session_;
}
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev;
@end

@implementation StudioHost
- (instancetype)initWithProject:(const std::string&)project dev:(BOOL)dev {
    if ((self=[super init])) {
        std::string root=AIVERSE_STUDIO_SOURCE_DIR;
        session_=std::make_unique<aiverse::studio::StudioSession>(root,project);
        NSURL* bundled=[[NSBundle mainBundle] URLForResource:@"index" withExtension:@"html" subdirectory:@"studio-web"];
        dist_= bundled ? [[[bundled URLByDeletingLastPathComponent] path] stringByStandardizingPath]
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
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    if (devURL_) [web_ loadRequest:[NSURLRequest requestWithURL:devURL_]];
    else {
        NSURL* root=[NSURL fileURLWithPath:dist_ isDirectory:YES];
        [web_ loadFileURL:[root URLByAppendingPathComponent:@"index.html"] allowingReadAccessToURL:root];
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
        std::string response=session_->dispatch(aiverse::Json::parse(bytes)).dump(-1);
        NSString* json=[[NSString alloc] initWithBytes:response.data() length:response.size() encoding:NSUTF8StringEncoding];
        if (json) [web_ evaluateJavaScript:[NSString stringWithFormat:@"window.__aiverseReceive && window.__aiverseReceive(%@);",json] completionHandler:nil];
    } catch (const std::exception& e) { NSLog(@"Studio bridge: %s",e.what()); }
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app { (void)app; return YES; }
- (void)applicationWillTerminate:(NSNotification*)note {
    (void)note;
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
