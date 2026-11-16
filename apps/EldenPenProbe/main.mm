// SPDX-License-Identifier: BSD-2-Clause-Patent
// Observe genuine UIKit touch values. No input injection, private HID writes,
// capability overrides, PencilKit/Metal dependency, or automatic installation.
#import <UIKit/UIKit.h>
#import <CoreGraphics/CoreGraphics.h>
typedef struct {CGPoint point;CGFloat width;NSUInteger stroke;BOOL pencil;} InkPoint;
@interface PenView : UIView
@property(nonatomic,strong) UILabel *status;
@end
@implementation PenView {
    InkPoint _ink[512];NSUInteger _next,_count,_stroke,_events;
    NSMapTable<UITouch *,NSNumber *> *_strokes;
    NSTimeInterval _lastLog;
}
- (instancetype)initWithFrame:(CGRect)frame {
    if((self=[super initWithFrame:frame])){
        self.backgroundColor=UIColor.whiteColor;self.multipleTouchEnabled=YES;
        _strokes=[NSMapTable weakToStrongObjectsMapTable];
        self.status=[[UILabel alloc] initWithFrame:CGRectZero];self.status.numberOfLines=0;
        self.status.font=[UIFont monospacedSystemFontOfSize:17 weight:UIFontWeightRegular];
        self.status.textColor=UIColor.blackColor;self.status.backgroundColor=[UIColor colorWithWhite:0.95 alpha:0.94];
        self.status.text=@"Elden Pen Probe\nWrite with the pen, lift it, then try a finger.\nWaiting for genuine UIKit events.";
        self.status.userInteractionEnabled=NO;[self addSubview:self.status];
    }return self;
}
- (void)layoutSubviews {
    [super layoutSubviews];UIEdgeInsets safe=self.safeAreaInsets;
    self.status.frame=CGRectMake(safe.left+16,safe.top+16,MAX(1,self.bounds.size.width-safe.left-safe.right-32),210);
}
- (void)record:(NSSet<UITouch *> *)touches event:(UIEvent *)event {
    for(UITouch *touch in touches){
        ++_events;BOOL pencil=touch.type==UITouchTypePencil;
        NSNumber *stroke=[_strokes objectForKey:touch];
        if(!stroke){stroke=@(++_stroke);[_strokes setObject:stroke forKey:touch];}
        CGPoint point=[touch locationInView:self];CGFloat maximum=touch.maximumPossibleForce;
        CGFloat fraction=maximum>0?MIN(1,MAX(0,touch.force/maximum)):0;
        NSUInteger coalesced=[event coalescedTouchesForTouch:touch].count;
        if(touch.phase==UITouchPhaseBegan||touch.phase==UITouchPhaseMoved){
            _ink[_next]=(InkPoint){point,pencil?1+7*fraction:2,stroke.unsignedIntegerValue,pencil};
            _next=(_next+1)%512;if(_count<512)++_count;
        }
        NSString *kind=pencil?@"PENCIL":touch.type==UITouchTypeDirect?@"FINGER":@"OTHER";
        self.status.text=[NSString stringWithFormat:
            @"Elden Pen Probe — %@\nPhase %ld   Events %lu   Coalesced %lu\nPosition %.1f, %.1f\nForce %.6f / %.6f\nAltitude %.6f   Azimuth %.6f\nTimestamp %.6f\nUIKit values only; a drawn line alone is not Pencil proof.",
            kind,(long)touch.phase,(unsigned long)_events,(unsigned long)coalesced,
            (double)point.x,(double)point.y,(double)touch.force,(double)maximum,
            (double)touch.altitudeAngle,(double)[touch azimuthAngleInView:self],touch.timestamp];
        if(touch.phase!=UITouchPhaseMoved||touch.timestamp-_lastLog>=0.5){
            NSLog(@"ELDEN_PEN_PROBE type=%@ phase=%ld force=%.6f max=%.6f altitude=%.6f azimuth=%.6f timestamp=%.6f coalesced=%lu",
                kind,(long)touch.phase,(double)touch.force,(double)maximum,(double)touch.altitudeAngle,
                (double)[touch azimuthAngleInView:self],touch.timestamp,(unsigned long)coalesced);
            _lastLog=touch.timestamp;
        }
        if(touch.phase==UITouchPhaseEnded||touch.phase==UITouchPhaseCancelled)[_strokes removeObjectForKey:touch];
    }
    [self setNeedsDisplay];
}
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {[self record:touches event:event];}
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {[self record:touches event:event];}
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {[self record:touches event:event];++_stroke;}
- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {[self record:touches event:event];++_stroke;}
- (void)drawRect:(CGRect)rect {
    (void)rect;CGContextRef context=UIGraphicsGetCurrentContext();if(!context)return;
    NSUInteger first=(_next+512-_count)%512;
    for(NSUInteger i=1;i<_count;++i){
        InkPoint a=_ink[(first+i-1)%512],b=_ink[(first+i)%512];if(a.stroke!=b.stroke||a.pencil!=b.pencil)continue;
        CGContextSetStrokeColorWithColor(context,(b.pencil?UIColor.systemBlueColor:UIColor.darkGrayColor).CGColor);
        CGContextSetLineWidth(context,b.width);CGContextSetLineCap(context,kCGLineCapRound);
        CGContextMoveToPoint(context,a.point.x,a.point.y);CGContextAddLineToPoint(context,b.point.x,b.point.y);CGContextStrokePath(context);
    }
}
@end
@interface PenController:UIViewController @end
@implementation PenController
- (void)loadView {self.view=[[PenView alloc] initWithFrame:CGRectZero];}
@end
@interface PenScene:NSObject<UIWindowSceneDelegate>
@property(nonatomic,strong) UIWindow *window;
@end
@implementation PenScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session;(void)options;if(![scene isKindOfClass:UIWindowScene.class])return;
    self.window=[[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController=[PenController new];[self.window makeKeyAndVisible];
}
@end
@interface PenApp:NSObject<UIApplicationDelegate> @end
@implementation PenApp
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {(void)application;(void)options;return YES;}
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)application;(void)options;UISceneConfiguration *configuration=[[UISceneConfiguration alloc] initWithName:@"Pen" sessionRole:session.role];
    configuration.delegateClass=PenScene.class;return configuration;
}
@end
int main(int argc,char **argv){@autoreleasepool{return UIApplicationMain(argc,argv,nil,NSStringFromClass(PenApp.class));}}
