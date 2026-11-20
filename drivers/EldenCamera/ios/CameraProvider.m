/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Copyright (c) 2026, UniXNU-gts7xlwifi contributors. All rights reserved.
 * Standard iPadOS17 CMIOExtension daemon/launchd structure follows T970 C6.
 * Elden real-frame ticket client replaces all pattern/carveout producers. */
#include <os/availability.h>
#undef API_UNAVAILABLE
#define API_UNAVAILABLE(...)
#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMediaIO/CMIOExtension.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include "CameraClient.h"
static unsigned target;
@interface EldenCameraStream : NSObject <CMIOExtensionStreamSource>
@property(nonatomic,strong) CMIOExtensionStream *stream;
@property(atomic,strong) NSArray<CMIOExtensionStreamFormat *> *formats;
- (BOOL)openInput;
@end
@implementation EldenCameraStream {
    pthread_t worker;bool joinable;uint32_t running;EldenCameraUserClient client;
    CVPixelBufferPoolRef pool;CMVideoFormatDescriptionRef format;
}
- (BOOL)openInput {return !elden_camera_client_open(&client,target);}
- (NSSet<CMIOExtensionProperty> *)availableProperties {
    return [NSSet setWithObjects:CMIOExtensionPropertyStreamActiveFormatIndex,CMIOExtensionPropertyStreamFrameDuration,nil];
}
- (CMIOExtensionStreamProperties *)streamPropertiesForProperties:(NSSet<CMIOExtensionProperty> *)keys error:(NSError **)error {
    (void)error;CMIOExtensionStreamProperties *p=[CMIOExtensionStreamProperties streamPropertiesWithDictionary:@{}];
    if([keys containsObject:CMIOExtensionPropertyStreamActiveFormatIndex])p.activeFormatIndex=@0;
    if([keys containsObject:CMIOExtensionPropertyStreamFrameDuration])p.frameDuration=CFBridgingRelease(CMTimeCopyAsDictionary(CMTimeMake(1,15),kCFAllocatorDefault));return p;
}
- (BOOL)setStreamProperties:(CMIOExtensionStreamProperties *)p error:(NSError **)error {
    (void)error;return !p.activeFormatIndex||p.activeFormatIndex.unsignedIntegerValue==0;
}
- (BOOL)authorizedToStartStreamForClient:(CMIOExtensionClient *)c {(void)c;return YES;} // actual OS TCC/AVF authorizes its client
static void *camera_worker(void *arg);
- (BOOL)startStreamAndReturnError:(NSError **)error {
    if(__atomic_load_n(&running,__ATOMIC_ACQUIRE))return YES;
    if(joinable){pthread_join(worker,NULL);joinable=false;}
    if(!client.connection){int rc=elden_camera_client_open(&client,target);if(rc){if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:-rc userInfo:nil];return NO;}}
    if(!pool){NSDictionary *attrs=@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA),(id)kCVPixelBufferWidthKey:@720,
        (id)kCVPixelBufferHeightKey:@960,(id)kCVPixelBufferIOSurfacePropertiesKey:@{}};
        if(CVPixelBufferPoolCreate(kCFAllocatorDefault,(__bridge CFDictionaryRef)@{(id)kCVPixelBufferPoolMinimumBufferCountKey:@3},(__bridge CFDictionaryRef)attrs,&pool))return NO;}
    __atomic_store_n(&running,1u,__ATOMIC_RELEASE);
    if(pthread_create(&worker,NULL,camera_worker,(__bridge void *)self)){__atomic_store_n(&running,0u,__ATOMIC_RELEASE);return NO;}joinable=true;return YES;
}
- (BOOL)stopStreamAndReturnError:(NSError **)error {
    (void)error;__atomic_store_n(&running,0u,__ATOMIC_RELEASE);if(joinable){pthread_join(worker,NULL);joinable=false;}return YES;
}
- (void)runCapture {
    @autoreleasepool {
        while(__atomic_load_n(&running,__ATOMIC_ACQUIRE)){
            CVPixelBufferRef pixel=NULL;if(CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault,pool,&pixel)||!pixel){usleep(3000);continue;}
            if(CVPixelBufferLockBaseAddress(pixel,0)){CVPixelBufferRelease(pixel);continue;}
            size_t stride=CVPixelBufferGetBytesPerRow(pixel);uint64_t ns=0;
            int rc=elden_camera_client_frame(&client,CVPixelBufferGetBaseAddress(pixel),CVPixelBufferGetDataSize(pixel),stride,&ns);
            CVPixelBufferUnlockBaseAddress(pixel,0);
            if(rc){CVPixelBufferRelease(pixel);if(rc==-11){usleep(3000);continue;}fprintf(stderr,"eldencamerad sensor%u frame error%d\n",target,rc);break;}
            if(!format&&CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault,pixel,&format)){CVPixelBufferRelease(pixel);break;}
            if(ns>INT64_MAX){CVPixelBufferRelease(pixel);break;}
            CMSampleTimingInfo timing={CMTimeMake(1,15),CMTimeMake((int64_t)ns,1000000000),kCMTimeInvalid};CMSampleBufferRef sample=NULL;
            OSStatus st=CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault,pixel,true,NULL,NULL,format,&timing,&sample);CVPixelBufferRelease(pixel);
            if(st||!sample)break;[self.stream sendSampleBuffer:sample discontinuity:CMIOExtensionStreamDiscontinuityFlagNone hostTimeInNanoseconds:ns];CFRelease(sample);
        }
        __atomic_store_n(&running,0u,__ATOMIC_RELEASE);
    }
}
static void *camera_worker(void *arg){@autoreleasepool{[(__bridge EldenCameraStream *)arg runCapture];}return NULL;}
@end
@interface EldenCameraDevice : NSObject <CMIOExtensionDeviceSource>
@property(nonatomic,strong) CMIOExtensionDevice *device;
@property(nonatomic,strong) EldenCameraStream *streamSource;
@end
@implementation EldenCameraDevice
- (NSSet<CMIOExtensionProperty> *)availableProperties {return [NSSet setWithObjects:CMIOExtensionPropertyDeviceTransportType,CMIOExtensionPropertyDeviceModel,nil];}
- (CMIOExtensionDeviceProperties *)devicePropertiesForProperties:(NSSet<CMIOExtensionProperty> *)keys error:(NSError **)error {
    (void)error;CMIOExtensionDeviceProperties *p=[CMIOExtensionDeviceProperties devicePropertiesWithDictionary:@{}];
    if([keys containsObject:CMIOExtensionPropertyDeviceTransportType])p.transportType=@(0x75736220);
    if([keys containsObject:CMIOExtensionPropertyDeviceModel])p.model=target?@"Elden Front Camera":@"Elden Rear Camera";return p;
}
- (BOOL)setDeviceProperties:(CMIOExtensionDeviceProperties *)p error:(NSError **)error {(void)p;(void)error;return YES;}
@end
@interface EldenCameraProvider : NSObject <CMIOExtensionProviderSource>
@property(nonatomic,strong) CMIOExtensionProvider *provider;
@property(nonatomic,strong) EldenCameraDevice *deviceSource;
@end
@implementation EldenCameraProvider
- (NSSet<CMIOExtensionProperty> *)availableProperties {return [NSSet setWithObject:CMIOExtensionPropertyProviderManufacturer];}
- (CMIOExtensionProviderProperties *)providerPropertiesForProperties:(NSSet<CMIOExtensionProperty> *)keys error:(NSError **)error {
    (void)error;CMIOExtensionProviderProperties *p=[CMIOExtensionProviderProperties providerPropertiesWithDictionary:@{}];
    if([keys containsObject:CMIOExtensionPropertyProviderManufacturer])p.manufacturer=@"UniXNU";return p;
}
- (BOOL)setProviderProperties:(CMIOExtensionProviderProperties *)p error:(NSError **)error {(void)p;(void)error;return YES;}
- (BOOL)connectClient:(CMIOExtensionClient *)c error:(NSError **)error {(void)c;(void)error;return YES;}
- (void)disconnectClient:(CMIOExtensionClient *)c {(void)c;}
@end
int main(int argc,char **argv){@autoreleasepool{
    if(argc!=2||(strcmp(argv[1],"0")&&strcmp(argv[1],"1")))return 2;target=argv[1][0]=='1';
    NSString *name=target?@"Elden Front Camera":@"Elden Rear Camera";
    EldenCameraProvider *p=[EldenCameraProvider new];p.provider=[[CMIOExtensionProvider alloc]initWithSource:p clientQueue:nil];
    EldenCameraDevice *d=[EldenCameraDevice new];EldenCameraStream *s=[EldenCameraStream new];
    if(![s openInput])return 75; // no CMIO device is published without actual native camera readiness
    NSUUID *device=[[NSUUID alloc]initWithUUIDString:target?@"C50E7DED-BC56-4AD3-85EA-501CA0000001":@"C50E7DED-BC56-4AD3-85EA-501CA0000000"];
    NSUUID *stream=[[NSUUID alloc]initWithUUIDString:target?@"C50E7DED-BC56-4AD3-85EA-501CA0000101":@"C50E7DED-BC56-4AD3-85EA-501CA0000100"];
    d.device=[[CMIOExtensionDevice alloc]initWithLocalizedName:name deviceID:device legacyDeviceID:nil source:d];
    CMVideoFormatDescriptionRef fd=NULL;if(CMVideoFormatDescriptionCreate(kCFAllocatorDefault,kCVPixelFormatType_32BGRA,720,960,NULL,&fd))return 1;
    s.formats=@[[[CMIOExtensionStreamFormat alloc]initWithFormatDescription:fd maxFrameDuration:CMTimeMake(1,5) minFrameDuration:CMTimeMake(1,15) validFrameDurations:nil]];CFRelease(fd);
    s.stream=[[CMIOExtensionStream alloc]initWithLocalizedName:[name stringByAppendingString:@".video"] streamID:stream direction:CMIOExtensionStreamDirectionSource clockType:CMIOExtensionStreamClockTypeHostTime source:s];
    d.streamSource=s;p.deviceSource=d;NSError *error=nil;
    if(![d.device addStream:s.stream error:&error]||![p.provider addDevice:d.device error:&error])return 1;
    [CMIOExtensionProvider startServiceWithProvider:p.provider];CFRunLoopRun();
}return 0;}
