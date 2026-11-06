/* Opt-in S9+ native pixel canvas. Keep the existing apparent UI size while
 * extending the logical width for the full 1440 x 2960 panel. The earlier
 * padded 1368 x 2960 experiment remains supported for reproducible comparisons.
 */
#include <CoreGraphics/CGGeometry.h>
#include <CoreGraphics/CGImage.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

static id (*original_init)(id, SEL, uint64_t, uint64_t, uint64_t, double, double, long, long, long, BOOL, long);
static CGSize (*original_size)(id, SEL);
static SEL width_selector, height_selector;
static CGRect (*original_bounds)(id, SEL), (*original_private_bounds)(id, SEL);
static CGImageRef (*original_raster)(id, SEL, double, CGSize);
static Class (*original_status_provider)(id, SEL, id, id);
static Class flat_status_provider;
typedef struct { double top, leading, bottom, trailing; } NativeDirectionalInsets;
static NativeDirectionalInsets (*original_status_insets)(id, SEL);
static BOOL full_panel_profile;
static BOOL integral_point_grid;
static double canvas_height = 2960.0;
static void (*original_keyboard_layout)(id, SEL);
static void (*original_emoji_layout)(id, SEL);
static void (*previous_uncaught_exception)(id);
static SEL bounds_selector, private_bounds_selector, current_mode_selector, mode_size_selector;
static unsigned raster_calls, bounds_calls, mode_calls, status_calls, keyboard_calls;
static unsigned keyboard_tree_calls, emoji_calls;

static void
say(const char *format, ...)
{
	char line[512];
	va_list list;
	va_start(list, format);
	int length = vsnprintf(line, sizeof(line), format, list);
	va_end(list);
	if (length <= 0) return;
	int fd = open("/dev/console", O_WRONLY | O_NOCTTY);
	if (fd >= 0) {
		(void)write(fd, line, (size_t)length < sizeof(line) ? (size_t)length : sizeof(line)-1);
		close(fd);
	}
}

static BOOL
supported_pixels(double width, double height)
{
	if (canvas_height == 2220)
		return full_panel_profile && width == 1080 && height == 2220;
	if (canvas_height == 2442)
		return full_panel_profile && width == 1188 && height == 2442;
	return (width == 1368 || width == 1440) && height == 2960;
}

static BOOL
native_mode(id object)
{
	uint64_t width = ((uint64_t (*)(id, SEL))objc_msgSend)(object, width_selector);
	return supported_pixels(width, ((uint64_t (*)(id, SEL))objc_msgSend)(object, height_selector));
}

static double
logical_width(double pixels)
{
	return (pixels == 1440 || pixels == 1080 || pixels == 1188) ? (integral_point_grid ? 396.0 : pixels * 812.0 / canvas_height) : 375.0;
}

static double logical_height(void) { return integral_point_grid ? 814.0 : 812.0; }
static double native_density(void) { return canvas_height / logical_height(); }

#include "keyboard_native.inc"

static id
native_init(id self, SEL command, uint64_t width, uint64_t height, uint64_t preferred,
    double override, double refresh, long gamut, long hdr, long rotation, BOOL virtual_mode, long validity)
{
	if (supported_pixels(width, height)) {
		preferred = 3;  /* retain the existing artwork family */
		override = native_density();
		if (__atomic_fetch_add(&mode_calls, 1, __ATOMIC_RELAXED) < 2)
			say("fb_nativeui: %s: mode %llux%llu, layout %.9f x %.9f points, density %.9f\n",
			    getprogname(), (unsigned long long)width, (unsigned long long)height,
			    logical_width((double)width), logical_height(), override);
	}
	return original_init(self, command, width, height, preferred, override, refresh,
	    gamut, hdr, rotation, virtual_mode, validity);
}

static CGSize
native_size(id self, SEL command)
{
	CGSize result = original_size(self, command);
	if (native_mode(self)) {
		double points = logical_width((double)((uint64_t (*)(id, SEL))objc_msgSend)(self, width_selector));
		return result.width > result.height ? (CGSize){logical_height(), points} : (CGSize){points, logical_height()};
	}
	return result;
}

