/* Native keyboard dimensions at their providers, before view creation/layout.
 * All originals are captured before installing any override, including inherited
 * methods. Frame/bounds/center setters remain UIKit's own implementations.
 */
struct KeyboardSizeHook { Class owner; SEL selector; IMP original; unsigned arity; BOOL class_method; };
static struct KeyboardSizeHook keyboard_size_hooks[16];
static unsigned keyboard_size_hook_count, keyboard_size_calls, emoji_invalidation_calls;
static BOOL (*original_emoji_invalidation)(id, SEL, id, id);
static void (*original_keyplane_name)(id, SEL, id);
static id (*original_keyboard_hit)(id, SEL, CGPoint);
static unsigned keyplane_trace_count, key_hit_trace_count;

/* Opt-in observation of layout transitions, never entered text. */
static void
trace_keyplane_name(id self, SEL selector, id name)
{
    original_keyplane_name(self, selector, name);
    if (__atomic_fetch_add(&keyplane_trace_count, 1, __ATOMIC_RELAXED) >= 32) return;
    const char *label = name ? ((const char *(*)(id, SEL))objc_msgSend)(name, sel_registerName("UTF8String")) : "nil";
    say("kbtrace: %s: keyplane set %.120s\n", getprogname(), label ? label : "?");
}

