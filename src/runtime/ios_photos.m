/* CAMERA's photos on iOS (touch.c): a frame, as RGBA rows top first, into the Photos library. The first save
 * asks for permission (Info.plist's NSPhotoLibraryAddUsageDescription). */
#import <UIKit/UIKit.h>

void touch_save_photo(const unsigned char *rgba, int w, int h) {
    size_t len = (size_t)w * h * 4;
    CFDataRef data = CFDataCreate(NULL, rgba, (CFIndex)len);
    CGDataProviderRef src = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGImageRef cg = CGImageCreate(w, h, 8, 32, (size_t)w * 4, rgb, kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault,
                                  src, NULL, false, kCGRenderingIntentDefault);
    UIImage *img = [UIImage imageWithCGImage:cg];
    CGImageRelease(cg);
    CGColorSpaceRelease(rgb);
    CGDataProviderRelease(src);
    CFRelease(data);
    dispatch_async(dispatch_get_main_queue(), ^{ UIImageWriteToSavedPhotosAlbum(img, nil, nil, nil); });
}
