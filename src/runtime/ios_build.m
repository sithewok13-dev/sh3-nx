/* The build stamp under the open MENU tray (touch.c): "build 80 · Nov 9" from the SWBuildNumber and SWBuildDate
 * keys the cloud build writes into Info.plist; "build dev" for a build without them. */
#import <Foundation/Foundation.h>
#include <stdio.h>

const char *ios_build_stamp(void) {
    static char stamp[48];
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        NSDictionary *info = [[NSBundle mainBundle] infoDictionary];
        id num = info[@"SWBuildNumber"], date = info[@"SWBuildDate"];
        int y, m, d;
        if ([num isKindOfClass:[NSString class]] && [date isKindOfClass:[NSString class]] && [num length] &&
            sscanf([date UTF8String], "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 && d <= 31)
            snprintf(stamp, sizeof stamp, "build %.16s \xC2\xB7 %s %d", [num UTF8String], months[m - 1], d);
        else
            snprintf(stamp, sizeof stamp, "build dev");
    });
    return stamp;
}
