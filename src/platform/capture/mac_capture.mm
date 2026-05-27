#include "platform/capture/mac_capture.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "platform/capture/bitmap_writer.hpp"

namespace kiseki::platform::capture {

namespace {

CaptureResult fail_capture(const std::filesystem::path& path, std::string error) {
    return CaptureResult{
        .ok = false,
        .code = 2,
        .output_path = path,
        .width = 0,
        .height = 0,
        .error = std::move(error),
    };
}

std::string ns_error_string(NSError* error) {
    if (error == nil) {
        return {};
    }
    NSString* text = [error localizedDescription];
    return text == nil ? "ScreenCaptureKit failed" : std::string{[text UTF8String]};
}

CaptureResult write_cg_image_bmp(CGImageRef image, const std::filesystem::path& output_path) {
    if (image == nullptr) {
        return fail_capture(output_path, "ScreenCaptureKit returned no image");
    }

    const auto width = static_cast<int>(CGImageGetWidth(image));
    const auto height = static_cast<int>(CGImageGetHeight(image));
    if (width <= 0 || height <= 0) {
        return fail_capture(output_path, "captured image has empty dimensions");
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    if (color_space == nullptr) {
        return fail_capture(output_path, "CGColorSpaceCreateDeviceRGB failed");
    }

    CGContextRef context = CGBitmapContextCreate(
        pixels.data(),
        static_cast<std::size_t>(width),
        static_cast<std::size_t>(height),
        8,
        static_cast<std::size_t>(width) * 4U,
        color_space,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(color_space);
    if (context == nullptr) {
        return fail_capture(output_path, "CGBitmapContextCreate failed");
    }

    CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(height));
    CGContextScaleCTM(context, 1.0, -1.0);
    CGContextDrawImage(context, CGRectMake(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), image);
    CGContextRelease(context);

    return write_bgra_bmp(output_path, width, height, pixels);
}

SCShareableContent* load_shareable_content(std::string& error) {
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block SCShareableContent* content = nil;
    __block NSError* captured_error = nil;

    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                               onScreenWindowsOnly:YES
                                                completionHandler:^(SCShareableContent* shareableContent, NSError* blockError) {
                                                    content = shareableContent;
                                                    captured_error = blockError;
                                                    dispatch_semaphore_signal(semaphore);
                                                }];

    const long wait_result = dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 15LL * NSEC_PER_SEC));
    if (wait_result != 0) {
        error = "ScreenCaptureKit shareable content request timed out";
        return nil;
    }
    if (captured_error != nil) {
        error = ns_error_string(captured_error);
        return nil;
    }
    if (content == nil) {
        error = "ScreenCaptureKit returned no shareable content";
        return nil;
    }
    return content;
}

struct ImageResult {
    CGImageRef image = nullptr;
    std::string error;
};

ImageResult capture_rect(CGRect rect) {
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block CGImageRef image = nullptr;
    __block NSError* captured_error = nil;

    [SCScreenshotManager captureImageInRect:rect
                          completionHandler:^(CGImageRef blockImage, NSError* blockError) {
                              if (blockImage != nullptr) {
                                  image = CGImageRetain(blockImage);
                              }
                              captured_error = blockError;
                              dispatch_semaphore_signal(semaphore);
                          }];

    const long wait_result = dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 15LL * NSEC_PER_SEC));
    if (wait_result != 0) {
        return ImageResult{nullptr, "ScreenCaptureKit screenshot request timed out"};
    }
    if (captured_error != nil) {
        return ImageResult{nullptr, ns_error_string(captured_error)};
    }
    if (image == nullptr) {
        return ImageResult{nullptr, "ScreenCaptureKit returned no image"};
    }
    return ImageResult{image, ""};
}