static id
trace_keyboard_hit(id self, SEL selector, CGPoint point)
{
    id key = original_keyboard_hit(self, selector, point);
    if (point.x < 120 && point.y > 100 &&
        __atomic_fetch_add(&key_hit_trace_count, 1, __ATOMIC_RELAXED) < 48) {
        CGRect frame = {{0, 0}, {0, 0}};
        Method getter = key ? class_getInstanceMethod(object_getClass(key), sel_registerName("frame")) : NULL;
        if (getter && strcmp(method_getTypeEncoding(getter), "{CGRect={CGPoint=dd}{CGSize=dd}}16@0:8") == 0)
            frame = ((CGRect (*)(id, SEL))objc_msgSend)(key, sel_registerName("frame"));
        say("kbtrace: %s: lower-left hit %.6f,%.6f key %s frame %.6f,%.6f %.6fx%.6f\n",
            getprogname(), point.x, point.y, key ? object_getClassName(key) : "nil",
            frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
    }
    return key;
}

static void
install_native_keyboard_trace(void)
{
    Class cls = objc_getClass("UIKeyboardLayoutStar");
    SEL plane = sel_registerName("setKeyplaneName:");
    SEL hit = sel_registerName("keyHitTest:");
    Method set_plane = cls ? class_getInstanceMethod(cls, plane) : NULL;
    Method key_hit = cls ? class_getInstanceMethod(cls, hit) : NULL;
    if (set_plane && strcmp(method_getTypeEncoding(set_plane), "v24@0:8@16") == 0) {
        original_keyplane_name = (void *)method_getImplementation(set_plane);
        class_replaceMethod(cls, plane, (IMP)trace_keyplane_name, method_getTypeEncoding(set_plane));
        say("kbtrace: %s: keyplane observation installed\n", getprogname());
    }
    if (key_hit && strcmp(method_getTypeEncoding(key_hit), "@32@0:8{CGPoint=dd}16") == 0) {
        original_keyboard_hit = (void *)method_getImplementation(key_hit);
        class_replaceMethod(cls, hit, (IMP)trace_keyboard_hit, method_getTypeEncoding(key_hit));
        say("kbtrace: %s: lower-left geometry observation installed\n", getprogname());
    }
}

static IMP
keyboard_size_original(id self, SEL selector, BOOL class_method)
{
    Class current = class_method ? (Class)self : object_getClass(self);
    for (; current; current = class_getSuperclass(current))
        for (unsigned i = 0; i < keyboard_size_hook_count; ++i)
            if (keyboard_size_hooks[i].owner == current && keyboard_size_hooks[i].selector == selector &&
                keyboard_size_hooks[i].class_method == class_method)
                return keyboard_size_hooks[i].original;
    abort();
}

static CGSize
native_keyboard_size(CGSize size)
{
    if (size.width >= 320 && size.width <= 420 && size.height > 100 && size.height < 500) {
        double before = size.width;
        size.width = logical_width(1440);
        if (__atomic_fetch_add(&keyboard_size_calls, 1, __ATOMIC_RELAXED) < 16)
            say("fb_nativeui: %s: keyboard size provider %.3fx%.3f -> %.3fx%.3f\n",
                getprogname(), before, size.height, size.width, size.height);
    }
    return size;
}

static CGSize keyboard_class_size0(id self, SEL selector)
{
    return native_keyboard_size(((CGSize (*)(id, SEL))keyboard_size_original(self, selector, YES))(self, selector));
}
static CGSize keyboard_class_size1(id self, SEL selector, long orientation)
{
    return native_keyboard_size(((CGSize (*)(id, SEL, long))keyboard_size_original(self, selector, YES))(self, selector, orientation));
}
static CGSize keyboard_class_size2(id self, SEL selector, long orientation, BOOL first)
{
    return native_keyboard_size(((CGSize (*)(id, SEL, long, BOOL))keyboard_size_original(self, selector, YES))(self, selector, orientation, first));
}
static CGSize keyboard_class_size3(id self, SEL selector, long orientation, BOOL first, BOOL second)
{
    return native_keyboard_size(((CGSize (*)(id, SEL, long, BOOL, BOOL))keyboard_size_original(self, selector, YES))(self, selector, orientation, first, second));
}
static CGSize keyboard_instance_size1(id self, SEL selector, long orientation)
{
    return native_keyboard_size(((CGSize (*)(id, SEL, long))keyboard_size_original(self, selector, NO))(self, selector, orientation));
}

static void
collect_keyboard_size(Class owner, const char *name, unsigned arity, BOOL class_method)
{
    if (!owner || keyboard_size_hook_count >= 16) return;
    SEL selector = sel_registerName(name);
    Method method = class_method ? class_getClassMethod(owner, selector) : class_getInstanceMethod(owner, selector);
    static const char *abi[] = {"{CGSize=dd}16@0:8", "{CGSize=dd}24@0:8q16",
                               "{CGSize=dd}28@0:8q16B24", "{CGSize=dd}32@0:8q16B24B28"};
    if (!method || arity > 3 || strcmp(method_getTypeEncoding(method), abi[arity]) != 0) return;
    keyboard_size_hooks[keyboard_size_hook_count++] = (struct KeyboardSizeHook){
        owner, selector, method_getImplementation(method), arity, class_method };
}

static BOOL
same_native_pixel(double first, double second)
{
    if (!isfinite(first) || !isfinite(second)) return NO;
    return fabs(first-second) < 0.0000001 || nearbyint(first*native_density()) == nearbyint(second*native_density());
}

static BOOL
native_emoji_invalidation(id self, SEL selector, id preferred, id original)
{
    BOOL result = original_emoji_invalidation(self, selector, preferred, original);
    if (!result || !preferred || !original) return result;
    CGRect first = ((CGRect (*)(id, SEL))objc_msgSend)(preferred, sel_registerName("frame"));
    CGRect second = ((CGRect (*)(id, SEL))objc_msgSend)(original, sel_registerName("frame"));
    BOOL same = same_native_pixel(first.origin.x, second.origin.x) && same_native_pixel(first.origin.y, second.origin.y) &&
                same_native_pixel(first.size.width, second.size.width) && same_native_pixel(first.size.height, second.size.height);
    if (__atomic_fetch_add(&emoji_invalidation_calls, 1, __ATOMIC_RELAXED) < 24)
        say("fb_nativeui: %s: emoji preferred %.9f,%.9f %.9fx%.9f original %.9f,%.9f %.9fx%.9f same physical pixels %d\n",
            getprogname(), first.origin.x, first.origin.y, first.size.width, first.size.height,
            second.origin.x, second.origin.y, second.size.width, second.size.height, same);
    /* Retain invalidation for every actual pixel change. Avoid re-layout only
     * when fractional point arithmetic describes the same physical rectangle. */
    return same ? NO : result;
}

static void
install_native_keyboard_sizes(void)
{
    const char *owners[] = {"UIKeyboard", "UIKeyboardImpl"};
    for (unsigned i = 0; i < 2; ++i) {
        Class owner = objc_getClass(owners[i]);
        collect_keyboard_size(owner, "defaultSize", 0, YES);
        collect_keyboard_size(owner, "defaultSizeForInterfaceOrientation:", 1, YES);
        collect_keyboard_size(owner, "keyboardSizeForInterfaceOrientation:", 1, YES);
        collect_keyboard_size(owner, "sizeForInterfaceOrientation:", 1, YES);
        collect_keyboard_size(owner, "sizeForInterfaceOrientation:ignoreInputView:", 2, YES);
        collect_keyboard_size(owner, "sizeForInterfaceOrientation:includingAssistantBar:ignoreInputView:", 3, YES);
    }
    collect_keyboard_size(objc_getClass("UIKeyboardImpl"), "defaultSizeForInterfaceOrientation:", 1, NO);
    for (unsigned i = 0; i < keyboard_size_hook_count; ++i) {
        struct KeyboardSizeHook *hook = &keyboard_size_hooks[i];
        IMP implementation = (IMP)keyboard_instance_size1;
        if (hook->class_method) {
            const IMP handlers[] = {(IMP)keyboard_class_size0, (IMP)keyboard_class_size1,
                                    (IMP)keyboard_class_size2, (IMP)keyboard_class_size3};
            implementation = handlers[hook->arity];
        }
        Class target = hook->class_method ? object_getClass(hook->owner) : hook->owner;
        const char *abi[] = {"{CGSize=dd}16@0:8", "{CGSize=dd}24@0:8q16",
                            "{CGSize=dd}28@0:8q16B24", "{CGSize=dd}32@0:8q16B24B28"};
        class_replaceMethod(target, hook->selector, implementation, abi[hook->arity]);
    }
    Class layout = objc_getClass("UIKeyboardEmojiLayout");
    SEL invalidation = sel_registerName("shouldInvalidateLayoutForPreferredLayoutAttributes:withOriginalAttributes:");
    Method method = layout ? class_getInstanceMethod(layout, invalidation) : NULL;
    if (method && strcmp(method_getTypeEncoding(method), "B32@0:8@16@24") == 0) {
        original_emoji_invalidation = (void *)method_getImplementation(method);
        class_replaceMethod(layout, invalidation, (IMP)native_emoji_invalidation, method_getTypeEncoding(method));
    }
    say("fb_nativeui: %s: %u keyboard size providers installed; UIKit geometry setters unchanged\n", getprogname(), keyboard_size_hook_count);
}