static CGRect
native_bounds(id self, SEL command)
{
	CGRect result = command == private_bounds_selector ? original_private_bounds(self, command) : original_bounds(self, command);
	id mode = ((id (*)(id, SEL))objc_msgSend)(self, current_mode_selector);
	if (mode != nil) {
		CGSize pixels = ((CGSize (*)(id, SEL))objc_msgSend)(mode, mode_size_selector);
		double short_side = fmin(pixels.width, pixels.height), long_side = fmax(pixels.width, pixels.height);
		if (supported_pixels(short_side, long_side)) {
			double points = logical_width(short_side);
			result.size = result.size.width > result.size.height ? (CGSize){logical_height(), points} : (CGSize){points, logical_height()};
			if (__atomic_fetch_add(&bounds_calls, 1, __ATOMIC_RELAXED) < 4)
				say("fb_nativeui: %s: UIScreen %s = %.9f x %.9f points\n", getprogname(),
				    sel_getName(command), result.size.width, result.size.height);
		}
	}
	return result;
}

static CGImageRef
native_vector_raster(id self, SEL command, double scale, CGSize target)
{
	CGSize requested = target;
	/* This firmware's CoreUI truncates pixel width and pixel width * bytes/pixel
	 * separately when creating its bitmap. A fractional width can therefore give
	 * an RGBA stride that is not divisible by four. Round the pixel dimensions,
	 * retaining the display density and UIKit's point-space layout. The small
	 * positive bias prevents division/multiplication rounding just below an integer
	 * before CoreUI's truncating conversions (fcvtzu at 0x19ee54c3c/4c).
	 */
	if ((fabs(scale - 2960.0/812.0) < 0.000001 || fabs(scale - 2960.0/814.0) < 0.000001 ||
	    fabs(scale - native_density()) < 0.000001) && isfinite(target.width) &&
	    isfinite(target.height) && target.width > 0 && target.height > 0) {
		target.width = (ceil(target.width * scale) + 0.000001) / scale;
		target.height = (ceil(target.height * scale) + 0.000001) / scale;
	}
	if (__atomic_fetch_add(&raster_calls, 1, __ATOMIC_RELAXED) < 8) {
		say("fb_nativeui: %s: vector raster scale %.9f target %.9f x %.9f -> %.9f x %.9f\n",
		    getprogname(), scale, requested.width, requested.height, target.width, target.height);
	}
	return original_raster(self, command, scale, target);
}

static Class
native_status_provider(id self, SEL command, id screen, id info)
{
	if (screen && flat_status_provider) {
		id mode = ((id (*)(id, SEL))objc_msgSend)(screen, current_mode_selector);
		if (mode) {
			CGSize pixels = ((CGSize (*)(id, SEL))objc_msgSend)(mode, mode_size_selector);
			if (full_panel_profile && supported_pixels(fmin(pixels.width, pixels.height), fmax(pixels.width, pixels.height))) {
				if (__atomic_fetch_add(&status_calls, 1, __ATOMIC_RELAXED) < 2)
					say("fb_nativeui: %s: flat-panel status bar provider %s\n", getprogname(), class_getName(flat_status_provider));
				return flat_status_provider;
			}
		}
	}
	return original_status_provider(self, command, screen, info);
}

static NativeDirectionalInsets
native_status_insets(id self, SEL command)
{
	NativeDirectionalInsets result = original_status_insets(self, command);
	if (full_panel_profile) {
		/* Keep carrier and battery clear of the S9+'s curved top corners. */
		result.leading = fmax(result.leading, 18.0);
		result.trailing = fmax(result.trailing, 18.0);
	}
	return result;
}

static void native_uncaught_exception(id exception)
{
	id name = ((id (*)(id, SEL))objc_msgSend)(exception, sel_registerName("name"));
	id reason = ((id (*)(id, SEL))objc_msgSend)(exception, sel_registerName("reason"));
	const char *name_text = name ? ((const char *(*)(id, SEL))objc_msgSend)(name, sel_registerName("UTF8String")) : "?";
	const char *reason_text = reason ? ((const char *(*)(id, SEL))objc_msgSend)(reason, sel_registerName("UTF8String")) : "?";
	say("fb_nativeui: %s: uncaught exception %s\n", getprogname(), name_text ? name_text : "?");
	say("fb_nativeui: %s: uncaught reason %s\n", getprogname(), reason_text ? reason_text : "?");
	if (previous_uncaught_exception) previous_uncaught_exception(exception);
}