ImageResult capture_filter(SCContentFilter* filter, SCStreamConfiguration* configuration) {
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block CGImageRef image = nullptr;
    __block NSError* captured_error = nil;

    [SCScreenshotManager captureImageWithFilter:filter
                                  configuration:configuration
                              completionHandler:^(CGImageRef blockImage, NSError* blockError) {
                                  if (blockImage != nullptr) {
                                      image = CGImageRetain(blockImage);
                                  }
                                  captured_error = blockError;
                                  dispatch_semaphore_signal(semaphore);
                              }];

    const long wait_result = dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 15LL * NSEC_PER_SEC));
    if (wait_result != 0) {
        return ImageResult{nullptr, "ScreenCaptureKit screenshot request timed out"};
    }
    if (captured_error != nil) {
        return ImageResult{nullptr, ns_error_string(captured_error)};
    }
    if (image == nullptr) {
        return ImageResult{nullptr, "ScreenCaptureKit returned no image"};
    }
    return ImageResult{image, ""};
}

CGRect union_display_rect(SCShareableContent* content) {
    CGRect rect = CGRectNull;
    for (SCDisplay* display in content.displays) {
        rect = CGRectIsNull(rect) ? display.frame : CGRectUnion(rect, display.frame);
    }
    return rect;
}

SCWindow* find_window(SCShareableContent* content, std::uint32_t window_id) {
    for (SCWindow* window in content.windows) {
        if (window.windowID == window_id) {
            return window;
        }
    }
    return nil;
}

SCStreamConfiguration* window_configuration(SCContentFilter* filter) {
    SCStreamConfiguration* configuration = [[SCStreamConfiguration alloc] init];
    configuration.pixelFormat = kCVPixelFormatType_32BGRA;
    configuration.showsCursor = NO;
    configuration.scalesToFit = NO;
    configuration.queueDepth = 1;
    configuration.ignoreShadowsSingleWindow = YES;
    configuration.ignoreGlobalClipSingleWindow = YES;
    configuration.shouldBeOpaque = YES;
    configuration.captureResolution = SCCaptureResolutionBest;

    const CGFloat scale = std::max<CGFloat>(filter.pointPixelScale, 1.0);
    const CGRect content_rect = filter.contentRect;
    configuration.width = static_cast<std::size_t>(std::max<CGFloat>(1.0, std::ceil(content_rect.size.width * scale)));
    configuration.height = static_cast<std::size_t>(std::max<CGFloat>(1.0, std::ceil(content_rect.size.height * scale)));
    return configuration;
}

}

CaptureResult capture_desktop_bmp_screencapturekit(const std::filesystem::path& output_path) {
    @autoreleasepool {
        std::string error;
        SCShareableContent* content = load_shareable_content(error);
        if (content == nil) {
            return fail_capture(output_path, error);
        }

        const CGRect rect = union_display_rect(content);
        if (CGRectIsNull(rect) || rect.size.width <= 0.0 || rect.size.height <= 0.0) {
            return fail_capture(output_path, "ScreenCaptureKit returned no display bounds");
        }

        const auto image = capture_rect(rect);
        if (image.image == nullptr) {
            return fail_capture(output_path, image.error);
        }
        const auto result = write_cg_image_bmp(image.image, output_path);
        CGImageRelease(image.image);
        return result;
    }
}

CaptureResult capture_window_bmp_screencapturekit(std::uint32_t window_id, const std::filesystem::path& output_path) {
    @autoreleasepool {
        std::string error;
        SCShareableContent* content = load_shareable_content(error);
        if (content == nil) {
            return fail_capture(output_path, error);
        }

        SCWindow* window = find_window(content, window_id);
        if (window == nil) {
            return fail_capture(output_path, "ScreenCaptureKit target window not found");
        }

        SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
        SCStreamConfiguration* configuration = window_configuration(filter);
        const auto image = capture_filter(filter, configuration);
        if (image.image == nullptr) {
            return fail_capture(output_path, image.error);
        }
        const auto result = write_cg_image_bmp(image.image, output_path);
        CGImageRelease(image.image);
        return result;
    }
}

}
