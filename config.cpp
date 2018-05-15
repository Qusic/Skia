#include "config.hpp"
#include <substrate.h>

void config::create_context() {
    script_context = JSGlobalContextCreate(NULL);
    for (const auto &native_function : native_functions) {
        JSStringRef function_name = JSStringCreateWithUTF8CString(native_function.first.c_str());
        JSObjectCallAsFunctionCallback function_callback = reinterpret_cast<JSObjectCallAsFunctionCallback>(MSFindSymbol(NULL, native_function.second.c_str()));
        JSObjectRef function_object = JSObjectMakeFunctionWithCallback(script_context, function_name, function_callback);
        JSObjectSetProperty(script_context, JSContextGetGlobalObject(script_context), function_name, function_object, 0, NULL);
        JSStringRelease(function_name);
    }
    JSStringRef support_script = read_script("/Library/PreferenceBundles/skiapref.bundle/proxy.js");
    JSEvaluateScript(script_context, support_script, NULL, NULL, 0, NULL);
    JSStringRelease(support_script);
    JSStringRef config_script = read_script("/User/Library/Preferences/me.qusic.skia.js");
    JSEvaluateScript(script_context, config_script, NULL, NULL, 0, NULL);
    JSStringRelease(config_script);
}

void config::release_context() {
    JSGlobalContextRelease(script_context);
    script_context = NULL;
}

JSStringRef config::read_script(const std::string &file) {
    CFStringRef path = CFStringCreateWithCString(kCFAllocatorDefault, file.c_str(), CFStringGetSystemEncoding());
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path, kCFURLPOSIXPathStyle, FALSE);
    CFRelease(path);
    CFReadStreamRef stream = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
    CFRelease(url);
    if (stream == NULL) {
        return JSStringCreateWithUTF8CString("");
    }
    if (CFReadStreamOpen(stream) == FALSE) {
        CFRelease(stream);
        return JSStringCreateWithUTF8CString("");
    }
    CFMutableDataRef data = CFDataCreateMutable(kCFAllocatorDefault, 0);
    UInt8 buffer[1024 * 8];
    CFIndex read = 0;
    while ((read = CFReadStreamRead(stream, buffer, sizeof(buffer))) > 0) {
        CFDataAppendBytes(data, buffer, read);
    }
    CFReadStreamClose(stream);
    CFRelease(stream);
    if (read == -1) {
        CFRelease(data);
        return JSStringCreateWithUTF8CString("");
    }
    CFStringRef string = CFStringCreateWithBytesNoCopy(kCFAllocatorDefault, CFDataGetBytePtr(data), CFDataGetLength(data), kCFStringEncodingUTF8, TRUE, kCFAllocatorNull);
    JSStringRef script = JSStringCreateWithCFString(string);
    CFRelease(string);
    CFRelease(data);
    return script;
}

void config::execute(const std::function<void(JSGlobalContextRef)> &code) {
    code(script_context);
}

std::string config::evaluate(const std::string &code) {
    std::string result;
    execute([&](JSGlobalContextRef context) {
        JSStringRef script = JSStringCreateWithUTF8CString(code.c_str());
        JSValueRef exception = NULL;
        JSValueRef value = JSEvaluateScript(context, script, NULL, NULL, 0, &exception);
        JSStringRelease(script);
        JSStringRef value_string = NULL;
        if (exception) {
            value_string = JSValueToStringCopy(context, exception, NULL);
        } else {
            if (JSValueIsObject(context, value)) {
                value_string = JSValueCreateJSONString(context, value, 2, NULL) ?: JSValueToStringCopy(context, value, NULL);
            } else {
                value_string = JSValueToStringCopy(context, value, NULL);
            }
        }
        char buffer[1024 * 8];
        JSStringGetUTF8CString(value_string, buffer, sizeof(buffer));
        JSStringRelease(value_string);
        result = buffer;
    });
    return result;
}