static void native_emoji_layout(id self, SEL command)
{
	unsigned count = __atomic_fetch_add(&emoji_calls, 1, __ATOMIC_RELAXED);
	if (count < 16) {
		CGRect frame = ((CGRect (*)(id, SEL))objc_msgSend)(self, sel_registerName("frame"));
		CGRect bounds = ((CGRect (*)(id, SEL))objc_msgSend)(self, bounds_selector);
		CGSize content = ((CGSize (*)(id, SEL))objc_msgSend)(self, sel_registerName("contentSize"));
		double scale = ((double (*)(id, SEL))objc_msgSend)(self, sel_registerName("_currentScreenScale"));
		say("fb_nativeui: %s: emoji layout %u frame %.6f,%.6f %.6fx%.6f bounds %.6f,%.6f %.6fx%.6f content %.6fx%.6f scale %.9f\n",
		    getprogname(), count, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height,
		    bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height, content.width, content.height, scale);
	}
	original_emoji_layout(self, command);
}

static void
native_keyboard_layout(id self, SEL command)
{
	original_keyboard_layout(self, command);
	id parent = ((id (*)(id, SEL))objc_msgSend)(self, sel_registerName("superview"));
	if (!parent) return;
	CGRect frame = ((CGRect (*)(id, SEL))objc_msgSend)(self, sel_registerName("frame"));
	CGRect host = ((CGRect (*)(id, SEL))objc_msgSend)(parent, sel_registerName("bounds"));
	if (frame.size.height > 100 && __atomic_fetch_add(&keyboard_calls, 1, __ATOMIC_RELAXED) < 16)
		say("fb_nativeui: %s: keyboard %s frame (%.3f,%.3f) %.3fx%.3f host %.3fx%.3f\n",
		    getprogname(), object_getClassName(self), frame.origin.x, frame.origin.y,
		    frame.size.width, frame.size.height, host.size.width, host.size.height);
	if (frame.size.height > 100 && __atomic_fetch_add(&keyboard_tree_calls, 1, __ATOMIC_RELAXED) < 2) {
		id node = self;
		for (unsigned depth = 0; node && depth < 6; ++depth) {
			CGRect f = ((CGRect (*)(id, SEL))objc_msgSend)(node, sel_registerName("frame"));
			CGRect b = ((CGRect (*)(id, SEL))objc_msgSend)(node, bounds_selector);
			say("fb_nativeui: %s: keyboard ancestor %u %s frame %.3f,%.3f %.3fx%.3f bounds %.3f,%.3f %.3fx%.3f\n",
			    getprogname(), depth, object_getClassName(node), f.origin.x, f.origin.y, f.size.width, f.size.height,
			    b.origin.x, b.origin.y, b.size.width, b.size.height);
			node = ((id (*)(id, SEL))objc_msgSend)(node, sel_registerName("superview"));
		}
	}
}

__attribute__((constructor)) static void
install_native_display(void)
{
	char args[1024] = "";
	size_t size = sizeof(args)-1;
	if (sysctlbyname("kern.bootargs", args, &size, NULL, 0) != 0) return;
	const char *arg = strstr(args, "fb_nativeui=");
	if (arg == NULL || (arg != args && arg[-1] != ' ') || (arg[12] != '1' && arg[12] != '2') ||
	    (arg[13] != '\0' && arg[13] != ' ')) return;
	const char *width_arg = strstr(args, "fbwidth=1440");
	full_panel_profile = width_arg && (width_arg == args || width_arg[-1] == ' ') &&
	    (width_arg[12] == '\0' || width_arg[12] == ' ');
	const char *fhd_width = strstr(args, "fbwidth=1080");
	const char *fhd_height = strstr(args, "fbheight=2220");
	if (fhd_width && (fhd_width == args || fhd_width[-1] == ' ') &&
	    (fhd_width[12] == '\0' || fhd_width[12] == ' ') &&
	    fhd_height && (fhd_height == args || fhd_height[-1] == ' ') &&
	    (fhd_height[13] == '\0' || fhd_height[13] == ' ')) {
		full_panel_profile = YES;
		canvas_height = 2220.0;
	}
	const char *integer_width = strstr(args, "fbwidth=1188");
	const char *integer_height = strstr(args, "fbheight=2442");
	if (integer_width && (integer_width == args || integer_width[-1] == ' ') &&
	    (integer_width[12] == '\0' || integer_width[12] == ' ') &&
	    integer_height && (integer_height == args || integer_height[-1] == ' ') &&
	    (integer_height[13] == '\0' || integer_height[13] == ' ')) {
		full_panel_profile = YES;
		canvas_height = 2442.0;
	}
	integral_point_grid = full_panel_profile && arg[12] == '2';
	say("fb_nativeui: %s: constructor entered\n", getprogname());
	if (dlopen("/System/Library/PrivateFrameworks/FrontBoardServices.framework/FrontBoardServices", RTLD_NOW) == NULL) {
		say("fb_nativeui: %s: FrontBoardServices unavailable\n", getprogname());
		return;
	}
	Class cls = objc_getClass("FBSDisplayMode");
	SEL init = sel_registerName("_initWithWidth:height:preferredScale:scaleOverride:refreshRate:gamut:hdr:rotation:virtual:validityCheck:");
	SEL get_size = sel_registerName("size");
	width_selector = sel_registerName("_caWidth");
	height_selector = sel_registerName("_caHeight");
	Method initializer = cls ? class_getInstanceMethod(cls, init) : NULL;
	Method dimensions = cls ? class_getInstanceMethod(cls, get_size) : NULL;
	Method width = cls ? class_getInstanceMethod(cls, width_selector) : NULL;
	Method height = cls ? class_getInstanceMethod(cls, height_selector) : NULL;
	if (initializer == NULL || dimensions == NULL || width == NULL || height == NULL ||
	    strcmp(method_getTypeEncoding(initializer), "@92@0:8Q16Q24Q32d40d48q56q64q72B80q84") != 0 ||
	    strcmp(method_getTypeEncoding(dimensions), "{CGSize=dd}16@0:8") != 0 ||
	    strcmp(method_getTypeEncoding(width), "Q16@0:8") != 0 || strcmp(method_getTypeEncoding(height), "Q16@0:8") != 0) {
		say("fb_nativeui: %s: unexpected display ABI; hooks not installed\n", getprogname());
		return;
	}
	original_size = (CGSize (*)(id, SEL))method_getImplementation(dimensions);
	original_init = (void *)method_getImplementation(initializer);
	method_setImplementation(dimensions, (IMP)native_size);
	method_setImplementation(initializer, (IMP)native_init);
	/* UIScreen derives bounds from pixels/scale rather than calling FBSDisplayMode.size.
	 * Override both entry points while leaving nativeBounds and the display scale intact.
	 * Only use UIKit if the host already loaded it. Linking UIKit into backboardd
	 * introduces a startup cycle before this constructor can run. */
	Class screen = objc_getClass("UIScreen");
	bounds_selector = sel_registerName("bounds");
	private_bounds_selector = sel_registerName("_bounds");
	current_mode_selector = sel_registerName("currentMode");
	mode_size_selector = sel_registerName("size");
	Method bounds = screen ? class_getInstanceMethod(screen, bounds_selector) : NULL;
	Method private_bounds = screen ? class_getInstanceMethod(screen, private_bounds_selector) : NULL;
	Method current_mode = screen ? class_getInstanceMethod(screen, current_mode_selector) : NULL;
	Class mode_class = objc_getClass("UIScreenMode");
	Method mode_size = mode_class ? class_getInstanceMethod(mode_class, mode_size_selector) : NULL;
	if (bounds && current_mode && mode_size &&
	    strcmp(method_getTypeEncoding(bounds), "{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8") == 0 &&
	    strcmp(method_getTypeEncoding(mode_size), "{CGSize=dd}16@0:8") == 0) {
		original_bounds = (CGRect (*)(id, SEL))method_getImplementation(bounds);
		method_setImplementation(bounds, (IMP)native_bounds);
		if (private_bounds && strcmp(method_getTypeEncoding(private_bounds), method_getTypeEncoding(bounds)) == 0) {
			original_private_bounds = (CGRect (*)(id, SEL))method_getImplementation(private_bounds);
			method_setImplementation(private_bounds, (IMP)native_bounds);
		}
		say("fb_nativeui: %s: UIScreen point bounds hooks installed\n", getprogname());
	} else {
		say("fb_nativeui: %s: UIScreen bounds ABI unavailable\n", getprogname());
	}
	Class vector = objc_getClass("CUINamedVectorPDFImage");
	Method raster = vector ? class_getInstanceMethod(vector,
	    sel_registerName("rasterizeImageUsingScaleFactor:forTargetSize:")) : NULL;
	if (raster && strcmp(method_getTypeEncoding(raster), "^{CGImage=}40@0:8d16{CGSize=dd}24") == 0) {
		original_raster = (CGImageRef (*)(id, SEL, double, CGSize))method_getImplementation(raster);
		method_setImplementation(raster, (IMP)native_vector_raster);
		say("fb_nativeui: %s: CoreUI vector raster density hook installed\n", getprogname());
	}
	/* UIKit's preset status-bar selector rejects unlisted screen widths/scales
	 * and displays its red rdar:45025538 fallback. The S9+ has a flat, unnotched
	 * panel: select UIKit's adaptive legacy-phone layout for this exact mode.
	 * This changes the status bar's provider, never the screen geometry. */
	Class status = objc_getClass("_UIStatusBarVisualProvider_iOS");
	flat_status_provider = objc_getClass("_UIStatusBarVisualProvider_LegacyPhone");
	Method provider = status ? class_getClassMethod(status,
	    sel_registerName("visualProviderSubclassForScreen:visualProviderInfo:")) : NULL;
	if (provider && flat_status_provider &&
	    strcmp(method_getTypeEncoding(provider), "#32@0:8@16@24") == 0) {
		original_status_provider = (Class (*)(id, SEL, id, id))method_getImplementation(provider);
		method_setImplementation(provider, (IMP)native_status_provider);
		say("fb_nativeui: %s: full-panel status bar selection hook installed\n", getprogname());
	}
	Method insets = flat_status_provider ? class_getInstanceMethod(flat_status_provider, sel_registerName("edgeInsets")) : NULL;
	if (full_panel_profile && insets && strcmp(method_getTypeEncoding(insets), "{NSDirectionalEdgeInsets=dddd}16@0:8") == 0) {
		original_status_insets = (NativeDirectionalInsets (*)(id, SEL))method_getImplementation(insets);
		method_setImplementation(insets, (IMP)native_status_insets);
		say("fb_nativeui: %s: status bar horizontal insets at least 18 points\n", getprogname());
	}
	if (integral_point_grid) {
		Class keyboard = objc_getClass("UIKeyboardImpl");
		SEL layout = sel_registerName("layoutSubviews");
		Method key_method = keyboard ? class_getInstanceMethod(keyboard, layout) : NULL;
		if (key_method && strcmp(method_getTypeEncoding(key_method), "v16@0:8") == 0) {
			original_keyboard_layout = (void (*)(id, SEL))method_getImplementation(key_method);
			class_replaceMethod(keyboard, layout, (IMP)native_keyboard_layout, method_getTypeEncoding(key_method));
		}
		install_native_keyboard_sizes();
		if (strstr(args, " kbtrace=1") != NULL) install_native_keyboard_trace();
		Class emoji = objc_getClass("UIKeyboardEmojiCollectionView");
		Method emoji_layout = emoji ? class_getInstanceMethod(emoji, layout) : NULL;
		if (emoji_layout && strcmp(method_getTypeEncoding(emoji_layout), "v16@0:8") == 0) {
			original_emoji_layout = (void (*)(id, SEL))method_getImplementation(emoji_layout);
			class_replaceMethod(emoji, layout, (IMP)native_emoji_layout, method_getTypeEncoding(emoji_layout));
		}
		void (*(*get_handler)(void))(id) = dlsym(RTLD_DEFAULT, "NSGetUncaughtExceptionHandler");
		void (*set_handler)(void (*)(id)) = dlsym(RTLD_DEFAULT, "NSSetUncaughtExceptionHandler");
		if (get_handler && set_handler) {
			previous_uncaught_exception = get_handler();
			set_handler(native_uncaught_exception);
		}
	}
	say("fb_nativeui: %s: native pixel modes enabled, density %.9f, artwork family 3\n", getprogname(), native_density());
}
